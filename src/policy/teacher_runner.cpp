#include "ygo/policy/teacher_runner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "runner_shared.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/shard.hpp"

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
            error = "Teacher runner requires one assignment per player";
            return nullptr;
        }
        result = &assignment;
    }
    if (result == nullptr) {
        error = "Teacher runner lacks a participant assignment for a player";
    }
    return result;
}

bool add_teacher_rng_evidence(
    const trajectory::DecisionRecord& record,
    std::string& error) {
    const auto& attribution = record.policy_rng_decision_provenance;
    if (attribution.mode != trajectory::PolicyRngMode::None ||
        attribution.policy_rng_identity != trajectory::kNoPolicyRngContractId ||
        attribution.policy_rng_contract_identity != trajectory::kNoPolicyRngContractId ||
        attribution.policy_rng_stream_id != trajectory::kNoPolicyRngContractId ||
        attribution.policy_rng_initialization_identity != trajectory::kNoPolicyRngContractId ||
        attribution.pre_cursor.has_value() || attribution.post_cursor.has_value() ||
        attribution.pre_state.has_value() || attribution.post_state.has_value()) {
        error = "Teacher record contains non-NONE RNG attribution";
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
        return trajectory::dataset::validate_dataset_manifest(
            output, std::vector<trajectory::VerifiedAdmissionReceipt>{receipt}, &error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "Teacher dataset manifest construction threw";
        return false;
    }
}

PolicyRunnerResult finalize_teacher_run(
    const TeacherRunnerConfig& config,
    trajectory::TrajectoryRecorder& recorder,
    const trajectory::ProvenanceResolver& resolver,
    const std::optional<environment::EpisodeInterrupted>& interruption,
    const bool quarantined) noexcept {
    try {
        std::string error;
        const auto sealed = recorder.seal(&error);
        if (!sealed.has_value()) {
            return failed_result("Teacher runner could not seal trajectory: " + error);
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
            if (!add_teacher_rng_evidence(record, error)) {
                return failed_result("Teacher runner could not build RNG evidence: " + error);
            }
        }
        if (interruption.has_value()) {
            if (!std::holds_alternative<trajectory::InterruptedClosure>(result.envelope->closure)) {
                return failed_result("Teacher runner interruption evidence has the wrong closure");
            }
            evidence.interrupted_episodes.push_back(
                {shard.entries.front().episode_envelope_sha256,
                 detail::restricted_replay_evidence_for_interruption(*interruption)});
        } else if (std::holds_alternative<trajectory::InterruptedClosure>(result.envelope->closure)) {
            return failed_result("Teacher runner sealed interruption without V2 evidence");
        }
        (void)trajectory::canonical_restricted_collection_evidence_bundle_bytes(evidence);
        const auto evidence_artifact =
            trajectory::restricted_collection_evidence_artifact_sha256(evidence);
        result.candidate_shard = shard;
        result.restricted_evidence = evidence;

        if (std::holds_alternative<trajectory::FailedClosure>(result.envelope->closure)) {
            return failed_result("Teacher V2 run closed with an EpisodeFailure");
        }
        trajectory::admission::ReplayOptions options;
        options.cancellation_source = config.run_control.cancellation.source;
        if (std::holds_alternative<trajectory::TerminalClosure>(result.envelope->closure)) {
            options.terminal_run_control = config.run_control;
        }
        std::string admission_error;
        auto verification = trajectory::admission::verify_candidate_shard_for_admission(
            *result.candidate_shard, *result.restricted_evidence, shard_artifact,
            evidence_artifact, options, resolver, &admission_error);
        if (quarantined) {
            if (verification.has_value()) {
                return failed_result("quarantined Teacher trajectory passed admission");
            }
            result.disposition = PolicyRunnerDisposition::Quarantined;
            result.diagnostic = "quarantined Teacher trajectory was rejected by clean admission";
            if (!admission_error.empty()) {
                result.diagnostic += ": " + admission_error;
            }
            return result;
        }
        if (!verification.has_value()) {
            return failed_result("clean Teacher trajectory failed admission: " + admission_error);
        }
        result.admission_verification = std::move(*verification);
        auto receipt = trajectory::issue_admission_receipt(
            *result.admission_verification, &error);
        if (!receipt.has_value()) {
            return failed_result("Teacher runner could not issue admission receipt: " + error);
        }
        result.admission_receipt = std::move(*receipt);
        trajectory::DatasetManifest manifest;
        if (!build_dataset_manifest(*result.admission_receipt, manifest, error)) {
            return failed_result("Teacher runner could not validate dataset manifest: " + error);
        }
        result.dataset_manifest = std::move(manifest);
        result.disposition = PolicyRunnerDisposition::CleanAdmitted;
        return result;
    } catch (const std::exception& exception) {
        return failed_result(exception.what());
    } catch (...) {
        return failed_result("Teacher runner finalization threw");
    }
}

}  // namespace

