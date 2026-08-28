#include "ygo/trajectory/admission.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

namespace ygo::trajectory::admission {
namespace {

void fail(ReplayResult& result, std::string message) {
    result.accepted = false;
    result.error = std::move(message);
}

bool safe_source(const std::string& value) noexcept {
    if (value.size() > 128) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7f) {
            return false;
        }
    }
    return true;
}

bool valid_control(const environment::RunControl& value) noexcept {
    return value.engine_process_budget != 0 && value.semantic_action_budget != 0 &&
           value.cancellation.reason == "ADMINISTRATIVE_CANCEL" &&
           safe_source(value.cancellation.source);
}

bool snapshot_from_frame(const environment::DecisionFrame& frame,
                         PublicFrameSnapshot& output,
                         std::string& error) {
    if (!frame.submission_token.valid()) {
        error = "replay received an invalid V2 submission token";
        return false;
    }
    output.v2_contract_id = frame.contract_id;
    output.episode_semantic_id = frame.episode_semantic_id;
    output.public_semantic_decision_id = frame.public_semantic_decision_id;
    output.decision_index = frame.decision_index;
    output.acting_player = frame.acting_player;
    output.public_observation = frame.public_observation;
    output.public_observation_digest = frame.public_observation_digest;
    output.request = frame.request;
    output.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    try {
        (void)canonical_public_frame_snapshot_bytes(output);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool equal_frame(const PublicFrameSnapshot& expected,
                 const environment::DecisionFrame& actual,
                 std::string& error) {
    PublicFrameSnapshot actual_snapshot;
    if (!snapshot_from_frame(actual, actual_snapshot, error)) {
        return false;
    }
    try {
        if (canonical_public_frame_snapshot_bytes(expected) !=
            canonical_public_frame_snapshot_bytes(actual_snapshot)) {
            error = "regenerated V2 public frame differs from the recorded frame";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool equal_observation(const environment::PublicEnvironmentObservation& expected,
                       const environment::PublicEnvironmentObservation& actual,
                       std::string& error) {
    try {
        if (environment::canonical_public_environment_observation_bytes(expected) !=
            environment::canonical_public_environment_observation_bytes(actual)) {
            error = "regenerated V2 terminal observation differs from the recorded view";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool compare_terminal(const EpisodeEnvelope& envelope,
                      environment::EpisodicEnvironment& environment,
                      const environment::EpisodeTerminal& actual,
                      std::string& error) {
    const auto* expected = std::get_if<TerminalClosure>(&envelope.closure);
    if (expected == nullptr || actual.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        actual.episode_semantic_id != envelope.manifest.episode_semantic_id ||
        actual.winner != expected->winner || actual.win_reason != expected->win_reason ||
        actual.semantic_action_count != expected->semantic_action_count ||
        actual.last_decision_index != expected->last_decision_index ||
        actual.semantic_action_count != envelope.records.size()) {
        error = "V2 terminal boundary differs from the recorded closure";
        return false;
    }
    const auto player_zero = environment.perspective_terminal_view(0);
    const auto player_one = environment.perspective_terminal_view(1);
    if (!player_zero.has_value() || !player_one.has_value() ||
        !equal_observation(expected->terminal_view_player_0, *player_zero, error) ||
        !equal_observation(expected->terminal_view_player_1, *player_one, error)) {
        if (error.empty()) {
            error = "V2 terminal boundary did not provide both public views";
        }
        return false;
    }
    return true;
}

bool compare_interrupted(const EpisodeEnvelope& envelope,
                         const RestrictedReplayEvidence& evidence,
                         const environment::EpisodeInterrupted& actual,
                         const bool administrative_pending,
                         std::uint64_t& final_engine_step,
                         std::string& error) {
    const auto* expected = std::get_if<InterruptedClosure>(&envelope.closure);
    if (expected == nullptr || actual.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        actual.episode_semantic_id != envelope.manifest.episode_semantic_id ||
        actual.reason != evidence.interruption_reason ||
        actual.run_control_evidence.engine_process_budget != evidence.engine_process_budget ||
        actual.run_control_evidence.semantic_action_budget != evidence.semantic_action_budget ||
        actual.run_control_evidence.engine_process_count != evidence.observed_engine_process_count ||
        actual.run_control_evidence.semantic_action_count != evidence.observed_semantic_action_count ||
        actual.semantic_action_count != expected->record_count ||
        actual.semantic_action_count != envelope.records.size() ||
        actual.final_engine_step_index != evidence.final_engine_step_index) {
        error = "V2 interruption boundary differs from restricted replay evidence";
        return false;
    }
    if (administrative_pending) {
        if (!expected->pending_unacted_frame.has_value() ||
            actual.last_decision_index !=
                std::optional<std::uint64_t>{expected->pending_unacted_frame->decision_index} ||
            actual.last_public_semantic_decision_id !=
                std::optional<std::string>{expected->pending_unacted_frame->public_semantic_decision_id}) {
            error = "administrative interruption does not identify the pending frame";
            return false;
        }
    } else {
        const auto expected_last_index = envelope.records.empty()
                                             ? std::optional<std::uint64_t>{}
                                             : std::optional<std::uint64_t>{envelope.records.size() - 1};
        const auto expected_last_id = envelope.records.empty()
                                          ? std::optional<std::string>{}
                                          : std::optional<std::string>{
                                                envelope.records.back().frame.public_semantic_decision_id};
        if (actual.last_decision_index != expected_last_index ||
            actual.last_public_semantic_decision_id != expected_last_id) {
            error = "budget interruption prefix differs from the recorded action prefix";
            return false;
        }
    }
    final_engine_step = actual.final_engine_step_index;
    return true;
}

bool resolve_identity_inputs(const EpisodeEnvelope& envelope,
                             environment::CertifiedEnvironmentConfig& config,
                             environment::EpisodeSpec& spec,
                             std::string& error) {
    const auto decoded_config = decode_environment_identity_input(
        envelope.manifest.environment_identity_input);
    if (!decoded_config || !is_current_certified_environment(*decoded_config.value)) {
        error = "episode environment identity is unknown or not the certified environment";
        return false;
    }
    config = *decoded_config.value;
    if (config.environment_semantic_id != envelope.manifest.environment_semantic_id ||
        environment::environment_semantic_id(config) != envelope.manifest.environment_semantic_id) {
        error = "episode environment identity does not match its manifest ID";
        return false;
    }
    const auto decoded_spec = decode_episode_identity_input(
        envelope.manifest.episode_identity_input, config);
    if (!decoded_spec ||
        environment::episode_semantic_id(config, *decoded_spec.value) !=
            envelope.manifest.episode_semantic_id) {
        error = "episode identity input does not match its manifest ID";
        return false;
    }
    spec = *decoded_spec.value;
    return true;
}

std::optional<environment::RunControl> replay_control(
    const EpisodeEnvelope& envelope,
    const std::optional<RestrictedReplayEvidence>& evidence,
    const ReplayOptions& options,
    std::string& error) {
    environment::RunControl control;
    if (std::holds_alternative<TerminalClosure>(envelope.closure)) {
        if (!options.terminal_run_control.has_value() ||
            !valid_control(*options.terminal_run_control)) {
            error = "terminal replay requires an explicit valid V2 run control";
            return std::nullopt;
        }
        return options.terminal_run_control;
    }
    if (!evidence.has_value() || !is_lower_hex_digest(evidence->episode_semantic_id) ||
        evidence->engine_process_budget == 0 || evidence->semantic_action_budget == 0 ||
        !options.cancellation_source.has_value() ||
        !safe_source(*options.cancellation_source)) {
        error = "interrupted replay lacks exact restricted run-control evidence";
        return std::nullopt;
    }
    control.engine_process_budget = evidence->engine_process_budget;
    control.semantic_action_budget = evidence->semantic_action_budget;
    control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    control.cancellation.source = *options.cancellation_source;
    if (!valid_control(control)) {
        error = "interrupted replay cancellation source is invalid";
        return std::nullopt;
    }
    return control;
}

bool expected_transition_class(const environment::EnvironmentActionCandidate& candidate,
                               const environment::EnvironmentDecisionRequest& request,
                               const bool core_response_submitted,
                               TransitionClass& output,
                               std::string& error) {
    if (!request.continuation.has_value()) {
        if (!candidate.submits_engine_response || !core_response_submitted) {
            error = "atomic V2 transition did not submit exactly one response";
            return false;
        }
        output = TransitionClass::AtomicEngineResponse;
        return true;
    }
    if (candidate.submits_engine_response) {
        if (!core_response_submitted) {
            error = "final continuation V2 transition did not submit a response";
            return false;
        }
        output = TransitionClass::FinalContinuationResponse;
        return true;
    }
    if (core_response_submitted) {
        error = "intermediate continuation submitted a response";
        return false;
    }
    output = TransitionClass::IntermediateContinuation;
    return true;
}

TransitionClass declared_transition_class(
    const environment::EnvironmentActionCandidate& candidate,
    const environment::EnvironmentDecisionRequest& request) noexcept {
    if (!request.continuation.has_value()) {
        return TransitionClass::AtomicEngineResponse;
    }
    return candidate.submits_engine_response ? TransitionClass::FinalContinuationResponse
                                              : TransitionClass::IntermediateContinuation;
}

bool same_public_frame_as_snapshot(const PublicFrameSnapshot& expected,
                                   const std::variant<environment::DecisionFrame,
                                                       environment::EpisodeTerminal,
                                                       environment::EpisodeInterrupted,
                                                       environment::EpisodeFailure>& boundary,
                                   std::string& error) {
    const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
    return frame != nullptr && equal_frame(expected, *frame, error);
}

const ParticipantPolicyAssignment* assignment_for(
    const PolicyProvenanceEnvelope& provenance,
    const std::uint8_t player,
    const std::uint64_t decision_index,
    std::string& error) {
    const ParticipantPolicyAssignment* selected = nullptr;
    for (const auto& assignment : provenance.participant_assignments) {
        if (assignment.player != player ||
            assignment.effective_from_decision_index > decision_index) {
            continue;
        }
        if (selected == nullptr ||
            assignment.effective_from_decision_index > selected->effective_from_decision_index) {
            selected = &assignment;
        } else if (assignment.effective_from_decision_index ==
                   selected->effective_from_decision_index) {
            error = "two participant assignments are active at one decision index";
            return nullptr;
        }
    }
    if (selected == nullptr) {
        error = "record has no active participant assignment";
    }
    return selected;
}

const PolicyArtifact* artifact_for(const PolicyProvenanceEnvelope& provenance,
                                   const std::string_view id) noexcept {
    const auto it = std::find_if(
        provenance.policy_artifacts.begin(), provenance.policy_artifacts.end(),
        [id](const PolicyArtifact& artifact) { return artifact.policy_artifact_id == id; });
    return it == provenance.policy_artifacts.end() ? nullptr : &*it;
}

bool validate_record_attribution(const EpisodeEnvelope& envelope,
                                 const DecisionRecord& record,
                                 std::string& error) {
    if (record.acting_policy_assignment_id !=
            record.policy_rng_decision_provenance.acting_policy_assignment_id ||
        record.policy_rng_decision_provenance.decision_index != record.frame.decision_index) {
        error = "record attribution does not match its surrounding record";
        return false;
    }
    const auto* assignment = assignment_for(
        envelope.manifest.policy_provenance, record.frame.acting_player,
        record.frame.decision_index, error);
    if (assignment == nullptr || assignment->participant_policy_assignment_id !=
                                    record.acting_policy_assignment_id) {
        if (error.empty()) {
            error = "record assignment is not the active assignment for its player";
        }
        return false;
    }
    const auto* artifact = artifact_for(envelope.manifest.policy_provenance,
                                        assignment->policy_artifact_id);
    if (artifact == nullptr) {
        error = "record assignment references an unknown policy artifact";
        return false;
    }
    const auto& attribution = record.policy_rng_decision_provenance;
    const bool artifact_uses_rng = artifact->policy_rng_contract_identity != kNoPolicyRngContractId;
    if (attribution.mode == PolicyRngMode::None) {
        if (artifact_uses_rng) {
            error = "stochastic policy record has NONE RNG attribution";
            return false;
        }
        return true;
    }
    if (!artifact_uses_rng || attribution.policy_rng_contract_identity !=
                                  artifact->policy_rng_contract_identity) {
        error = "record RNG attribution disagrees with its policy artifact";
        return false;
    }
    if (attribution.mode == PolicyRngMode::Cursor) {
        error = "CURSOR RNG admission lacks a proof of unique cursor state; STATE is required";
        return false;
    }
    PolicyRngStreamIdentity stream;
    stream.policy_artifact_id = artifact->policy_artifact_id;
    stream.participant_policy_assignment_id = assignment->participant_policy_assignment_id;
    stream.policy_rng_contract_identity = attribution.policy_rng_contract_identity;
    stream.policy_rng_stream_id = attribution.policy_rng_stream_id;
    stream.policy_rng_initialization_identity = attribution.policy_rng_initialization_identity;
    stream.policy_rng_identity = compute_policy_rng_stream_id(stream);
    if (stream.policy_rng_identity != attribution.policy_rng_identity) {
        error = "record RNG stream identity does not recompute from provenance";
        return false;
    }
    return true;
}

const RestrictedReplayEvidence* restricted_entry_for(
    const RestrictedCollectionEvidenceBundle& bundle,
    const std::string_view envelope_sha256) noexcept {
    const auto it = std::lower_bound(
        bundle.interrupted_episodes.begin(), bundle.interrupted_episodes.end(), envelope_sha256,
        [](const InterruptedEvidenceEntry& entry, const std::string_view key) {
            return entry.episode_envelope_sha256 < key;
        });
    return it == bundle.interrupted_episodes.end() || it->episode_envelope_sha256 != envelope_sha256
               ? nullptr
               : &it->evidence;
}

}  // namespace

ReplayResult replay_episode(const EpisodeEnvelope& envelope,
                            const std::optional<RestrictedReplayEvidence>& evidence,
                            const ReplayOptions& options) {
    ReplayResult result;
    std::string error;
    try {
        (void)canonical_episode_envelope_bytes(envelope);
        if (std::holds_alternative<FailedClosure>(envelope.closure)) {
            fail(result, "failed envelope has no normal replay admission");
            return result;
        }
        if (evidence.has_value() && evidence->episode_semantic_id !=
                                      envelope.manifest.episode_semantic_id) {
            fail(result, "restricted evidence episode does not match envelope");
            return result;
        }
        if (std::holds_alternative<TerminalClosure>(envelope.closure) && evidence.has_value()) {
            fail(result, "terminal replay received restricted interruption evidence");
            return result;
        }
        if (evidence.has_value()) {
            (void)canonical_restricted_replay_evidence_bytes(*evidence);
        }
        environment::CertifiedEnvironmentConfig config;
        environment::EpisodeSpec spec;
        if (!resolve_identity_inputs(envelope, config, spec, error)) {
            fail(result, std::move(error));
            return result;
        }
        const auto control = replay_control(envelope, evidence, options, error);
        if (!control.has_value()) {
            fail(result, std::move(error));
            return result;
        }
        auto factory = environment::EpisodicEnvironment::create(config);
        const auto* factory_rejection = std::get_if<environment::EnvironmentFactoryRejected>(&factory);
        if (factory_rejection != nullptr) {
            fail(result, "V2 environment factory rejected the certified environment");
            return result;
        }
        auto environment = std::move(*std::get_if<std::unique_ptr<environment::EpisodicEnvironment>>(
            &factory));
        if (environment == nullptr) {
            fail(result, "V2 environment factory returned no environment");
            return result;
        }
        const auto reset = environment->reset(spec, *control);
        const auto* reset_accepted = std::get_if<environment::ResetAccepted>(&reset);
        if (reset_accepted == nullptr) {
            fail(result, "V2 replay reset was rejected");
            return result;
        }
        std::variant<environment::DecisionFrame, environment::EpisodeTerminal,
                     environment::EpisodeInterrupted, environment::EpisodeFailure>
            boundary = reset_accepted->next;
        bool pending_administrative_interrupt = false;

        for (std::size_t index = 0; index < envelope.records.size(); ++index) {
            const auto* current_frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (current_frame == nullptr) {
                fail(result, "replay reached a non-frame boundary before all records");
                return result;
            }
            const auto& record = envelope.records[index];
            if (!equal_frame(record.frame, *current_frame, error)) {
                fail(result, std::move(error));
                return result;
            }
            const environment::EnvironmentActionCandidate* selected = nullptr;
            std::size_t selected_count = 0;
            for (const auto& candidate : current_frame->request.candidates) {
                if (candidate.public_action_key == record.selected_public_action_key) {
                    selected = &candidate;
                    ++selected_count;
                }
            }
            if (selected_count != 1 || selected == nullptr) {
                fail(result, "record selected key is not unique in regenerated V2 domain");
                return result;
            }
            if (record.transition_class !=
                declared_transition_class(*selected, current_frame->request)) {
                error = "record transition class differs from regenerated V2 semantics";
                fail(result, std::move(error));
                return result;
            }
            environment::ActionSelection selection;
            selection.contract_id = std::string(environment::kEpisodicEnvironmentV2ContractId);
            selection.episode_semantic_id = current_frame->episode_semantic_id;
            selection.public_semantic_decision_id = current_frame->public_semantic_decision_id;
            selection.submission_token = current_frame->submission_token;
            selection.public_action_key = record.selected_public_action_key;
            const auto stepped = environment->step(selection);
            const auto* step_accepted = std::get_if<environment::StepAccepted>(&stepped);
            if (step_accepted == nullptr ||
                step_accepted->transition.episode_semantic_id != current_frame->episode_semantic_id ||
                step_accepted->transition.public_semantic_decision_id !=
                    current_frame->public_semantic_decision_id ||
                step_accepted->transition.decision_index != current_frame->decision_index ||
                step_accepted->transition.selected_public_action_key !=
                    record.selected_public_action_key) {
                fail(result, "V2 rejected or changed the recorded public action");
                return result;
            }
            TransitionClass actual_class;
            if (!expected_transition_class(*selected, current_frame->request,
                                           step_accepted->transition.core_response_submitted,
                                           actual_class, error) || actual_class != record.transition_class) {
                fail(result, std::move(error));
                return result;
            }

            const auto* next_frame_target = record.successor.next_frame.has_value()
                                                ? &*record.successor.next_frame
                                                : nullptr;
            if (record.successor.kind == SuccessorKind::NextFrame) {
                const auto* next_frame = std::get_if<environment::DecisionFrame>(&step_accepted->next);
                if (next_frame == nullptr || next_frame_target == nullptr ||
                    next_frame_target->next_decision_index != next_frame->decision_index ||
                    next_frame_target->next_public_semantic_decision_id !=
                        next_frame->public_semantic_decision_id ||
                    next_frame_target->kind == NextFrameTargetKind::InterruptionPendingUnactedFrame) {
                    if (next_frame_target != nullptr &&
                        next_frame_target->kind == NextFrameTargetKind::InterruptionPendingUnactedFrame &&
                        index + 1 == envelope.records.size()) {
                        const auto* interrupted = std::get_if<InterruptedClosure>(&envelope.closure);
                        if (next_frame != nullptr && interrupted != nullptr &&
                            interrupted->pending_unacted_frame.has_value() &&
                            same_public_frame_as_snapshot(*interrupted->pending_unacted_frame,
                                                          step_accepted->next, error)) {
                            boundary = *next_frame;
                            pending_administrative_interrupt = true;
                            break;
                        }
                    }
                    fail(result, error.empty() ? "V2 successor frame differs" : std::move(error));
                    return result;
                }
                if (index + 1 == envelope.records.size()) {
                    fail(result, "final record unexpectedly points to a next decision frame");
                    return result;
                }
                boundary = *next_frame;
                continue;
            }
            if (index + 1 != envelope.records.size()) {
                fail(result, "non-final record has a closure successor");
                return result;
            }
            if (record.successor.kind == SuccessorKind::Terminal) {
                if (!std::holds_alternative<environment::EpisodeTerminal>(step_accepted->next) ||
                    !std::holds_alternative<TerminalClosure>(envelope.closure)) {
                    fail(result, "record terminal successor does not match V2");
                    return result;
                }
            } else if (record.successor.kind == SuccessorKind::Interrupted) {
                if (!std::holds_alternative<environment::EpisodeInterrupted>(step_accepted->next) ||
                    !std::holds_alternative<InterruptedClosure>(envelope.closure) ||
                    std::get<InterruptedClosure>(envelope.closure).pending_unacted_frame.has_value()) {
                    fail(result, "record interrupted successor does not match V2");
                    return result;
                }
            } else {
                fail(result, "failed successor cannot be admitted");
                return result;
            }
            boundary = step_accepted->next;
        }

        if (pending_administrative_interrupt) {
            const auto* pending_frame = std::get_if<environment::DecisionFrame>(&boundary);
            const auto* closure = std::get_if<InterruptedClosure>(&envelope.closure);
            if (pending_frame == nullptr || closure == nullptr ||
                !closure->pending_unacted_frame.has_value() ||
                !equal_frame(*closure->pending_unacted_frame, *pending_frame, error)) {
                fail(result, error.empty() ? "pending administrative frame differs" : std::move(error));
                return result;
            }
            const auto interrupted = environment->interrupt(environment::InterruptRequest{
                std::string(environment::kEpisodicEnvironmentV2ContractId),
                environment::InterruptionReason::AdministrativeCancel});
            const auto* accepted_interrupt = std::get_if<environment::InterruptAccepted>(&interrupted);
            if (accepted_interrupt == nullptr || !evidence.has_value() ||
                !compare_interrupted(envelope, *evidence, accepted_interrupt->interruption, true,
                                     result.final_engine_step_index, error)) {
                fail(result, error.empty() ? "V2 administrative interruption differs" : std::move(error));
                return result;
            }
            result.accepted = true;
            return result;
        }

        if (envelope.records.empty() && std::holds_alternative<InterruptedClosure>(envelope.closure) &&
            std::get<InterruptedClosure>(envelope.closure).pending_unacted_frame.has_value()) {
            const auto* pending_frame = std::get_if<environment::DecisionFrame>(&boundary);
            const auto& expected_pending =
                *std::get<InterruptedClosure>(envelope.closure).pending_unacted_frame;
            if (pending_frame == nullptr || !equal_frame(expected_pending, *pending_frame, error)) {
                fail(result, error.empty() ? "initial pending frame differs" : std::move(error));
                return result;
            }
            const auto interrupted = environment->interrupt(environment::InterruptRequest{
                std::string(environment::kEpisodicEnvironmentV2ContractId),
                environment::InterruptionReason::AdministrativeCancel});
            const auto* accepted_interrupt = std::get_if<environment::InterruptAccepted>(&interrupted);
            if (accepted_interrupt == nullptr || !evidence.has_value() ||
                !compare_interrupted(envelope, *evidence, accepted_interrupt->interruption, true,
                                     result.final_engine_step_index, error)) {
                fail(result, error.empty() ? "initial administrative interruption differs"
                                           : std::move(error));
                return result;
            }
            result.accepted = true;
            return result;
        }

        if (envelope.records.empty()) {
            if (const auto* interrupted = std::get_if<environment::EpisodeInterrupted>(&boundary)) {
                if (!evidence.has_value() ||
                    !compare_interrupted(envelope, *evidence, *interrupted, false,
                                         result.final_engine_step_index, error)) {
                    fail(result, error.empty() ? "initial interruption lacks matching evidence"
                                               : std::move(error));
                    return result;
                }
                result.accepted = true;
                return result;
            }
        }

        if (const auto* terminal = std::get_if<environment::EpisodeTerminal>(&boundary)) {
            if (!compare_terminal(envelope, *environment, *terminal, error)) {
                fail(result, std::move(error));
                return result;
            }
            result.accepted = true;
            return result;
        }
        if (const auto* interrupted = std::get_if<environment::EpisodeInterrupted>(&boundary)) {
            if (!evidence.has_value() ||
                !compare_interrupted(envelope, *evidence, *interrupted, false,
                                     result.final_engine_step_index, error)) {
                fail(result, error.empty() ? "interrupted replay lacks matching evidence"
                                           : std::move(error));
                return result;
            }
            result.accepted = true;
            return result;
        }
        fail(result, "replay ended in an unsupported V2 failure or actionable boundary");
        return result;
    } catch (const std::exception& exception) {
        fail(result, exception.what());
        return result;
    } catch (...) {
        fail(result, "semantic V2 replay threw");
        return result;
    }
}

std::optional<AdmissionVerification> verify_candidate_shard_for_admission(
    const CandidateTrajectoryShard& shard,
    const RestrictedCollectionEvidenceBundle& restricted_evidence,
    const std::string_view candidate_shard_artifact_sha256,
    const std::string_view restricted_evidence_artifact_sha256,
    const ReplayOptions& options,
    const ProvenanceResolver& resolver,
    std::string* error) {
    try {
        const auto computed_shard_sha256 = ygo::trajectory::candidate_shard_artifact_sha256(shard);
        const auto computed_evidence_sha256 =
            ygo::trajectory::restricted_collection_evidence_artifact_sha256(restricted_evidence);
        if (candidate_shard_artifact_sha256 != computed_shard_sha256 ||
            restricted_evidence_artifact_sha256 != computed_evidence_sha256) {
            if (error != nullptr) {
                *error = "admission artifact digest does not match canonical bytes";
            }
            return std::nullopt;
        }
        std::string validation_error;
        if (!validate_restricted_collection_evidence_bundle(
                restricted_evidence, shard, candidate_shard_artifact_sha256, &validation_error)) {
            if (error != nullptr) {
                *error = validation_error;
            }
            return std::nullopt;
        }
        std::vector<AdmissionEntryCommitment> entries;
        for (const auto& shard_entry : shard.entries) {
            const auto decoded = decode_episode_envelope(shard_entry.envelope_bytes);
            if (!decoded) {
                validation_error = "admission shard envelope failed strict decode";
                break;
            }
            const auto& envelope = *decoded.value;
            if (envelope.manifest.collection_disposition.kind != CollectionDispositionKind::Clean ||
                std::holds_alternative<FailedClosure>(envelope.closure)) {
                validation_error = "failed or quarantined episode is not learner eligible";
                break;
            }
            environment::CertifiedEnvironmentConfig config;
            environment::EpisodeSpec spec;
            if (!resolve_identity_inputs(envelope, config, spec, validation_error) ||
                !resolver.validate(envelope.manifest.policy_provenance, config, spec,
                                   &validation_error)) {
                break;
            }
            for (const auto& record : envelope.records) {
                if (!validate_record_attribution(envelope, record, validation_error)) {
                    break;
                }
            }
            if (!validation_error.empty()) {
                break;
            }
            std::optional<RestrictedReplayEvidence> evidence;
            if (std::holds_alternative<InterruptedClosure>(envelope.closure)) {
                const auto* entry = restricted_entry_for(
                    restricted_evidence, shard_entry.episode_envelope_sha256);
                if (entry == nullptr) {
                    validation_error = "interrupted admission entry lacks restricted evidence";
                    break;
                }
                evidence = *entry;
            }
            const auto replay = replay_episode(envelope, evidence, options);
            if (!replay) {
                validation_error = "semantic V2 replay failed: " + replay.error;
                break;
            }
            AdmissionEntryCommitment commitment;
            commitment.trajectory_record_id = trajectory_record_id(envelope);
            commitment.public_gameplay_trajectory_id = public_gameplay_trajectory_id(envelope);
            commitment.environment_semantic_id = envelope.manifest.environment_semantic_id;
            commitment.episode_semantic_id = envelope.manifest.episode_semantic_id;
            commitment.episode_envelope_sha256 = shard_entry.episode_envelope_sha256;
            commitment.closure_kind = std::holds_alternative<TerminalClosure>(envelope.closure) ? 0 : 1;
            entries.push_back(std::move(commitment));
        }
        if (validation_error.empty()) {
            std::sort(entries.begin(), entries.end(),
                      [](const auto& left, const auto& right) {
                          return left.trajectory_record_id < right.trajectory_record_id;
                      });
            for (std::size_t index = 1; index < entries.size(); ++index) {
                if (entries[index - 1].trajectory_record_id ==
                    entries[index].trajectory_record_id) {
                    validation_error = "admission produced duplicate trajectory record IDs";
                    break;
                }
            }
        }
        if (!validation_error.empty()) {
            if (error != nullptr) {
                *error = std::move(validation_error);
            }
            return std::nullopt;
        }
        return AdmissionVerification{std::string(candidate_shard_artifact_sha256),
                                     std::string(restricted_evidence_artifact_sha256),
                                     std::move(entries)};
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return std::nullopt;
    } catch (...) {
        if (error != nullptr) {
            *error = "whole-shard admission validation threw";
        }
        return std::nullopt;
    }
}

}  // namespace ygo::trajectory::admission
