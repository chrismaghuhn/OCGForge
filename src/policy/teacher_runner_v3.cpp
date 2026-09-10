#include "ygo/policy/teacher_runner_v3.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <string>
#include <utility>

#include "teacher_v2_internal.hpp"

namespace ygo::policy {

PolicySelection TeacherRunnerV3::failure(const PolicyErrorCode code,
                                         std::string message) noexcept {
    PolicySelection result;
    result.error = PolicyError{code, std::move(message)};
    return result;
}

TeacherRunnerV3CreateResult TeacherRunnerV3::create(
    TeacherRunnerV3Config config) noexcept {
    try {
        std::array<bool, 2> deck_role_seen = {false, false};
        std::array<std::string, 2> assignment_ids;
        std::array<std::string, 2> artifact_ids;
        for (std::uint8_t player = 0; player < 2; ++player) {
            if (!config.sessions[player].has_value()) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "V3 Teacher runner lacks a session for a player"}};
            }
            const auto& session = *config.sessions[player];
            std::string diagnostic;
            if (session.assignment.player != player ||
                session.policy.participant() != player ||
                session.assignment.participant_policy_assignment_id !=
                    session.policy.participant_policy_assignment_id() ||
                !detail::validate_teacher_policy_session_v2(
                    session.policy.profile(), session.policy.policy_binding(),
                    session.artifact, session.assignment, &diagnostic)) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    diagnostic.empty()
                                        ? "V3 Teacher session does not match its assignment"
                                        : diagnostic}};
            }
            const auto deck_role = static_cast<std::size_t>(session.assignment.deck_role);
            if (deck_role > 1 || deck_role_seen[deck_role] ||
                std::find(assignment_ids.begin(), assignment_ids.end(),
                          session.assignment.participant_policy_assignment_id) !=
                    assignment_ids.end() ||
                std::find(artifact_ids.begin(), artifact_ids.end(),
                          session.artifact.policy_artifact_id) != artifact_ids.end()) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "V3 Teacher runner requires two distinct deck roles, assignments, and artifacts"}};
            }
            deck_role_seen[deck_role] = true;
            assignment_ids[player] = session.assignment.participant_policy_assignment_id;
            artifact_ids[player] = session.artifact.policy_artifact_id;
        }
        if (!deck_role_seen[0] || !deck_role_seen[1]) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "V3 Teacher runner requires one session per locked deck role"}};
        }
        return {std::optional<TeacherRunnerV3>(
                    TeacherRunnerV3(std::move(config))),
                std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "V3 Teacher runner construction failed"}};
    }
}

PolicySelection TeacherRunnerV3::select(const environment::DecisionFrame& frame) noexcept {
    return select_impl(frame, nullptr);
}

PolicySelection TeacherRunnerV3::select_with_diagnostics(
    const environment::DecisionFrame& frame,
    teacher::TeacherRankingDiagnosticsV2& diagnostics) noexcept {
    return select_impl(frame, &diagnostics);
}

PolicySelection TeacherRunnerV3::select_impl(
    const environment::DecisionFrame& frame,
    teacher::TeacherRankingDiagnosticsV2* diagnostics) noexcept {
    if (pending_player_.has_value()) {
        return failure(PolicyErrorCode::LifecycleFailure,
                       "V3 Teacher runner has an unresolved pending proposal");
    }
    if (frame.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        frame.acting_player > 1 ||
        frame.public_observation.perspective_player != frame.acting_player) {
        return failure(PolicyErrorCode::InvalidConfiguration,
                       "V3 Teacher runner received an invalid V3 public frame");
    }
    auto& session = *config_.sessions[frame.acting_player];
    const auto selection = diagnostics == nullptr
                               ? session.policy.select(
                                     PolicyInput{frame.public_observation,
                                                 frame.request.candidates})
                               : session.policy.select_with_diagnostics(
                                     PolicyInput{frame.public_observation,
                                                 frame.request.candidates},
                                     *diagnostics);
    if (selection) {
        pending_player_ = frame.acting_player;
        pending_episode_semantic_id_ = frame.episode_semantic_id;
        pending_public_semantic_decision_id_ = frame.public_semantic_decision_id;
    }
    return selection;
}

TeacherRunnerV3ActionResult TeacherRunnerV3::select_action(
    const environment::DecisionFrame& frame) noexcept {
    if (!frame.submission_token.valid()) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "V3 Teacher runner received an invalid submission token"}};
    }
    const auto selection = select(frame);
    if (!selection) {
        return {std::nullopt, selection.error};
    }
    environment::ActionSelection action;
    action.contract_id = frame.contract_id;
    action.episode_semantic_id = frame.episode_semantic_id;
    action.public_semantic_decision_id = frame.public_semantic_decision_id;
    action.submission_token = frame.submission_token;
    action.public_action_key = selection.value->public_action_key;
    return {std::optional<environment::ActionSelection>(std::move(action)), std::nullopt};
}

bool TeacherRunnerV3::commit(const environment::StepAccepted& accepted) noexcept {
    if (!pending_player_.has_value()) {
        return false;
    }
    if (accepted.transition.episode_semantic_id != *pending_episode_semantic_id_ ||
        accepted.transition.public_semantic_decision_id !=
            *pending_public_semantic_decision_id_) {
        return false;
    }
    const auto player = *pending_player_;
    const bool committed = config_.sessions[player]->policy.commit(accepted.transition);
    pending_player_.reset();
    pending_episode_semantic_id_.reset();
    pending_public_semantic_decision_id_.reset();
    return committed;
}

bool TeacherRunnerV3::reject_pending_proposal() noexcept {
    if (!pending_player_.has_value()) {
        return false;
    }
    config_.sessions[*pending_player_]->policy.reject_pending_proposal();
    pending_player_.reset();
    pending_episode_semantic_id_.reset();
    pending_public_semantic_decision_id_.reset();
    return true;
}

}  // namespace ygo::policy
