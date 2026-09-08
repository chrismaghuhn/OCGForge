#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/trajectory/codec.hpp"
#include "teacher_validation.hpp"

namespace ygo::teacher::internal {

inline bool valid_profile_id(const std::string_view value) noexcept {
    return trajectory::is_canonical_identity(value, kStrategyProfileIdentityPrefix);
}

inline bool valid_optional_id(const std::optional<std::string>& value) noexcept {
    return !value.has_value() || detail::canonical_token(*value);
}

inline bool valid_sorted_id_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!detail::canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

inline bool valid_public_fact_vector(const std::vector<PublicFactValue>& values) {
    const auto& registry = PublicFactRegistry::canonical();
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!registry.validate(values[index])) {
            return false;
        }
        if (index > 0 &&
            (values[index - 1].fact_id == values[index].fact_id ||
             !(canonical_public_fact_value_bytes(values[index - 1]) <
               canonical_public_fact_value_bytes(values[index])))) {
            return false;
        }
    }
    return true;
}

inline bool current_facts_match_snapshot(const std::vector<PublicFactValue>& values,
                                          const PublicFactSnapshot& snapshot) {
    for (const auto& fact : values) {
        if (fact.validity_scope != PublicFactValidityScope::CurrentReconciliation) {
            continue;
        }
        const auto current = snapshot.value(fact.fact_id);
        if (!current.has_value() || *current != fact) {
            return false;
        }
    }
    return true;
}

template <typename Delta>
bool delta_current_facts_match_snapshot(const Delta& delta,
                                        const PublicFactSnapshot& snapshot) {
    return current_facts_match_snapshot(delta.public_resource_facts, snapshot) &&
           current_facts_match_snapshot(delta.public_restriction_facts, snapshot) &&
           current_facts_match_snapshot(delta.public_threat_facts, snapshot);
}

