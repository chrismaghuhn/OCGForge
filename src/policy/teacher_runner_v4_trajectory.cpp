#include "ygo/policy/teacher_runner_v4_trajectory.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "runner_shared.hpp"
#include "teacher_v3_internal.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

namespace ygo::policy {
namespace {

using DiagnosticClock = std::chrono::steady_clock;

std::uint64_t diagnostic_elapsed_us(const DiagnosticClock::time_point start,
                                    const DiagnosticClock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

void add_public_frame_diagnostics_v4(
    diagnostics::Task7DiagnosticEvent& event,
    const environment::DecisionFrame& frame,
    const std::string_view selected_public_action_key) {
    event.decision_index = frame.decision_index;
    event.engine_step_index = frame.engine_step_index;
    event.engine_process_count =
        frame.engine_step_index == (std::numeric_limits<std::uint64_t>::max)()
            ? frame.engine_step_index
            : frame.engine_step_index + 1;
    event.semantic_action_count =
        frame.decision_index == (std::numeric_limits<std::uint64_t>::max)()
            ? frame.decision_index
            : frame.decision_index + 1;
    event.acting_player = frame.acting_player;
    event.candidate_count = frame.request.candidates.size();
    event.request_kind = std::string(
        environment::environment_decision_kind_name(frame.request.kind));
    event.decision_family = event.request_kind;
    event.public_observation_digest = frame.public_observation_digest;
    event.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    event.public_semantic_decision_id = frame.public_semantic_decision_id;
    event.selected_public_action_key = std::string(selected_public_action_key);
    if (frame.request.continuation.has_value()) {
        event.continuation_present = true;
        event.continuation_kind = frame.request.continuation->continuation_kind;
        event.continuation_step = frame.request.continuation->continuation_step;
        event.continuation_selected_count =
            frame.request.continuation->selected_indices.size();
        event.continuation_remaining_count =
            frame.request.continuation->remaining_indices.size();
        event.continuation_min_count = frame.request.continuation->min_count;
        event.continuation_max_count = frame.request.continuation->max_count;
        event.continuation_can_finish = frame.request.continuation->can_finish;
        event.continuation_can_cancel = frame.request.continuation->can_cancel;
    }

    const auto safe = environment::decode_canonical_public_safe_state(
        frame.public_observation.canonical_safe_state_bytes());
    if (!safe) return;
    event.public_current_state_fingerprint = trace::sha256_bytes(
        environment::diagnostic_public_current_state_bytes(*safe.value));
    const auto& globals = safe.value->globals();
    if (globals.turn_count.has_value()) {
        event.public_turn_count_present = true;
        event.public_turn_count = *globals.turn_count;
    }
    if (globals.turn_player.has_value()) event.public_turn_player = *globals.turn_player;
    if (globals.phase.has_value()) event.public_phase = std::to_string(*globals.phase);
    if (globals.life_points.size() >= 2) {
        event.public_life_points_present = true;
        event.public_life_points_p0 = globals.life_points[0];
        event.public_life_points_p1 = globals.life_points[1];
    }
    event.public_entity_count = safe.value->entities().size();
    event.public_visible_event_count = safe.value->visible_events().size();
    event.public_chain_length = safe.value->chain().length;
}

const trajectory::ParticipantPolicyAssignment* assignment_for_player(
    const trajectory::PolicyProvenanceEnvelope& provenance,
    const std::uint8_t player,
    std::string& error) {
    const auto found = std::find_if(
        provenance.participant_assignments.begin(),
        provenance.participant_assignments.end(),
        [player](const auto& value) { return value.player == player; });
    if (found == provenance.participant_assignments.end()) {
        error = "V4 trajectory provenance lacks an assignment for the player";
        return nullptr;
    }
    return &*found;
}

bool session_matches_assignment(
    const TeacherPolicySessionV3& session,
    const trajectory::ParticipantPolicyAssignment& assignment,
    const std::uint8_t player,
    std::string& error) {
    if (session.assignment.player != player ||
        session.assignment.participant_policy_assignment_id !=
            assignment.participant_policy_assignment_id ||
        session.assignment.policy_artifact_id != assignment.policy_artifact_id ||
        session.artifact.policy_artifact_id != assignment.policy_artifact_id ||
        session.policy.participant() != player ||
        session.policy.participant_policy_assignment_id() !=
            assignment.participant_policy_assignment_id) {
        error = "V4 trajectory session does not match policy provenance";
        return false;
    }
    return true;
}

TeacherRunnerV4TrajectoryRunResult failed_result(std::string message) {
    TeacherRunnerV4TrajectoryRunResult result;
    result.diagnostic = std::move(message);
    return result;
}

}  // namespace

TeacherRunnerV4TrajectoryCreateResult TeacherRunnerV4TrajectoryRunner::create(
    TeacherRunnerV4TrajectoryConfig config) noexcept {
    try {
        if (config.environment_config.contract_id !=
                environment::kEpisodicEnvironmentV4ContractId ||
            config.episode_spec.contract_id !=
                environment::kEpisodicEnvironmentV4ContractId ||
            !trajectory::is_current_certified_environment_v4(
                config.environment_config)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "V4 trajectory runner requires the current V4 environment"}};
        }

