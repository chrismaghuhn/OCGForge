#include "ygo/trajectory/replay_v2.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

namespace ygo::trajectory::replay_v2 {
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
                         PublicFrameSnapshotV2& output,
                         std::string& error) {
    if (frame.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        !frame.submission_token.valid()) {
        error = "V2 replay received a non-V3 frame or invalid submission token";
        return false;
    }
    output.episodic_environment_contract_id = frame.contract_id;
    output.episode_semantic_id = frame.episode_semantic_id;
    output.public_semantic_decision_id = frame.public_semantic_decision_id;
    output.decision_index = frame.decision_index;
    output.acting_player = frame.acting_player;
    output.public_observation = frame.public_observation;
    output.public_observation_digest = frame.public_observation_digest;
    output.request = frame.request;
    output.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    try {
        (void)canonical_public_frame_snapshot_bytes_v2(output);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool equal_frame(const PublicFrameSnapshotV2& expected,
                 const environment::DecisionFrame& actual,
                 std::string& error) {
    PublicFrameSnapshotV2 actual_snapshot;
    if (!snapshot_from_frame(actual, actual_snapshot, error)) {
        return false;
    }
    try {
        if (canonical_public_frame_snapshot_bytes_v2(expected) !=
            canonical_public_frame_snapshot_bytes_v2(actual_snapshot)) {
            error = "regenerated V3 public frame differs from the V2 recorded frame";
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
            error = "regenerated V3 terminal observation differs from the V2 recorded view";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool resolve_identity_inputs(const EpisodeEnvelopeV2& envelope,
                             environment::CertifiedEnvironmentConfig& config,
                             environment::EpisodeSpec& spec,
                             std::string& error) {
    const auto decoded_config = decode_environment_identity_input_v3(
        envelope.manifest.environment_identity_input);
    if (!decoded_config || !is_current_certified_environment_v3(*decoded_config.value)) {
        error = "V2 replay environment identity is not the current V3 environment";
        return false;
    }
    config = *decoded_config.value;
    if (environment::canonical_environment_identity_bytes(config) !=
            envelope.manifest.environment_identity_input ||
        environment::environment_semantic_id(config) != envelope.manifest.environment_semantic_id) {
        error = "V2 replay environment identity bytes or digest differ from the manifest";
        return false;
    }
    const auto decoded_spec = decode_episode_identity_input_v3(
        envelope.manifest.episode_identity_input, config);
    if (!decoded_spec ||
        environment::canonical_episode_identity_bytes(config, *decoded_spec.value) !=
            envelope.manifest.episode_identity_input ||
        environment::episode_semantic_id(config, *decoded_spec.value) !=
            envelope.manifest.episode_semantic_id) {
        error = "V2 replay episode identity bytes or digest differ from the manifest";
        return false;
    }
    spec = *decoded_spec.value;
    return true;
}

std::optional<environment::RunControl> replay_control(
    const EpisodeEnvelopeV2& envelope,
    const std::optional<RestrictedReplayEvidenceV2>& evidence,
    const ReplayOptions& options,
    std::string& error) {
    if (std::holds_alternative<TerminalClosureV2>(envelope.closure)) {
        if (evidence.has_value()) {
            error = "V2 terminal replay received interruption evidence";
            return std::nullopt;
        }
        if (!options.terminal_run_control.has_value() ||
            !valid_control(*options.terminal_run_control)) {
            error = "V2 terminal replay requires an explicit valid run control";
            return std::nullopt;
        }
        return options.terminal_run_control;
    }

    if (!evidence.has_value() ||
        evidence->trusted_trajectory_contract_id != kTrustedTrajectoryV2ContractId ||
        evidence->episodic_environment_contract_id !=
            environment::kEpisodicEnvironmentV3ContractId ||
        evidence->episode_semantic_id != envelope.manifest.episode_semantic_id ||
        evidence->closure_kind != 1 || evidence->engine_process_budget == 0 ||
        evidence->semantic_action_budget == 0 || !options.cancellation_source.has_value() ||
        !safe_source(*options.cancellation_source)) {
        error = "V2 interrupted replay lacks exact restricted evidence";
        return std::nullopt;
    }
    environment::RunControl control;
    control.engine_process_budget = evidence->engine_process_budget;
    control.semantic_action_budget = evidence->semantic_action_budget;
    control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    control.cancellation.source = *options.cancellation_source;
    if (!valid_control(control)) {
        error = "V2 replay cancellation source is invalid";
        return std::nullopt;
    }
    return control;
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

bool expected_transition_class(const environment::EnvironmentActionCandidate& candidate,
                               const environment::EnvironmentDecisionRequest& request,
                               const bool core_response_submitted,
                               TransitionClass& output,
                               std::string& error) {
    if (!request.continuation.has_value()) {
        if (!candidate.submits_engine_response || !core_response_submitted) {
            error = "V2 atomic transition did not submit exactly one response";
            return false;
        }
        output = TransitionClass::AtomicEngineResponse;
        return true;
    }
    if (candidate.submits_engine_response) {
        if (!core_response_submitted) {
            error = "V2 final continuation did not submit a response";
            return false;
        }
        output = TransitionClass::FinalContinuationResponse;
        return true;
    }
    if (core_response_submitted) {
        error = "V2 intermediate continuation submitted a response";
        return false;
    }
    output = TransitionClass::IntermediateContinuation;
    return true;
}

bool compare_terminal(const EpisodeEnvelopeV2& envelope,
                      environment::EpisodicEnvironment& environment,
                      const environment::EpisodeTerminal& actual,
                      std::string& error) {
    const auto* expected = std::get_if<TerminalClosureV2>(&envelope.closure);
    if (expected == nullptr || actual.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        actual.episode_semantic_id != envelope.manifest.episode_semantic_id ||
        actual.winner != expected->winner || actual.win_reason != expected->win_reason ||
        actual.semantic_action_count != expected->semantic_action_count ||
        actual.last_decision_index != expected->last_decision_index ||
        actual.semantic_action_count != envelope.records.size()) {
        error = "V3 terminal boundary differs from the V2 recorded closure";
        return false;
    }
    const auto player_zero = environment.perspective_terminal_view(0);
    const auto player_one = environment.perspective_terminal_view(1);
    if (!player_zero.has_value() || !player_one.has_value() ||
        !equal_observation(expected->terminal_view_player_0, *player_zero, error) ||
        !equal_observation(expected->terminal_view_player_1, *player_one, error)) {
        if (error.empty()) {
            error = "V3 terminal boundary did not provide both public views";
        }
        return false;
    }
    if (environment::public_observation_digest(*player_zero) !=
            expected->terminal_view_player_0_digest ||
        environment::public_observation_digest(*player_one) !=
            expected->terminal_view_player_1_digest) {
        error = "V3 terminal observation digest differs from the V2 closure";
        return false;
    }
    return true;
}

bool compare_interrupted(const EpisodeEnvelopeV2& envelope,
                         const RestrictedReplayEvidenceV2& evidence,
                         const environment::EpisodeInterrupted& actual,
                         const bool administrative_pending,
                         std::uint64_t& final_engine_step,
                         std::string& error) {
    const auto* expected = std::get_if<InterruptedClosureV2>(&envelope.closure);
    if (expected == nullptr || actual.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        actual.episode_semantic_id != envelope.manifest.episode_semantic_id ||
        actual.reason != evidence.interruption_reason ||
        actual.run_control_evidence.engine_process_budget != evidence.engine_process_budget ||
        actual.run_control_evidence.semantic_action_budget != evidence.semantic_action_budget ||
        actual.run_control_evidence.engine_process_count != evidence.observed_engine_process_count ||
        actual.run_control_evidence.semantic_action_count != evidence.observed_semantic_action_count ||
        actual.semantic_action_count != expected->record_count ||
        actual.semantic_action_count != envelope.records.size() ||
        actual.final_engine_step_index != evidence.final_engine_step_index) {
        error = "V3 interruption boundary differs from V2 restricted evidence";
        return false;
    }
    if (administrative_pending) {
        if (!expected->pending_unacted_frame.has_value() ||
            actual.last_decision_index !=
                std::optional<std::uint64_t>{expected->pending_unacted_frame->decision_index} ||
            actual.last_public_semantic_decision_id !=
                std::optional<std::string>{expected->pending_unacted_frame->public_semantic_decision_id}) {
            error = "V2 administrative interruption does not identify its pending frame";
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
            error = "V2 interruption prefix differs from the recorded action prefix";
            return false;
        }
    }
    final_engine_step = actual.final_engine_step_index;
    return true;
}

bool same_public_frame_as_snapshot(
    const PublicFrameSnapshotV2& expected,
    const std::variant<environment::DecisionFrame, environment::EpisodeTerminal,
                       environment::EpisodeInterrupted, environment::EpisodeFailure>& boundary,
    std::string& error) {
    const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
    return frame != nullptr && equal_frame(expected, *frame, error);
}

}  // namespace

ReplayResult replay_episode_v2(
    const EpisodeEnvelopeV2& envelope,
    const std::optional<RestrictedReplayEvidenceV2>& evidence,
    const ReplayOptions& options) {
    ReplayResult result;
    std::string error;
    try {
        (void)canonical_episode_envelope_bytes_v2(envelope);
        if (std::holds_alternative<FailedClosureV2>(envelope.closure)) {
            fail(result, "failed V2 envelope has no normal replay admission");
            return result;
        }
        if (evidence.has_value()) {
            (void)canonical_restricted_replay_evidence_bytes_v2(*evidence);
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
        auto* environment_value =
            std::get_if<std::unique_ptr<environment::EpisodicEnvironment>>(&factory);
        if (environment_value == nullptr || *environment_value == nullptr) {
            fail(result, "V3 environment factory rejected the certified environment");
            return result;
        }
        auto environment = std::move(*environment_value);
        const auto reset = environment->reset(spec, *control);
        const auto* reset_accepted = std::get_if<environment::ResetAccepted>(&reset);
        if (reset_accepted == nullptr) {
            fail(result, "V3 replay reset was rejected");
            return result;
        }

        std::variant<environment::DecisionFrame, environment::EpisodeTerminal,
                     environment::EpisodeInterrupted, environment::EpisodeFailure>
            boundary = reset_accepted->next;
        bool pending_administrative_interrupt = false;

        for (std::size_t index = 0; index < envelope.records.size(); ++index) {
            const auto* current_frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (current_frame == nullptr) {
                fail(result, "V2 replay reached a non-frame boundary before all records");
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
            if (selected_count != 1 || selected == nullptr ||
                !environment::is_public_action_key_v2(record.selected_public_action_key)) {
                fail(result, "V2 selected action key is not unique in the regenerated V3 domain");
                return result;
            }
            if (record.transition_class != declared_transition_class(*selected, current_frame->request)) {
                fail(result, "V2 transition class differs from regenerated V3 semantics");
                return result;
            }

            environment::ActionSelection selection;
            selection.contract_id = std::string(environment::kEpisodicEnvironmentV3ContractId);
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
                fail(result, "V3 rejected or changed the recorded V2 public action");
                return result;
            }
            TransitionClass actual_class;
            if (!expected_transition_class(*selected, current_frame->request,
                                            step_accepted->transition.core_response_submitted,
                                            actual_class, error) ||
                actual_class != record.transition_class) {
                fail(result, std::move(error));
                return result;
            }

            const auto* next_frame_target = record.successor.next_frame.has_value()
                                                ? &*record.successor.next_frame
                                                : nullptr;
            if (record.successor.kind == SuccessorKind::NextFrame) {
                const auto* next_frame =
                    std::get_if<environment::DecisionFrame>(&step_accepted->next);
                if (next_frame == nullptr || next_frame_target == nullptr) {
                    fail(result, "V2 successor is not a regenerated V3 frame");
                    return result;
                }
                if (next_frame_target->kind == NextFrameTargetKind::InterruptionPendingUnactedFrame) {
                    if (index + 1 != envelope.records.size()) {
                        fail(result, "pending interruption target is not the final record");
                        return result;
                    }
                    const auto* interrupted =
                        std::get_if<InterruptedClosureV2>(&envelope.closure);
                    if (interrupted == nullptr || !interrupted->pending_unacted_frame.has_value() ||
                        next_frame_target->next_decision_index != next_frame->decision_index ||
                        next_frame_target->next_public_semantic_decision_id !=
                            next_frame->public_semantic_decision_id ||
                        !same_public_frame_as_snapshot(*interrupted->pending_unacted_frame,
                                                       step_accepted->next, error)) {
                        fail(result, error.empty() ? "pending V2 frame differs" : std::move(error));
                        return result;
                    }
                    boundary = *next_frame;
                    pending_administrative_interrupt = true;
                    break;
                }
                if (next_frame_target->next_decision_index != next_frame->decision_index ||
                    next_frame_target->next_public_semantic_decision_id !=
                        next_frame->public_semantic_decision_id ||
                    index + 1 == envelope.records.size()) {
                    fail(result, "V2 successor frame differs from the recorded sequence");
                    return result;
                }
                boundary = *next_frame;
                continue;
            }

            if (index + 1 != envelope.records.size()) {
                fail(result, "non-final V2 record has a closure successor");
                return result;
            }
            if (record.successor.kind == SuccessorKind::Terminal) {
                if (!std::holds_alternative<environment::EpisodeTerminal>(step_accepted->next) ||
                    !std::holds_alternative<TerminalClosureV2>(envelope.closure)) {
                    fail(result, "V2 terminal successor does not match V3");
                    return result;
                }
            } else if (record.successor.kind == SuccessorKind::Interrupted) {
                if (!std::holds_alternative<environment::EpisodeInterrupted>(step_accepted->next) ||
                    !std::holds_alternative<InterruptedClosureV2>(envelope.closure) ||
                    std::get<InterruptedClosureV2>(envelope.closure).pending_unacted_frame.has_value()) {
                    fail(result, "V2 interrupted successor does not match V3");
                    return result;
                }
            } else {
                fail(result, "V2 failed successor cannot be admitted");
                return result;
            }
            boundary = step_accepted->next;
        }

        if (pending_administrative_interrupt) {
            const auto* pending_frame = std::get_if<environment::DecisionFrame>(&boundary);
            const auto* closure = std::get_if<InterruptedClosureV2>(&envelope.closure);
            if (pending_frame == nullptr || closure == nullptr ||
                !closure->pending_unacted_frame.has_value() ||
                !equal_frame(*closure->pending_unacted_frame, *pending_frame, error)) {
                fail(result, error.empty() ? "pending V2 frame differs" : std::move(error));
                return result;
            }
            const auto interrupted = environment->interrupt(environment::InterruptRequest{
                std::string(environment::kEpisodicEnvironmentV3ContractId),
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

        if (envelope.records.empty() &&
            std::holds_alternative<InterruptedClosureV2>(envelope.closure) &&
            std::get<InterruptedClosureV2>(envelope.closure).pending_unacted_frame.has_value()) {
            const auto* pending_frame = std::get_if<environment::DecisionFrame>(&boundary);
            const auto& expected_pending =
                *std::get<InterruptedClosureV2>(envelope.closure).pending_unacted_frame;
            if (pending_frame == nullptr || !equal_frame(expected_pending, *pending_frame, error)) {
                fail(result, error.empty() ? "initial pending V2 frame differs" : std::move(error));
                return result;
            }
            const auto interrupted = environment->interrupt(environment::InterruptRequest{
                std::string(environment::kEpisodicEnvironmentV3ContractId),
                environment::InterruptionReason::AdministrativeCancel});
            const auto* accepted_interrupt = std::get_if<environment::InterruptAccepted>(&interrupted);
            if (accepted_interrupt == nullptr || !evidence.has_value() ||
                !compare_interrupted(envelope, *evidence, accepted_interrupt->interruption, true,
                                     result.final_engine_step_index, error)) {
                fail(result, error.empty() ? "initial V2 interruption differs" : std::move(error));
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
                    fail(result, error.empty() ? "initial V2 interruption lacks evidence"
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
            result.final_engine_step_index = terminal->final_engine_step_index;
            result.accepted = true;
            return result;
        }
        if (const auto* interrupted = std::get_if<environment::EpisodeInterrupted>(&boundary)) {
            if (!evidence.has_value() ||
                !compare_interrupted(envelope, *evidence, *interrupted, false,
                                     result.final_engine_step_index, error)) {
                fail(result, error.empty() ? "V2 interruption lacks evidence" : std::move(error));
                return result;
            }
            result.accepted = true;
            return result;
        }
        fail(result, "V2 replay ended in an unsupported failure boundary");
        return result;
    } catch (const std::exception& exception) {
        fail(result, exception.what());
        return result;
    } catch (...) {
        fail(result, "V2 replay threw");
        return result;
    }
}

}  // namespace ygo::trajectory::replay_v2
