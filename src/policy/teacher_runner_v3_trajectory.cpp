#include "ygo/policy/teacher_runner_v3_trajectory.hpp"

#include <exception>
#include <string>
#include <utility>
#include <variant>

#include "runner_shared.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

namespace ygo::policy {
namespace {

using Boundary = std::variant<environment::DecisionFrame, environment::EpisodeTerminal,
                              environment::EpisodeInterrupted, environment::EpisodeFailure>;

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
            error = "V3 trajectory runner requires one assignment per player";
            return nullptr;
        }
        result = &assignment;
    }
    if (result == nullptr) {
        error = "V3 trajectory runner lacks an assignment for a player";
    }
    return result;
}

bool session_matches_assignment(const TeacherPolicySessionV2& session,
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
        error = "V3 trajectory session does not match policy provenance";
        return false;
    }
    return true;
}

trajectory::RestrictedReplayEvidenceV2 evidence_for_interruption(
    const environment::EpisodeInterrupted& interruption) {
    trajectory::RestrictedReplayEvidenceV2 result;
    result.episode_semantic_id = interruption.episode_semantic_id;
    result.interruption_reason = interruption.reason;
    result.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    result.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    result.observed_engine_process_count = interruption.run_control_evidence.engine_process_count;
    result.observed_semantic_action_count = interruption.run_control_evidence.semantic_action_count;
    result.final_engine_step_index = interruption.final_engine_step_index;
    return result;
}

TeacherRunnerV3TrajectoryRunResult failed_result(
    std::string message, std::optional<PolicyError> policy_error = std::nullopt) {
    TeacherRunnerV3TrajectoryRunResult result;
    result.error = std::move(policy_error);
    result.diagnostic = std::move(message);
    return result;
}

}  // namespace

TeacherRunnerV3TrajectoryCreateResult TeacherRunnerV3TrajectoryRunner::create(
    TeacherRunnerV3TrajectoryConfig config) noexcept {
    try {
        if (config.environment_config.contract_id !=
                environment::kEpisodicEnvironmentV3ContractId ||
            config.episode_spec.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
            !trajectory::is_current_certified_environment_v3(config.environment_config)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "V3 trajectory runner requires the current V3 environment"}};
        }
        const auto resolver = make_production_policy_provenance_resolver();
        std::string error;
        if (!resolver.validate(config.policy_provenance, config.environment_config,
                               config.episode_spec, &error)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration, std::move(error)}};
        }
        for (std::uint8_t player = 0; player < 2; ++player) {
            if (!config.runner_config.sessions[player].has_value()) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "V3 trajectory runner lacks a session for a player"}};
            }
            const auto* assignment = assignment_for_player(
                config.policy_provenance, player, error);
            if (assignment == nullptr ||
                !session_matches_assignment(*config.runner_config.sessions[player], *assignment,
                                            player, error)) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    error.empty() ? "V3 session/provenance mismatch" : error}};
            }
        }

        auto runner_result = TeacherRunnerV3::create(std::move(config.runner_config));
        if (!runner_result) {
            return {std::nullopt, runner_result.error};
        }
        auto factory = environment::EpisodicEnvironment::create(config.environment_config);
        auto* environment_value =
            std::get_if<std::unique_ptr<environment::EpisodicEnvironment>>(&factory);
        if (environment_value == nullptr || *environment_value == nullptr) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::LifecycleFailure,
                                "V3 environment factory rejected the environment"}};
        }
        auto recorder = std::make_unique<trajectory::TrajectoryRecorderV2>(
            config.environment_config, config.episode_spec, config.policy_provenance, resolver);
        return {std::optional<TeacherRunnerV3TrajectoryRunner>(
                    TeacherRunnerV3TrajectoryRunner(std::move(config),
                                                    std::move(*runner_result.value),
                                                    std::move(*environment_value),
                                                    std::move(recorder))),
                std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, exception.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "V3 trajectory runner construction threw"}};
    }
}

TeacherRunnerV3TrajectoryRunResult TeacherRunnerV3TrajectoryRunner::failure(
    std::string message, std::optional<PolicyError> policy_error) noexcept {
    return failed_result(std::move(message), std::move(policy_error));
}

