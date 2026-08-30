#include "ygo/policy/runner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::policy {
namespace {

using Boundary = std::variant<environment::DecisionFrame, environment::EpisodeTerminal,
                              environment::EpisodeInterrupted, environment::EpisodeFailure>;

PolicyRunnerResult failed_result(std::string message,
                                 std::optional<PolicyError> policy_error = std::nullopt) {
    PolicyRunnerResult result;
    result.disposition = PolicyRunnerDisposition::Failed;
    result.diagnostic = std::move(message);
    result.policy_error = std::move(policy_error);
    return result;
}

const trajectory::ParticipantPolicyAssignment* assignment_for_player(
    const trajectory::PolicyProvenanceEnvelope& provenance,
    const std::uint8_t player,
    std::string& error) {
    const trajectory::ParticipantPolicyAssignment* result = nullptr;
    for (const auto& assignment : provenance.participant_assignments) {
        if (assignment.player != player) {
            continue;
        }
        if (result != nullptr) {
            error = "runner requires one epoch-zero assignment per player";
            return nullptr;
        }
        result = &assignment;
    }
    if (result == nullptr) {
        error = "runner lacks a participant assignment for a player";
    }
    return result;
}

const trajectory::PolicyArtifact* artifact_for(
    const trajectory::PolicyProvenanceEnvelope& provenance,
    const std::string_view artifact_id) noexcept {
    const auto it = std::find_if(
        provenance.policy_artifacts.begin(), provenance.policy_artifacts.end(),
        [artifact_id](const auto& artifact) { return artifact.policy_artifact_id == artifact_id; });
    return it == provenance.policy_artifacts.end() ? nullptr : &*it;
}

bool execution_binding_fields_match(const PolicyExecutionBinding& execution,
                                    const trajectory::PolicyRngStreamIdentity& stream) noexcept {
    return execution.policy_artifact_id == stream.policy_artifact_id &&
           execution.participant_policy_assignment_id ==
               stream.participant_policy_assignment_id &&
           execution.policy_rng_contract_identity == stream.policy_rng_contract_identity &&
           execution.policy_rng_stream_id == stream.policy_rng_stream_id &&
           execution.policy_rng_initialization_identity ==
               stream.policy_rng_initialization_identity &&
           execution.policy_rng_identity == stream.policy_rng_identity;
}

bool validate_execution_binding(const RandomLegalExecutionBinding& binding,
                                const std::uint8_t player,
                                const PolicyRunnerConfig& config,
                                const trajectory::ProvenanceResolver& resolver,
                                std::string& error) {
    const auto* assignment = assignment_for_player(config.policy_provenance, player, error);
    if (assignment == nullptr) {
        return false;
    }
    const auto* artifact = artifact_for(
        config.policy_provenance, binding.execution_binding.policy_artifact_id);
    if (artifact == nullptr || artifact->policy_kind != trajectory::PolicyKind::RandomLegal) {
        error = "runner binding references a non-RandomLegal artifact";
        return false;
    }
    if (assignment->participant_policy_assignment_id !=
            binding.execution_binding.participant_policy_assignment_id ||
        assignment->policy_artifact_id != binding.execution_binding.policy_artifact_id ||
        assignment->player != player) {
        error = "runner binding does not match the player assignment";
        return false;
    }
    if (binding.initialization.policy_rng_contract_identity !=
            artifact->policy_rng_contract_identity ||
        binding.stream.policy_artifact_id != artifact->policy_artifact_id ||
        binding.stream.participant_policy_assignment_id !=
            assignment->participant_policy_assignment_id ||
        binding.stream.policy_rng_contract_identity !=
            artifact->policy_rng_contract_identity ||
        binding.stream.policy_rng_initialization_identity !=
            binding.initialization.policy_rng_initialization_identity ||
        !execution_binding_fields_match(binding.execution_binding, binding.stream)) {
        error = "runner binding identity fields are inconsistent";
        return false;
    }
    if (trajectory::compute_policy_rng_stream_id(binding.stream) !=
        binding.stream.policy_rng_identity) {
        error = "runner binding stream identity does not recompute";
        return false;
    }
    try {
        (void)trajectory::canonical_policy_rng_initialization_identity_bytes(
            binding.initialization);
        (void)trajectory::canonical_policy_rng_stream_identity_bytes(binding.stream);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!trajectory::validate_policy_rng_initialization_material(
            binding.initialization, binding.initialization.initialization_material, resolver,
            &error)) {
        return false;
    }
    const auto* descriptor = resolver.policy_rng_contract_descriptor(
        binding.initialization.policy_rng_contract_identity);
    if (descriptor == nullptr || !descriptor->cursor_is_unique ||
        !descriptor->cursor_is_unique(binding.initialization)) {
        error = "runner binding lacks a unique typed RNG initialization";
        return false;
    }
    return true;
}

std::optional<trajectory::TerminalViews> terminal_views_for(
    const environment::EpisodicEnvironment& environment) {
    const auto player_zero = environment.perspective_terminal_view(0);
    const auto player_one = environment.perspective_terminal_view(1);
    if (!player_zero.has_value() || !player_one.has_value()) {
        return std::nullopt;
    }
    return trajectory::TerminalViews{*player_zero, *player_one};
}

trajectory::PolicyRngDecisionProvenance make_attribution(
    const environment::DecisionFrame& frame,
    const RandomLegalExecutionBinding& binding,
    const PolicySelectionResult& selection) {
    trajectory::PolicyRngDecisionProvenance attribution;
    attribution.decision_index = frame.decision_index;
    attribution.acting_policy_assignment_id =
        binding.execution_binding.participant_policy_assignment_id;
    if (!selection.rng_cursor.has_value()) {
        attribution.policy_rng_identity = trajectory::kNoPolicyRngContractId;
        attribution.policy_rng_contract_identity = trajectory::kNoPolicyRngContractId;
        attribution.policy_rng_stream_id = trajectory::kNoPolicyRngContractId;
        attribution.policy_rng_initialization_identity = trajectory::kNoPolicyRngContractId;
        attribution.mode = trajectory::PolicyRngMode::None;
        return attribution;
    }
    attribution.policy_rng_identity = binding.execution_binding.policy_rng_identity;
    attribution.policy_rng_contract_identity =
        binding.execution_binding.policy_rng_contract_identity;
    attribution.policy_rng_stream_id = binding.execution_binding.policy_rng_stream_id;
    attribution.policy_rng_initialization_identity =
        binding.execution_binding.policy_rng_initialization_identity;
    attribution.mode = trajectory::PolicyRngMode::Cursor;
    attribution.pre_cursor = selection.rng_cursor->pre_cursor;
    attribution.post_cursor = selection.rng_cursor->post_cursor;
    return attribution;
}

trajectory::RestrictedReplayEvidence restricted_evidence_for(
    const environment::EpisodeInterrupted& interruption) {
    trajectory::RestrictedReplayEvidence result;
    result.v2_contract_id = interruption.contract_id;
    result.episode_semantic_id = interruption.episode_semantic_id;
    result.interruption_reason = interruption.reason;
    result.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    result.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    result.observed_engine_process_count =
        interruption.run_control_evidence.engine_process_count;
    result.observed_semantic_action_count =
        interruption.run_control_evidence.semantic_action_count;
    result.final_engine_step_index = interruption.final_engine_step_index;
    return result;
}

bool add_rng_initialization_evidence(
    const PolicyRunnerConfig& config,
    const trajectory::DecisionRecord& record,
    trajectory::RestrictedCollectionEvidenceBundle& evidence,
    std::string& error) {
    const auto& attribution = record.policy_rng_decision_provenance;
    if (attribution.mode == trajectory::PolicyRngMode::None) {
        return true;
    }
    if (record.frame.acting_player > 1) {
        error = "record has an invalid acting player for RNG evidence";
        return false;
    }
    const auto& binding = config.execution_bindings[record.frame.acting_player];
    if (attribution.policy_rng_initialization_identity !=
            binding.initialization.policy_rng_initialization_identity ||
        attribution.policy_rng_identity != binding.stream.policy_rng_identity) {
        error = "record RNG attribution does not match its execution binding";
        return false;
    }
    const auto existing = std::find_if(
        evidence.rng_initializations.begin(), evidence.rng_initializations.end(),
        [&](const auto& item) {
            return item.policy_rng_initialization_identity ==
                   binding.initialization.policy_rng_initialization_identity;
        });
    if (existing == evidence.rng_initializations.end()) {
        evidence.rng_initializations.push_back(
            {binding.initialization.policy_rng_initialization_identity,
             binding.initialization.initialization_material});
    } else if (existing->initialization_material !=
               binding.initialization.initialization_material) {
        error = "one RNG initialization identity has conflicting raw material";
        return false;
    }
    return true;
}

bool build_dataset_manifest(const trajectory::VerifiedAdmissionReceipt& receipt,
                            trajectory::DatasetManifest& output,
                            std::string& error) {
    try {
        const auto& admission_receipt = receipt.receipt();
        const auto receipt_id = trajectory::admission_receipt_id(admission_receipt);
        for (const auto& entry : admission_receipt.entries) {
            output.members.push_back({entry.trajectory_record_id,
                                      entry.public_gameplay_trajectory_id,
                                      receipt_id,
                                      admission_receipt.candidate_shard_artifact_sha256,
                                      entry.episode_envelope_sha256});
        }
        std::sort(output.members.begin(), output.members.end(),
                  [](const auto& left, const auto& right) {
                      return left.trajectory_record_id < right.trajectory_record_id;
                  });
        std::vector<std::string> record_ids;
        record_ids.reserve(output.members.size());
        for (const auto& member : output.members) {
            record_ids.push_back(member.trajectory_record_id);
        }
        output.dataset_semantic_id = trajectory::dataset::dataset_semantic_id(record_ids);
        (void)trajectory::dataset::canonical_dataset_manifest_bytes(output);
        if (!trajectory::dataset::validate_dataset_manifest(
                output, std::vector<trajectory::VerifiedAdmissionReceipt>{receipt}, &error)) {
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "dataset manifest construction threw";
        return false;
    }
}

PolicyRunnerResult finalize_run(
    const PolicyRunnerConfig& config,
    trajectory::TrajectoryRecorder& recorder,
    const trajectory::ProvenanceResolver& resolver,
    const std::optional<environment::EpisodeInterrupted>& interruption,
    const bool quarantined) noexcept {
    try {
        std::string error;
        const auto sealed = recorder.seal(&error);
        if (!sealed.has_value()) {
            return failed_result("runner could not seal trajectory: " + error);
        }
        PolicyRunnerResult result;
        result.envelope = *sealed;

        const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes(*result.envelope);
        trajectory::CandidateTrajectoryShard shard;
        shard.entries.push_back({ygo::trace::sha256_bytes(envelope_bytes), envelope_bytes});
        const auto shard_artifact = trajectory::candidate_shard_artifact_sha256(shard);

        trajectory::RestrictedCollectionEvidenceBundle evidence;
        evidence.candidate_shard_artifact_sha256 = shard_artifact;
        for (const auto& record : result.envelope->records) {
            if (!add_rng_initialization_evidence(config, record, evidence, error)) {
                return failed_result("runner could not build RNG evidence: " + error);
            }
        }
        std::sort(evidence.rng_initializations.begin(), evidence.rng_initializations.end(),
                  [](const auto& left, const auto& right) {
                      return left.policy_rng_initialization_identity <
                             right.policy_rng_initialization_identity;
                  });

        if (interruption.has_value()) {
            if (!std::holds_alternative<trajectory::InterruptedClosure>(result.envelope->closure)) {
                return failed_result("runner received interruption evidence for a non-interrupted closure");
            }
            evidence.interrupted_episodes.push_back(
                {shard.entries.front().episode_envelope_sha256,
                 restricted_evidence_for(*interruption)});
        } else if (std::holds_alternative<trajectory::InterruptedClosure>(result.envelope->closure)) {
            return failed_result("runner sealed an interrupted closure without V2 evidence");
        }
        (void)trajectory::canonical_restricted_collection_evidence_bundle_bytes(evidence);
        const auto evidence_artifact =
            trajectory::restricted_collection_evidence_artifact_sha256(evidence);
        result.candidate_shard = shard;
        result.restricted_evidence = evidence;

        if (std::holds_alternative<trajectory::FailedClosure>(result.envelope->closure)) {
            return failed_result("V2 run closed with an EpisodeFailure");
        }

        trajectory::admission::ReplayOptions options;
        options.cancellation_source = config.run_control.cancellation.source;
        if (std::holds_alternative<trajectory::TerminalClosure>(result.envelope->closure)) {
            options.terminal_run_control = config.run_control;
        }
        std::string admission_error;
        auto verification = trajectory::admission::verify_candidate_shard_for_admission(
            *result.candidate_shard, *result.restricted_evidence, shard_artifact, evidence_artifact,
            options, resolver, &admission_error);
        if (quarantined) {
            if (verification.has_value()) {
                return failed_result("quarantined trajectory unexpectedly passed admission");
            }
            result.disposition = PolicyRunnerDisposition::Quarantined;
            result.diagnostic = "quarantined trajectory was rejected by clean admission";
            if (!admission_error.empty()) {
                result.diagnostic += ": " + admission_error;
            }
            return result;
        }
        if (!verification.has_value()) {
            return failed_result("clean trajectory failed admission: " + admission_error);
        }
        result.admission_verification = std::move(*verification);
        auto receipt = trajectory::issue_admission_receipt(
            *result.admission_verification, &error);
        if (!receipt.has_value()) {
            return failed_result("runner could not issue an admission receipt: " + error);
        }
        result.admission_receipt = std::move(*receipt);
        trajectory::DatasetManifest manifest;
        if (!build_dataset_manifest(*result.admission_receipt, manifest, error)) {
            return failed_result("runner could not validate the dataset manifest: " + error);
        }
        result.dataset_manifest = std::move(manifest);
        result.disposition = PolicyRunnerDisposition::CleanAdmitted;
        return result;
    } catch (const std::exception& exception) {
        return failed_result(exception.what());
    } catch (...) {
        return failed_result("runner finalization threw");
    }
}

}  // namespace

PolicyRunnerCreateResult PolicyRunner::create(PolicyRunnerConfig config) noexcept {
    try {
        const auto resolver = make_production_policy_provenance_resolver();
        for (std::uint8_t player = 0; player < 2; ++player) {
            if (!config.selectors[player]) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "runner lacks a selector for a player"}};
            }
            std::string error;
            if (!validate_execution_binding(config.execution_bindings[player], player, config,
                                            resolver, error)) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    std::move(error)}};
            }
        }
        if (config.execution_bindings[0].stream.policy_rng_identity ==
            config.execution_bindings[1].stream.policy_rng_identity) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "runner reuses one policy RNG identity for two players"}};
        }

        auto factory = environment::EpisodicEnvironment::create(config.environment_config);
        const auto* rejected = std::get_if<environment::EnvironmentFactoryRejected>(&factory);
        if (rejected != nullptr) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::LifecycleFailure,
                                "V2 environment factory rejected " +
                                    std::string(environment::reset_rejection_code_name(
                                        rejected->rejection_code))}};
        }
        auto environment = std::move(
            *std::get_if<std::unique_ptr<environment::EpisodicEnvironment>>(&factory));
        if (environment == nullptr) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::LifecycleFailure,
                                "V2 environment factory returned no environment"}};
        }
        auto recorder = std::make_unique<trajectory::TrajectoryRecorder>(
            config.environment_config, config.episode_spec, config.policy_provenance, resolver);
        return {std::optional<PolicyRunner>(PolicyRunner(
                    std::move(config), std::move(environment), std::move(recorder), resolver)),
                std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, exception.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "policy runner construction threw"}};
    }
}

