#include "ygo/policy/teacher_runner_v4_trajectory.hpp"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <variant>

#include "runner_shared.hpp"
#include "teacher_v3_internal.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

namespace ygo::policy {
namespace {

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
    const std::optional<std::uint64_t> decision_limit) noexcept {
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
                return seal();
            }

            const auto* session = runner_.session(frame->acting_player);
            if (session == nullptr) {
                return failure("V4 trajectory runner lacks the acting V3 session");
            }
            const auto action = runner_.select_action(*frame);
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
    return run_impl(std::nullopt);
}

}  // namespace ygo::policy