        const auto resolver = make_production_policy_provenance_resolver();
        std::string error;
        if (!resolver.validate(config.policy_provenance,
                               config.environment_config,
                               config.episode_spec, &error)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                std::move(error)}};
        }
        for (std::uint8_t player = 0; player < 2; ++player) {
            if (!config.runner_config.sessions[player].has_value()) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "V4 trajectory runner lacks a session for a player"}};
            }
            const auto* assignment = assignment_for_player(
                config.policy_provenance, player, error);
            if (assignment == nullptr ||
                !session_matches_assignment(
                    *config.runner_config.sessions[player], *assignment, player, error) ||
                !detail::validate_teacher_policy_session_v3(
                    config.runner_config.sessions[player]->policy.profile(),
                    config.runner_config.sessions[player]->policy.policy_binding(),
                    config.runner_config.sessions[player]->artifact,
                    config.runner_config.sessions[player]->assignment, &error)) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    error.empty() ? "V4 session/provenance mismatch"
                                                   : error}};
            }
        }

        auto runner_result = TeacherRunnerV4::create(
            std::move(config.runner_config));
        if (!runner_result) {
            return {std::nullopt, runner_result.error};
        }
        auto factory = environment::EpisodicEnvironment::create(
            config.environment_config);
        auto* environment_value =
            std::get_if<std::unique_ptr<environment::EpisodicEnvironment>>(&factory);
        if (environment_value == nullptr || *environment_value == nullptr) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::LifecycleFailure,
                                "V4 environment factory rejected the environment"}};
        }
        (*environment_value)->set_diagnostic_observer(config.diagnostic_observer);
        auto recorder = std::make_unique<trajectory::TrajectoryRecorderV3>(
            config.environment_config, config.episode_spec,
            config.policy_provenance, resolver);
        return {std::optional<TeacherRunnerV4TrajectoryRunner>(
                    TeacherRunnerV4TrajectoryRunner(
                        std::move(config), std::move(*runner_result.value),
                        std::move(*environment_value), std::move(recorder))),
                std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            exception.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "V4 trajectory runner construction threw"}};
    }
}

TeacherRunnerV4TrajectoryRunResult TeacherRunnerV4TrajectoryRunner::failure(
    std::string message, std::optional<PolicyError> policy_error) noexcept {
    auto result = failed_result(std::move(message));
    result.error = std::move(policy_error);
    return result;
}