inline bool valid_reason_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!detail::canonical_token(values[index]) ||
            !is_registered_invalidation_reason(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

template <typename Value>
bool valid_plan_references(const Value& value,
                           const StrategyProfileV1& profile) noexcept {
    if (value.strategy_profile_id != profile.profile_id) {
        return false;
    }

    const auto goal_exists = [&profile](const std::string& id) noexcept {
        return std::any_of(profile.goals.begin(), profile.goals.end(),
                           [&id](const auto& goal) { return goal.goal_id == id; });
    };
    for (const auto& goal_id : value.achieved_goal_ids) {
        if (!goal_exists(goal_id)) {
            return false;
        }
    }

    const LineDefinition* active_line = nullptr;
    if (value.active_goal_id.has_value() && !goal_exists(*value.active_goal_id)) {
        return false;
    }
    if (value.active_line_id.has_value()) {
        const auto line = std::find_if(
            profile.lines.begin(), profile.lines.end(), [&value](const auto& candidate) {
                return candidate.line_id == *value.active_line_id;
            });
        if (line == profile.lines.end() || !value.active_goal_id.has_value() ||
            line->goal_id != *value.active_goal_id) {
            return false;
        }
        active_line = &*line;
    } else if (!value.completed_line_node_ids.empty()) {
        return false;
    }

    if (active_line != nullptr) {
        for (const auto& node_id : value.completed_line_node_ids) {
            const auto node = std::find_if(
                active_line->nodes.begin(), active_line->nodes.end(),
                [&node_id](const auto& candidate) { return candidate.node_id == node_id; });
            if (node == active_line->nodes.end()) {
                return false;
            }
        }
    }
    return true;
}

template <typename State, typename StateValidator>
bool valid_state_for_profile(const State& state,
                             const StrategyProfileV1& profile,
                             StateValidator state_validator) noexcept {
    return state_validator(state) && valid_plan_references(state, profile);
}

template <typename State, typename ActionKeyValidator>
bool validate_strategy_state_common(const State& state,
                                    ActionKeyValidator action_key_validator) noexcept {
    try {
        const bool has_index = state.last_accepted_decision_index.has_value();
        const bool has_key = state.last_accepted_public_action_key.has_value();
        return valid_profile_id(state.strategy_profile_id) &&
               valid_optional_id(state.active_goal_id) &&
               valid_optional_id(state.active_line_id) &&
               valid_sorted_id_vector(state.completed_line_node_ids) &&
               valid_sorted_id_vector(state.achieved_goal_ids) &&
               valid_public_fact_vector(state.public_resource_facts) &&
               valid_public_fact_vector(state.public_restriction_facts) &&
               valid_public_fact_vector(state.public_threat_facts) &&
               has_index == has_key &&
               (!has_key || action_key_validator(*state.last_accepted_public_action_key));
    } catch (...) {
        return false;
    }
}

template <typename Delta, typename ActionKeyValidator>
bool validate_teacher_state_delta_common(const Delta& delta,
                                         ActionKeyValidator action_key_validator) noexcept {
    try {
        const bool has_index = delta.base_last_accepted_decision_index.has_value();
        const bool has_key = delta.base_last_accepted_public_action_key.has_value();
        return valid_profile_id(delta.strategy_profile_id) &&
               has_index == has_key &&
               (!has_key || action_key_validator(*delta.base_last_accepted_public_action_key)) &&
               action_key_validator(delta.proposed_for_public_action_key) &&
               valid_optional_id(delta.active_goal_id) &&
               valid_optional_id(delta.active_line_id) &&
               valid_sorted_id_vector(delta.completed_line_node_ids) &&
               valid_sorted_id_vector(delta.achieved_goal_ids) &&
               valid_public_fact_vector(delta.public_resource_facts) &&
               valid_public_fact_vector(delta.public_restriction_facts) &&
               valid_public_fact_vector(delta.public_threat_facts) &&
               valid_reason_vector(delta.invalidation_reason_ids);
    } catch (...) {
        return false;
    }
}

inline bool observation_belongs_to_participant(
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant) noexcept {
    return owning_participant <= 1 &&
           observation.perspective_player == owning_participant;
}

inline void retain_current_facts(std::vector<PublicFactValue>& facts,
                                 const PublicFactSnapshot& snapshot,
                                 bool& stale) {
    std::vector<PublicFactValue> retained;
    retained.reserve(facts.size());
    for (const auto& fact : facts) {
        if (fact.validity_scope == PublicFactValidityScope::AcceptedPublicHistory) {
            retained.push_back(fact);
            continue;
        }
        const auto current = snapshot.value(fact.fact_id);
        if (current.has_value() && *current == fact) {
            retained.push_back(fact);
        } else {
            stale = true;
        }
    }
    facts = std::move(retained);
}

template <typename State>
bool reconcile_in_place(
    State& state,
    const environment::PublicEnvironmentObservation& observation,
    std::vector<std::string>& invalidation_reason_ids) {
    const auto extracted = extract_public_fact_snapshot(observation);
    if (!extracted.valid) {
        return false;
    }

    bool stale = false;
    retain_current_facts(state.public_resource_facts, extracted.snapshot, stale);
    retain_current_facts(state.public_restriction_facts, extracted.snapshot, stale);
    retain_current_facts(state.public_threat_facts, extracted.snapshot, stale);
    if (stale) {
        state.active_goal_id.reset();
        state.active_line_id.reset();
        state.completed_line_node_ids.clear();
        state.achieved_goal_ids.clear();
        invalidation_reason_ids.emplace_back("public_state_contradiction");
    }
    return valid_reason_vector(invalidation_reason_ids);
}

template <typename State, typename ReconciliationResult, typename StateValidator>
std::optional<ReconciliationResult> reconcile_strategy_state_with_evidence_impl(
    const State& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation,
    StateValidator state_validator) noexcept {
    try {
        if (!state_validator(state) ||
            !observation_belongs_to_participant(current_observation, owning_participant) ||
            (state.last_accepted_decision_index.has_value() &&
             current_observation.decision_index <=
                 *state.last_accepted_decision_index)) {
            return std::nullopt;
        }
        auto next_state = state;
        std::vector<std::string> invalidation_reason_ids;
        if (!reconcile_in_place(next_state, current_observation, invalidation_reason_ids) ||
            !state_validator(next_state)) {
            return std::nullopt;
        }
        return ReconciliationResult{std::move(next_state),
                                    std::move(invalidation_reason_ids)};
    } catch (...) {
        return std::nullopt;
    }
}

template <typename State, typename ReconciliationResult, typename StateValidator>
std::optional<State> reconcile_strategy_state_impl(
    const State& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation,
    StateValidator state_validator) noexcept {
    const auto result = reconcile_strategy_state_with_evidence_impl<
        State, ReconciliationResult>(state, owning_participant, current_observation,
                                     state_validator);
    if (!result.has_value()) {
        return std::nullopt;
    }
    return result->state;
}

template <typename State, typename Delta, typename ReconciliationResult,
          typename StateProfileValidator, typename Reconcile,
          typename DeltaValidator, typename DeltaProfileValidator>
std::optional<Delta> propose_teacher_state_delta_impl(
    const State& current_state,
    const environment::PublicEnvironmentObservation& current_observation,
    const std::uint8_t owning_participant,
    const StrategyProfileV1& validated_profile,
    const Delta& requested_replacement,
    StateProfileValidator state_profile_validator,
    Reconcile reconcile,
    DeltaValidator delta_validator,
    DeltaProfileValidator delta_profile_validator) noexcept {
    try {
        if (!validate_strategy_profile(validated_profile) ||
            !state_profile_validator(current_state, validated_profile)) {
            return std::nullopt;
        }

        const auto reconciled =
            reconcile(current_state, owning_participant, current_observation);
        if (!reconciled.has_value() ||
            !state_profile_validator(reconciled->state, validated_profile) ||
            (current_state.last_accepted_decision_index.has_value() &&
             current_observation.decision_index <=
                 *current_state.last_accepted_decision_index)) {
            return std::nullopt;
        }

        const auto current_facts = extract_public_fact_snapshot(current_observation);
        if (!current_facts.valid || !delta_validator(requested_replacement) ||
            !requested_replacement.invalidation_reason_ids.empty() ||
            !delta_profile_validator(requested_replacement, validated_profile) ||
            !delta_current_facts_match_snapshot(requested_replacement,
                                                current_facts.snapshot) ||
            requested_replacement.base_last_accepted_decision_index !=
                reconciled->state.last_accepted_decision_index ||
            requested_replacement.base_last_accepted_public_action_key !=
                reconciled->state.last_accepted_public_action_key) {
            return std::nullopt;
        }

        Delta result = requested_replacement;
        result.strategy_profile_id = validated_profile.profile_id;
        result.base_last_accepted_decision_index = current_state.last_accepted_decision_index;
        result.base_last_accepted_public_action_key =
            current_state.last_accepted_public_action_key;
        result.invalidation_reason_ids = reconciled->invalidation_reason_ids;
        return delta_profile_validator(result, validated_profile)
                   ? std::optional<Delta>(std::move(result))
                   : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename State, typename RankingResult, typename Delta,
          typename ReconciliationResult, typename StateProfileValidator,
          typename RankingValidator, typename DeltaProfileValidator,
          typename ActionKeyValidator>
std::optional<ReconciliationResult> commit_teacher_state_delta_with_evidence_impl(
    State& current_state,
    const RankingResult& ranking_result,
    const StrategyProfileV1& validated_profile,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition,
    StateProfileValidator state_profile_validator,
    RankingValidator ranking_validator,
    DeltaProfileValidator delta_profile_validator,
    ActionKeyValidator action_key_validator) noexcept {
    try {
        if (!state_profile_validator(current_state, validated_profile) ||
            !validate_strategy_profile(validated_profile) ||
            !ranking_validator(ranking_result) ||
            ranking_result.status != TeacherRankingStatus::Selected ||
            !ranking_result.selected_public_action_key.has_value() ||
            !ranking_result.proposed_state_delta.has_value() ||
            !action_key_validator(*ranking_result.selected_public_action_key) ||
            !action_key_validator(accepted_transition.selected_public_action_key) ||
            owning_participant > 1 ||
            proposal_observation.perspective_player != owning_participant ||
            proposal_observation.decision_index != accepted_transition.decision_index ||
            ranking_result.proposed_state_delta->strategy_profile_id !=
                current_state.strategy_profile_id ||
            !delta_profile_validator(*ranking_result.proposed_state_delta,
                                     validated_profile) ||
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

        const Delta& delta = *ranking_result.proposed_state_delta;
        const auto proposal_facts = extract_public_fact_snapshot(proposal_observation);
        if (!proposal_facts.valid ||
            !delta_current_facts_match_snapshot(delta, proposal_facts.snapshot) ||
            !state_profile_validator(current_state, validated_profile) ||
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

        if (!state_profile_validator(next_state, validated_profile) ||
            !valid_reason_vector(delta.invalidation_reason_ids)) {
            return std::nullopt;
        }
        ReconciliationResult result;
        result.state = next_state;
        result.invalidation_reason_ids = delta.invalidation_reason_ids;
        current_state = std::move(next_state);
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename State, typename StateValidator>
bool observe_step_rejected_impl(State& state,
                                const environment::StepRejected& rejection,
                                StateValidator state_validator) noexcept {
    try {
        return rejection.authoritative_state_unchanged && state_validator(state);
    } catch (...) {
        return false;
    }
}

}  // namespace ygo::teacher::internal
