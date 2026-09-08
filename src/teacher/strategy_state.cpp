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
    try {
        const bool has_index = state.last_accepted_decision_index.has_value();
        const bool has_key = state.last_accepted_public_action_key.has_value();
        return internal::valid_profile_id(state.strategy_profile_id) &&
               internal::valid_optional_id(state.active_goal_id) &&
               internal::valid_optional_id(state.active_line_id) &&
               internal::valid_sorted_id_vector(state.completed_line_node_ids) &&
               internal::valid_sorted_id_vector(state.achieved_goal_ids) &&
               internal::valid_public_fact_vector(state.public_resource_facts) &&
               internal::valid_public_fact_vector(state.public_restriction_facts) &&
               internal::valid_public_fact_vector(state.public_threat_facts) &&
               has_index == has_key &&
               (!has_key ||
                environment::is_public_action_key(*state.last_accepted_public_action_key));
    } catch (...) {
        return false;
    }
}

bool validate_teacher_state_delta(const TeacherStateDeltaV1& delta) noexcept {
    try {
        const bool has_index = delta.base_last_accepted_decision_index.has_value();
        const bool has_key = delta.base_last_accepted_public_action_key.has_value();
        return internal::valid_profile_id(delta.strategy_profile_id) &&
               has_index == has_key &&
               (!has_key ||
                environment::is_public_action_key(*delta.base_last_accepted_public_action_key)) &&
               environment::is_public_action_key(delta.proposed_for_public_action_key) &&
               internal::valid_optional_id(delta.active_goal_id) &&
               internal::valid_optional_id(delta.active_line_id) &&
               internal::valid_sorted_id_vector(delta.completed_line_node_ids) &&
               internal::valid_sorted_id_vector(delta.achieved_goal_ids) &&
               internal::valid_public_fact_vector(delta.public_resource_facts) &&
               internal::valid_public_fact_vector(delta.public_restriction_facts) &&
               internal::valid_public_fact_vector(delta.public_threat_facts) &&
               internal::valid_reason_vector(delta.invalidation_reason_ids);
    } catch (...) {
        return false;
    }
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
    try {
        if (!validate_strategy_profile(validated_profile) ||
            !valid_state_for_profile(current_state, validated_profile)) {
            return std::nullopt;
        }

        const auto reconciled = reconcile_strategy_state_with_evidence(
            current_state, owning_participant, current_observation);
        if (!reconciled.has_value() ||
            !valid_state_for_profile(reconciled->state, validated_profile) ||
            (current_state.last_accepted_decision_index.has_value() &&
             current_observation.decision_index <=
                 *current_state.last_accepted_decision_index)) {
            return std::nullopt;
        }

        const auto current_facts = extract_public_fact_snapshot(current_observation);
        if (!current_facts.valid) {
            return std::nullopt;
        }

        if (!validate_teacher_state_delta(requested_replacement) ||
            !requested_replacement.invalidation_reason_ids.empty() ||
            !valid_delta_for_profile(requested_replacement, validated_profile) ||
            !internal::delta_current_facts_match_snapshot(
                requested_replacement, current_facts.snapshot) ||
            requested_replacement.base_last_accepted_decision_index !=
                reconciled->state.last_accepted_decision_index ||
            requested_replacement.base_last_accepted_public_action_key !=
                reconciled->state.last_accepted_public_action_key) {
            return std::nullopt;
        }

        TeacherStateDeltaV1 result = requested_replacement;
        result.strategy_profile_id = validated_profile.profile_id;
        result.base_last_accepted_decision_index = current_state.last_accepted_decision_index;
        result.base_last_accepted_public_action_key =
            current_state.last_accepted_public_action_key;
        result.invalidation_reason_ids = reconciled->invalidation_reason_ids;
        return valid_delta_for_profile(result, validated_profile)
                   ? std::optional<TeacherStateDeltaV1>(result)
                   : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<StrategyReconciliationResult> commit_teacher_state_delta_with_evidence(
    EpisodeLocalStrategyStateV1& current_state,
    const TeacherRankingResult& ranking_result,
    const StrategyProfileV1& validated_profile,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    try {
        // The proposal observation is the only public frame used here. The
        // subsequent StepAccepted frame may belong to another participant.
        if (!valid_state_for_profile(current_state, validated_profile) ||
            !validate_strategy_profile(validated_profile) ||
            !validate_teacher_ranking_result(ranking_result) ||
            ranking_result.status != TeacherRankingStatus::Selected ||
            !ranking_result.selected_public_action_key.has_value() ||
            !ranking_result.proposed_state_delta.has_value() ||
            !environment::is_public_action_key(
                *ranking_result.selected_public_action_key) ||
            !environment::is_public_action_key(accepted_transition.selected_public_action_key) ||
            owning_participant > 1 ||
            proposal_observation.perspective_player != owning_participant ||
            proposal_observation.decision_index != accepted_transition.decision_index ||
            ranking_result.proposed_state_delta->strategy_profile_id !=
                current_state.strategy_profile_id ||
            !valid_delta_for_profile(*ranking_result.proposed_state_delta, validated_profile) ||
            ranking_result.proposed_state_delta->base_last_accepted_decision_index !=
                current_state.last_accepted_decision_index ||
            ranking_result.proposed_state_delta->base_last_accepted_public_action_key !=
                current_state.last_accepted_public_action_key ||
            ranking_result.proposed_state_delta->proposed_for_public_action_key !=
                *ranking_result.selected_public_action_key ||
            accepted_transition.selected_public_action_key !=
                *ranking_result.selected_public_action_key ||
            (current_state.last_accepted_decision_index.has_value() &&
             accepted_transition.decision_index <=
                 *current_state.last_accepted_decision_index)) {
            return std::nullopt;
        }

        const auto& delta = *ranking_result.proposed_state_delta;
        const auto proposal_facts = extract_public_fact_snapshot(proposal_observation);
        if (!proposal_facts.valid ||
            !internal::delta_current_facts_match_snapshot(delta, proposal_facts.snapshot) ||
            !valid_state_for_profile(current_state, validated_profile) ||
            (current_state.last_accepted_decision_index.has_value() &&
             proposal_observation.decision_index <=
                 *current_state.last_accepted_decision_index)) {
            return std::nullopt;
        }

        auto next_state = current_state;
        next_state.active_goal_id = delta.active_goal_id;
        next_state.active_line_id = delta.active_line_id;
        next_state.completed_line_node_ids = delta.completed_line_node_ids;
        next_state.achieved_goal_ids = delta.achieved_goal_ids;
        next_state.public_resource_facts = delta.public_resource_facts;
        next_state.public_restriction_facts = delta.public_restriction_facts;
        next_state.public_threat_facts = delta.public_threat_facts;
        next_state.last_accepted_decision_index = accepted_transition.decision_index;
        next_state.last_accepted_public_action_key =
            accepted_transition.selected_public_action_key;

        if (!valid_state_for_profile(next_state, validated_profile) ||
            !internal::valid_reason_vector(delta.invalidation_reason_ids)) {
            return std::nullopt;
        }
        StrategyReconciliationResult result;
        result.state = next_state;
        result.invalidation_reason_ids = delta.invalidation_reason_ids;
        current_state = std::move(next_state);
        return result;
    } catch (...) {
        return std::nullopt;
    }
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
    try {
        return rejection.authoritative_state_unchanged && validate_strategy_state(state);
    } catch (...) {
        return false;
    }
}

}  // namespace ygo::teacher