TeacherRunnerV4TrajectoryRunResult TeacherRunnerV4TrajectoryRunner::run_impl(
    const std::optional<std::uint64_t> decision_limit,
    std::optional<trajectory::RestrictedReplayEvidenceV3>*
        restricted_replay_evidence) noexcept {
    if (has_run_) {
        return failure("V4 trajectory runner can only execute one run");
    }
    has_run_ = true;
    try {
        const auto reset = environment_->reset(
            config_.episode_spec, config_.run_control);
        const auto* reset_accepted = std::get_if<environment::ResetAccepted>(&reset);
        if (reset_accepted == nullptr) {
            return failure("V4 reset was rejected");
        }
        auto boundary = reset_accepted->next;
        std::string recorder_error;
        std::optional<trajectory::TerminalViews> terminal_views;
        if (std::holds_alternative<environment::EpisodeTerminal>(boundary)) {
            terminal_views = detail::terminal_views_for_environment(*environment_);
            if (!terminal_views.has_value()) {
                return failure("V4 terminal reset lacks both public terminal views");
            }
        }
        if (!recorder_->on_reset_accepted(*reset_accepted, terminal_views,
                                           &recorder_error)) {
            return failure("V3 recorder rejected V4 reset: " + recorder_error);
        }

        const auto seal = [&](const bool quarantined = false) {
            TeacherRunnerV4TrajectoryRunResult result;
            result.envelope = recorder_->seal(&recorder_error);
            result.quarantined = quarantined ||
                recorder_->manifest().collection_disposition.kind !=
                    trajectory::CollectionDispositionKind::Clean;
            if (!result.envelope.has_value()) {
                return failure("V3 recorder could not seal envelope: " + recorder_error);
            }
            return result;
        };

        if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
            return seal();
        }

        for (;;) {
            const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (frame == nullptr ||
                frame->contract_id != environment::kEpisodicEnvironmentV4ContractId ||
                frame->acting_player > 1 ||
                frame->public_observation.perspective_player != frame->acting_player ||
                !frame->submission_token.valid()) {
                return failure("V4 trajectory runner reached an invalid public frame");
            }

            if (decision_limit.has_value() &&
                frame->decision_index >= *decision_limit) {
                const auto interrupted = environment_->interrupt(
                    environment::InterruptRequest{
                        std::string(environment::kEpisodicEnvironmentV4ContractId),
                        environment::InterruptionReason::AdministrativeCancel});
                const auto* accepted_interrupt =
                    std::get_if<environment::InterruptAccepted>(&interrupted);
                if (accepted_interrupt == nullptr ||
                    !recorder_->on_interrupt_accepted(
                        std::optional<environment::DecisionFrame>{*frame},
                        *accepted_interrupt, &recorder_error)) {
                    return failure("V3 diagnostic prefix could not close: " +
                                   recorder_error);
                }
                if (restricted_replay_evidence != nullptr) {
                    const auto& interruption = accepted_interrupt->interruption;
                    trajectory::RestrictedReplayEvidenceV3 evidence;
                    evidence.episode_semantic_id = frame->episode_semantic_id;
                    evidence.interruption_reason = interruption.reason;
                    evidence.engine_process_budget =
                        interruption.run_control_evidence.engine_process_budget;
                    evidence.semantic_action_budget =
                        interruption.run_control_evidence.semantic_action_budget;
                    evidence.observed_engine_process_count =
                        interruption.run_control_evidence.engine_process_count;
                    evidence.observed_semantic_action_count =
                        interruption.run_control_evidence.semantic_action_count;
                    evidence.final_engine_step_index =
                        interruption.final_engine_step_index;
                    *restricted_replay_evidence = std::move(evidence);
                }
                return seal();
            }

            const auto* session = runner_.session(frame->acting_player);
            if (session == nullptr) {
                return failure("V4 trajectory runner lacks the acting V3 session");
            }
            const auto teacher_start = DiagnosticClock::now();
            const auto action = runner_.select_action(*frame);
            const auto teacher_elapsed = diagnostic_elapsed_us(
                teacher_start, DiagnosticClock::now());
            if (!action || !action.value.has_value()) {
                return failure(
                    action.error.has_value() ? action.error->message
                                              : "V4 Teacher returned no selection",
                    action.error);
            }
            const auto selection = action.value->public_action_key;
            const auto pre_rejection_frame = *frame;
            const auto stepped = environment_->step(*action.value);
            if (const auto* rejected = std::get_if<environment::StepRejected>(&stepped)) {
                if (!runner_.reject_pending_proposal() ||
                    !recorder_->on_step_rejected(*rejected, true, &recorder_error)) {
                    return failure("V3 StepRejected handling failed: " + recorder_error);
                }
                const auto interrupted = environment_->interrupt(
                    environment::InterruptRequest{
                        std::string(environment::kEpisodicEnvironmentV4ContractId),
                        environment::InterruptionReason::AdministrativeCancel});
                const auto* accepted_interrupt =
                    std::get_if<environment::InterruptAccepted>(&interrupted);
                if (accepted_interrupt == nullptr ||
                    !recorder_->on_interrupt_accepted(
                        std::optional<environment::DecisionFrame>{pre_rejection_frame},
                        *accepted_interrupt, &recorder_error)) {
                    return failure("V3 StepRejected quarantine could not close: " +
                                   recorder_error);
                }
                return seal(true);
            }

            const auto* accepted = std::get_if<environment::StepAccepted>(&stepped);
            if (accepted == nullptr || !runner_.commit(*accepted)) {
                return failure("V4 accepted step did not commit the pending proposal");
            }
            terminal_views.reset();
            if (std::holds_alternative<environment::EpisodeTerminal>(accepted->next)) {
                terminal_views = detail::terminal_views_for_environment(*environment_);
                if (!terminal_views.has_value()) {
                    return failure("V4 terminal step lacks both public terminal views");
                }
            }
            const auto attribution = detail::make_policy_rng_attribution(
                *frame, session->execution_binding(),
                PolicySelectionResult{selection, std::nullopt});
            if (!recorder_->on_step_accepted(
                    *accepted, attribution, terminal_views, &recorder_error)) {
                return failure("V3 recorder rejected accepted V4 step: " +
                               recorder_error);
            }
            if (config_.diagnostic_observer) {
                try {
                    diagnostics::Task7DiagnosticEvent event;
                    event.phase = "TEACHER";
                    event.duration_us = teacher_elapsed;
                    add_public_frame_diagnostics_v4(event, *frame, selection);
                    config_.diagnostic_observer(event);
                } catch (...) {
                    // Diagnostics are strictly non-authoritative.
                }
            }
            if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
                return seal();
            }
            boundary = accepted->next;
        }
    } catch (const std::exception& exception) {
        return failure(exception.what());
    } catch (...) {
        return failure("V4 trajectory runner execution threw");
    }
}

TeacherRunnerV4TrajectoryRunResult TeacherRunnerV4TrajectoryRunner::run() noexcept {
    return run_impl(std::nullopt, nullptr);
}

}  // namespace ygo::policy