TeacherRunnerCreateResult TeacherRunner::create(TeacherRunnerConfig config) noexcept {
    try {
        const auto resolver = make_production_policy_provenance_resolver();
        std::string error;
        if (config.policy_provenance.policy_artifacts.size() != 2 ||
            config.policy_provenance.participant_assignments.size() != 2) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "Teacher runner requires exactly two current profile artifacts and assignments"}};
        }
        if (!resolver.validate(config.policy_provenance, config.environment_config,
                               config.episode_spec, &error)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration, std::move(error)}};
        }
        for (std::uint8_t player = 0; player < 2; ++player) {
            if (!config.sessions[player].has_value()) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "Teacher runner lacks a session for a player"}};
            }
            const auto* assignment = assignment_for_player(
                config.policy_provenance, player, error);
            if (assignment == nullptr) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration, std::move(error)}};
            }
            const auto& session = *config.sessions[player];
            if (session.assignment.player != player ||
                session.assignment.participant_policy_assignment_id !=
                    assignment->participant_policy_assignment_id ||
                session.assignment.policy_artifact_id != assignment->policy_artifact_id ||
                session.artifact.policy_artifact_id != assignment->policy_artifact_id ||
                session.policy.participant() != player ||
                session.policy.participant_policy_assignment_id() !=
                    assignment->participant_policy_assignment_id ||
                session.artifact.artifact_metadata_identity !=
                    std::optional<std::string>{session.policy.policy_binding().teacher_policy_binding_id}) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "Teacher session does not match its participant assignment"}};
            }
            if (!session.policy.profile().profile_id.empty() &&
                (session.policy.profile().own_deck_role !=
                     static_cast<std::uint8_t>(assignment->deck_role) ||
                 !teacher::validate_strategy_profile_binding(
                     session.policy.profile(), config.environment_config, &error) ||
                 !teacher::validate_teacher_policy_binding(
                     session.policy.policy_binding(), session.policy.profile(), &error))) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration, std::move(error)}};
            }
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
        return {std::optional<TeacherRunner>(TeacherRunner(
                    std::move(config), std::move(environment), std::move(recorder), resolver)),
                std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, exception.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "Teacher runner construction threw"}};
    }
}