TeacherRunnerV3TrajectoryRunResult TeacherRunnerV3TrajectoryRunner::run() noexcept {
    if (has_run_) {
        return failure("V3 trajectory runner can only execute one run");
    }
    has_run_ = true;
    try {
        const auto reset = environment_->reset(config_.episode_spec, config_.run_control);
        const auto* reset_accepted = std::get_if<environment::ResetAccepted>(&reset);
        if (reset_accepted == nullptr) {
            return failure("V3 reset was rejected");
        }
        Boundary boundary = reset_accepted->next;
        std::optional<trajectory::TerminalViews> terminal_views;
        if (std::holds_alternative<environment::EpisodeTerminal>(boundary)) {
            terminal_views = detail::terminal_views_for_environment(*environment_);
            if (!terminal_views.has_value()) {
                return failure("V3 terminal reset lacks both public terminal views");
            }
        }
        std::string recorder_error;
        if (!recorder_->on_reset_accepted(*reset_accepted, terminal_views, &recorder_error)) {
            return failure("V2 recorder rejected V3 reset: " + recorder_error);
        }
        if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
            TeacherRunnerV3TrajectoryRunResult result;
            result.envelope = recorder_->seal(&recorder_error);
            result.quarantined = recorder_->manifest().collection_disposition.kind !=
                                 trajectory::CollectionDispositionKind::Clean;
            if (!result.envelope.has_value()) {
                return failure("V2 recorder could not seal reset boundary: " + recorder_error);
            }
            if (const auto* interrupted =
                    std::get_if<environment::EpisodeInterrupted>(&boundary)) {
                result.replay_evidence = evidence_for_interruption(*interrupted);
            }
            return result;
        }

        for (;;) {
            const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (frame == nullptr || frame->contract_id !=
                                         environment::kEpisodicEnvironmentV3ContractId ||
                frame->acting_player > 1 ||
                frame->public_observation.perspective_player != frame->acting_player ||
                !frame->submission_token.valid()) {
                return failure("V3 trajectory runner reached an invalid public frame");
            }
            const auto selection = runner_.select(*frame);
            if (!selection) {
                return failure(
                    selection.error.has_value() ? selection.error->message
                                                : "V3 Teacher returned no selection",
                    selection.error);
            }
            if (selection.value->rng_cursor.has_value()) {
                return failure(
                    "V2 Teacher returned a policy RNG cursor",
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "V2 Teacher returned a policy RNG cursor"});
            }
            const auto* session = runner_.session(frame->acting_player);
            if (session == nullptr || selection.value->public_action_key.empty() ||
                !environment::is_public_action_key_v2(selection.value->public_action_key)) {
                return failure("V3 trajectory runner lacks the acting V2 session");
            }
            const auto action = environment::ActionSelection{
                std::string(environment::kEpisodicEnvironmentV3ContractId),
                frame->episode_semantic_id, frame->public_semantic_decision_id,
                frame->submission_token, selection.value->public_action_key};
            const auto pre_rejection_frame = *frame;
            const auto stepped = environment_->step(action);
            if (const auto* rejected = std::get_if<environment::StepRejected>(&stepped)) {
                if (!runner_.reject_pending_proposal() ||
                    !recorder_->on_step_rejected(*rejected, true, &recorder_error)) {
                    return failure("V2 StepRejected handling failed: " + recorder_error);
                }
                const auto interrupted = environment_->interrupt(environment::InterruptRequest{
                    std::string(environment::kEpisodicEnvironmentV3ContractId),
                    environment::InterruptionReason::AdministrativeCancel});
                const auto* accepted_interrupt =
                    std::get_if<environment::InterruptAccepted>(&interrupted);
                if (accepted_interrupt == nullptr ||
                    !recorder_->on_interrupt_accepted(
                        std::optional<environment::DecisionFrame>{pre_rejection_frame},
                        *accepted_interrupt, &recorder_error)) {
                    return failure("V2 StepRejected quarantine could not close: " + recorder_error);
                }
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                result.replay_evidence = evidence_for_interruption(
                    accepted_interrupt->interruption);
                result.quarantined = true;
                if (!result.envelope.has_value()) {
                    return failure("V2 recorder could not seal StepRejected closure: " +
                                   recorder_error);
                }
                return result;
            }

            const auto* accepted = std::get_if<environment::StepAccepted>(&stepped);
            if (accepted == nullptr || !runner_.commit(*accepted)) {
                return failure("V3 accepted step did not commit the pending V2 proposal");
            }
            terminal_views.reset();
            if (std::holds_alternative<environment::EpisodeTerminal>(accepted->next)) {
                terminal_views = detail::terminal_views_for_environment(*environment_);
                if (!terminal_views.has_value()) {
                    return failure("V3 terminal step lacks both public terminal views");
                }
            }
            const auto attribution = detail::make_policy_rng_attribution(
                *frame, session->execution_binding(), *selection.value);
            if (!recorder_->on_step_accepted(*accepted, attribution, terminal_views,
                                              &recorder_error)) {
                return failure("V2 recorder rejected accepted V3 step: " + recorder_error);
            }
            if (const auto* interrupted =
                    std::get_if<environment::EpisodeInterrupted>(&accepted->next)) {
                if (recorder_->lifecycle() != trajectory::RecorderLifecycle::Closed) {
                    return failure("V2 recorder did not close the interrupted boundary");
                }
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                result.replay_evidence = evidence_for_interruption(*interrupted);
                if (!result.envelope.has_value()) {
                    return failure("V2 recorder could not seal interruption: " + recorder_error);
                }
                return result;
            }
            if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                if (!result.envelope.has_value()) {
                    return failure("V2 recorder could not seal V3 closure: " + recorder_error);
                }
                return result;
            }
            boundary = accepted->next;
        }
    } catch (const std::exception& exception) {
        return failure(exception.what());
    } catch (...) {
        return failure("V3 trajectory runner execution threw");
    }
}

}  // namespace ygo::policy