PolicyRunnerResult PolicyRunner::run() noexcept {
    if (has_run_) {
        return failed_result("policy runner can only execute one collection run");
    }
    has_run_ = true;
    try {
        const auto reset = environment_->reset(config_.episode_spec, config_.run_control);
        const auto* reset_accepted = std::get_if<environment::ResetAccepted>(&reset);
        if (reset_accepted == nullptr) {
            const auto* rejected = std::get_if<environment::ResetRejected>(&reset);
            return failed_result(
                rejected == nullptr
                    ? "V2 reset returned an unknown result"
                    : "V2 reset was rejected: " +
                          std::string(environment::reset_rejection_code_name(
                              rejected->rejection_code)));
        }

        Boundary boundary = reset_accepted->next;
        std::optional<environment::EpisodeInterrupted> interruption;
        std::optional<trajectory::TerminalViews> terminal_views;
        if (std::holds_alternative<environment::EpisodeTerminal>(boundary)) {
            terminal_views = terminal_views_for(*environment_);
        }
        std::string recorder_error;
        if (!recorder_->on_reset_accepted(*reset_accepted, terminal_views, &recorder_error)) {
            return failed_result("recorder rejected V2 reset: " + recorder_error);
        }
        if (const auto* reset_interrupted =
                std::get_if<environment::EpisodeInterrupted>(&boundary)) {
            interruption = *reset_interrupted;
        }
        if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
            return finalize_run(
                config_, *recorder_, resolver_, interruption,
                recorder_->manifest().collection_disposition.kind ==
                    trajectory::CollectionDispositionKind::QuarantinedAfterPolicyRejection);
        }

        for (;;) {
            const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (frame == nullptr || frame->acting_player > 1) {
                return failed_result("runner reached a non-actionable or invalid V2 frame");
            }
            const auto& binding = config_.execution_bindings[frame->acting_player];
            const PolicyInput input{frame->public_observation, frame->request.candidates};
            PolicySelection selection;
            try {
                selection = config_.selectors[frame->acting_player](input);
            } catch (const std::exception& exception) {
                return failed_result(
                    "policy selector threw: " + std::string(exception.what()),
                    PolicyError{PolicyErrorCode::InvalidConfiguration, exception.what()});
            } catch (...) {
                return failed_result(
                    "policy selector threw",
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "policy selector threw"});
            }
            if (!selection) {
                return failed_result(
                    selection.error.has_value() ? selection.error->message
                                                : "policy selector returned no selection",
                    selection.error.has_value()
                        ? selection.error
                        : std::optional<PolicyError>{PolicyError{
                              PolicyErrorCode::InvalidConfiguration,
                              "policy selector returned no selection"}});
            }
            if (selection.value->rng_cursor.has_value() &&
                selection.value->rng_cursor->post_cursor <
                    selection.value->rng_cursor->pre_cursor) {
                return failed_result(
                    "policy selector returned a wrapping cursor transition",
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "policy selector returned a wrapping cursor transition"});
            }

            environment::ActionSelection action;
            action.contract_id = frame->contract_id;
            action.episode_semantic_id = frame->episode_semantic_id;
            action.public_semantic_decision_id = frame->public_semantic_decision_id;
            action.submission_token = frame->submission_token;
            action.public_action_key = selection.value->public_action_key;
            const auto pre_rejection_frame = *frame;
            const auto stepped = environment_->step(action);
            if (const auto* rejected = std::get_if<environment::StepRejected>(&stepped)) {
                if (!recorder_->on_step_rejected(*rejected, true, &recorder_error)) {
                    return failed_result("recorder rejected policy-origin StepRejected: " +
                                         recorder_error);
                }
                const auto interrupted = environment_->interrupt(environment::InterruptRequest{
                    std::string(environment::kEpisodicEnvironmentContractId),
                    environment::InterruptionReason::AdministrativeCancel});
                if (const auto* accepted_interrupt =
                        std::get_if<environment::InterruptAccepted>(&interrupted)) {
                    if (!recorder_->on_interrupt_accepted(
                            std::optional<environment::DecisionFrame>{pre_rejection_frame},
                            *accepted_interrupt, &recorder_error)) {
                        return failed_result("recorder rejected administrative quarantine: " +
                                             recorder_error);
                    }
                    return finalize_run(config_, *recorder_, resolver_,
                                        accepted_interrupt->interruption, true);
                }
                if (const auto* interrupt_failure =
                        std::get_if<environment::EpisodeFailure>(&interrupted)) {
                    (void)recorder_->on_failure(*interrupt_failure, &recorder_error);
                    return failed_result(
                        "administrative quarantine returned an EpisodeFailure: " +
                        (recorder_error.empty() ? std::string("fail-closed") : recorder_error));
                }
                const auto* interrupt_rejected =
                    std::get_if<environment::InterruptRejected>(&interrupted);
                return failed_result(
                    interrupt_rejected == nullptr
                        ? "administrative quarantine returned an unknown result"
                        : "administrative quarantine was rejected: " +
                              std::string(environment::rejection_code_name(
                                  interrupt_rejected->rejection_code)));
            }

            const auto* accepted = std::get_if<environment::StepAccepted>(&stepped);
            if (accepted == nullptr) {
                return failed_result("V2 step returned an unknown result");
            }
            const auto attribution = make_attribution(*frame, binding, *selection.value);
            terminal_views.reset();
            if (std::holds_alternative<environment::EpisodeTerminal>(accepted->next)) {
                terminal_views = terminal_views_for(*environment_);
            }
            if (!recorder_->on_step_accepted(*accepted, attribution, terminal_views,
                                             &recorder_error)) {
                return failed_result("recorder rejected accepted V2 step: " + recorder_error);
            }
            interruption.reset();
            if (const auto* next_interrupted =
                    std::get_if<environment::EpisodeInterrupted>(&accepted->next)) {
                interruption = *next_interrupted;
            }
            if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
                return finalize_run(
                    config_, *recorder_, resolver_, interruption,
                    recorder_->manifest().collection_disposition.kind ==
                        trajectory::CollectionDispositionKind::QuarantinedAfterPolicyRejection);
            }
            boundary = accepted->next;
        }
    } catch (const std::exception& exception) {
        return failed_result(exception.what());
    } catch (...) {
        return failed_result("policy runner execution threw");
    }
}

}  // namespace ygo::policy