PolicyRunnerResult TeacherRunner::run_impl(
    const detail::TeacherRunnerTestOverride* test_override) noexcept {
    if (has_run_) {
        return failed_result("Teacher runner can only execute one collection run");
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
            terminal_views = detail::terminal_views_for_environment(*environment_);
        }
        std::string recorder_error;
        if (!recorder_->on_reset_accepted(*reset_accepted, terminal_views, &recorder_error)) {
            return failed_result("Teacher recorder rejected V2 reset: " + recorder_error);
        }
        if (const auto* reset_interrupted =
                std::get_if<environment::EpisodeInterrupted>(&boundary)) {
            interruption = *reset_interrupted;
        }
        if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
            return finalize_teacher_run(
                config_, *recorder_, resolver_, interruption,
                recorder_->manifest().collection_disposition.kind ==
                    trajectory::CollectionDispositionKind::QuarantinedAfterPolicyRejection);
        }

        for (;;) {
            const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (frame == nullptr || frame->acting_player > 1) {
                return failed_result("Teacher runner reached an invalid V2 frame");
            }
            auto& session = *config_.sessions[frame->acting_player];
            if (frame->public_observation.perspective_player != frame->acting_player) {
                return failed_result("Teacher runner received a cross-participant public frame");
            }
            const PolicyInput input{frame->public_observation, frame->request.candidates};
            PolicySelection selection;
            if (test_override != nullptr && test_override->player == frame->acting_player) {
                if (test_override->selection_calls != nullptr) {
                    ++*test_override->selection_calls;
                }
                switch (test_override->behavior) {
                case detail::TeacherRunnerTestSelectorBehavior::InvalidPublicAction:
                    selection.value = PolicySelectionResult{
                        "not-a-public-action-key", std::nullopt};
                    break;
                case detail::TeacherRunnerTestSelectorBehavior::PolicyFailure:
                    selection.error = PolicyError{
                        PolicyErrorCode::InvalidConfiguration, "test Teacher policy failure"};
                    break;
                }
            } else {
                selection = session.policy.select(input);
            }
            if (!selection) {
                return failed_result(
                    selection.error.has_value() ? selection.error->message
                                                : "Teacher returned no selection",
                    selection.error);
            }
            if (selection.value->rng_cursor.has_value()) {
                return failed_result(
                    "Teacher returned a policy RNG cursor",
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "Teacher returned a policy RNG cursor"});
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
                session.policy.reject_pending_proposal();
                if (!recorder_->on_step_rejected(*rejected, true, &recorder_error)) {
                    return failed_result("Teacher recorder rejected policy-origin StepRejected: " +
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
                        return failed_result("Teacher recorder rejected quarantine: " +
                                             recorder_error);
                    }
                    return finalize_teacher_run(config_, *recorder_, resolver_,
                                                accepted_interrupt->interruption, true);
                }
                return failed_result("Teacher administrative quarantine failed");
            }

            const auto* accepted = std::get_if<environment::StepAccepted>(&stepped);
            if (accepted == nullptr) {
                return failed_result("V2 step returned an unknown result");
            }
            if (!session.policy.commit(accepted->transition)) {
                return failed_result(
                    "Teacher accepted transition did not match its pending proposal",
                    PolicyError{PolicyErrorCode::LifecycleFailure,
                                "Teacher accepted transition did not match its pending proposal"});
            }
            const auto attribution = detail::make_policy_rng_attribution(
                *frame, session.execution_binding(), *selection.value);
            terminal_views.reset();
            if (std::holds_alternative<environment::EpisodeTerminal>(accepted->next)) {
                terminal_views = detail::terminal_views_for_environment(*environment_);
            }
            if (!recorder_->on_step_accepted(*accepted, attribution, terminal_views,
                                             &recorder_error)) {
                return failed_result("Teacher recorder rejected accepted V2 step: " +
                                     recorder_error);
            }
            interruption.reset();
            if (const auto* next_interrupted =
                    std::get_if<environment::EpisodeInterrupted>(&accepted->next)) {
                interruption = *next_interrupted;
            }
            if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
                return finalize_teacher_run(
                    config_, *recorder_, resolver_, interruption,
                    recorder_->manifest().collection_disposition.kind ==
                        trajectory::CollectionDispositionKind::QuarantinedAfterPolicyRejection);
            }
            boundary = accepted->next;
        }
    } catch (const std::exception& exception) {
        return failed_result(exception.what());
    } catch (...) {
        return failed_result("Teacher runner execution threw");
    }
}

PolicyRunnerResult TeacherRunner::run() noexcept {
    return run_impl(nullptr);
}

PolicyRunnerResult detail::TeacherRunnerTestAccess::run_with_test_selector(
    TeacherRunner& runner,
    const std::uint8_t player,
    const TeacherRunnerTestSelectorBehavior behavior,
    std::shared_ptr<std::size_t> selection_calls) {
    const TeacherRunnerTestOverride override_value{player, behavior,
                                                   std::move(selection_calls)};
    return runner.run_impl(&override_value);
}

}  // namespace ygo::policy
