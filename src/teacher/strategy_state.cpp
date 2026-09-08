#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_decision.hpp"

#include <cstddef>
#include <exception>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trajectory/codec.hpp"
#include "strategy_state_common.hpp"
#include "teacher_validation.hpp"

namespace ygo::teacher {
namespace {

bool valid_state_for_profile(const EpisodeLocalStrategyStateV1& state,
                             const StrategyProfileV1& profile) noexcept {
    return validate_strategy_state(state) && internal::valid_plan_references(state, profile);
}

bool valid_delta_for_profile(const TeacherStateDeltaV1& delta,
                             const StrategyProfileV1& profile) noexcept {
    return validate_teacher_state_delta(delta) && internal::valid_plan_references(delta, profile);
}

}  // namespace

bool validate_strategy_state(const EpisodeLocalStrategyStateV1& state) noexcept {
    return internal::validate_strategy_state_common(
        state, [](const auto& key) noexcept { return environment::is_public_action_key(key); });
}

bool validate_teacher_state_delta(const TeacherStateDeltaV1& delta) noexcept {
    return internal::validate_teacher_state_delta_common(
        delta, [](const auto& key) noexcept { return environment::is_public_action_key(key); });
}

std::optional<EpisodeLocalStrategyStateV1> reset_strategy_state(
    const StrategyProfileV1& validated_profile) noexcept {
    try {
        if (!validate_strategy_profile(validated_profile) ||
            !internal::valid_profile_id(validated_profile.profile_id)) {
            return std::nullopt;
        }
        EpisodeLocalStrategyStateV1 state;
        state.strategy_profile_id = validated_profile.profile_id;
        if (!valid_state_for_profile(state, validated_profile)) {
            return std::nullopt;
        }
        return state;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<TeacherStateDeltaV1> propose_teacher_state_delta(
    const EpisodeLocalStrategyStateV1& current_state,
    const environment::PublicEnvironmentObservation& current_observation,
    const std::uint8_t owning_participant,
    const StrategyProfileV1& validated_profile,
    const TeacherStateDeltaV1& requested_replacement) noexcept {
    return internal::propose_teacher_state_delta_impl<
        EpisodeLocalStrategyStateV1, TeacherStateDeltaV1, StrategyReconciliationResult>(
        current_state, current_observation, owning_participant, validated_profile,
        requested_replacement,
        [](const auto& state, const auto& profile) noexcept {
            return valid_state_for_profile(state, profile);
        },
        [](const auto& state, const auto participant, const auto& observation) noexcept {
            return reconcile_strategy_state_with_evidence(state, participant, observation);
        },
        [](const auto& delta) noexcept { return validate_teacher_state_delta(delta); },
        [](const auto& delta, const auto& profile) noexcept {
            return valid_delta_for_profile(delta, profile);
        });
}

std::optional<StrategyReconciliationResult> commit_teacher_state_delta_with_evidence(
    EpisodeLocalStrategyStateV1& current_state,
    const TeacherRankingResult& ranking_result,
    const StrategyProfileV1& validated_profile,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    // The proposal observation is the only public frame used here. The
    // subsequent StepAccepted frame may belong to another participant.
    return internal::commit_teacher_state_delta_with_evidence_impl<
        EpisodeLocalStrategyStateV1, TeacherRankingResult, TeacherStateDeltaV1,
        StrategyReconciliationResult>(
        current_state, ranking_result, validated_profile, owning_participant,
        proposal_observation, accepted_transition,
        [](const auto& state, const auto& profile) noexcept {
            return valid_state_for_profile(state, profile);
        },
        [](const auto& result) noexcept { return validate_teacher_ranking_result(result); },
        [](const auto& delta, const auto& profile) noexcept {
            return valid_delta_for_profile(delta, profile);
        },
        [](const auto& key) noexcept { return environment::is_public_action_key(key); });
}

bool commit_teacher_state_delta(
    EpisodeLocalStrategyStateV1& current_state,
    const TeacherRankingResult& ranking_result,
    const StrategyProfileV1& validated_profile,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    return commit_teacher_state_delta_with_evidence(
               current_state, ranking_result, validated_profile, owning_participant,
               proposal_observation, accepted_transition)
        .has_value();
}

bool observe_step_rejected(EpisodeLocalStrategyStateV1& state,
                           const environment::StepRejected& rejection) noexcept {
    return internal::observe_step_rejected_impl(state, rejection, validate_strategy_state);
}

}  // namespace ygo::teacher
