#include "ygo/teacher/strategy_state_v2.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string_view>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/teacher/teacher_decision_v2.hpp"
#include "ygo/trajectory/codec.hpp"
#include "teacher_validation.hpp"

namespace ygo::teacher {
namespace {

bool valid_profile_id(const std::string_view value) noexcept {
    return trajectory::is_canonical_identity(value, kStrategyProfileIdentityPrefix);
}

bool valid_optional_id(const std::optional<std::string>& value) noexcept {
    return !value.has_value() || detail::canonical_token(*value);
}

bool valid_id_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!detail::canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_fact_vector(const std::vector<PublicFactValue>& values) {
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

bool current_facts_match_snapshot(const std::vector<PublicFactValue>& values,
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

bool delta_current_facts_match_snapshot(const TeacherStateDeltaV2& delta,
                                        const PublicFactSnapshot& snapshot) {
    return current_facts_match_snapshot(delta.public_resource_facts, snapshot) &&
           current_facts_match_snapshot(delta.public_restriction_facts, snapshot) &&
           current_facts_match_snapshot(delta.public_threat_facts, snapshot);
}

bool valid_reason_vector(const std::vector<std::string>& values) noexcept {
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

bool valid_state_for_profile(const EpisodeLocalStrategyStateV2& state,
                             const StrategyProfileV1& profile) noexcept {
    return validate_strategy_state_v2(state) && valid_plan_references(state, profile);
}

bool valid_delta_for_profile(const TeacherStateDeltaV2& delta,
                             const StrategyProfileV1& profile) noexcept {
    return validate_teacher_state_delta_v2(delta) && valid_plan_references(delta, profile);
}

bool observation_belongs_to_participant(
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant) noexcept {
    return owning_participant <= 1 &&
           observation.perspective_player == owning_participant;
}

void retain_current_facts(std::vector<PublicFactValue>& facts,
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

bool reconcile_in_place(EpisodeLocalStrategyStateV2& state,
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

}  // namespace

bool validate_strategy_state_v2(const EpisodeLocalStrategyStateV2& state) noexcept {
    try {
        const bool has_index = state.last_accepted_decision_index.has_value();
        const bool has_key = state.last_accepted_public_action_key.has_value();
        return valid_profile_id(state.strategy_profile_id) &&
               valid_optional_id(state.active_goal_id) &&
               valid_optional_id(state.active_line_id) &&
               valid_id_vector(state.completed_line_node_ids) &&
               valid_id_vector(state.achieved_goal_ids) &&
               valid_fact_vector(state.public_resource_facts) &&
               valid_fact_vector(state.public_restriction_facts) &&
               valid_fact_vector(state.public_threat_facts) &&
               has_index == has_key &&
               (!has_key ||
                environment::is_public_action_key_v2(*state.last_accepted_public_action_key));
    } catch (...) {
        return false;
    }
}

bool validate_teacher_state_delta_v2(const TeacherStateDeltaV2& delta) noexcept {
    try {
        const bool has_index = delta.base_last_accepted_decision_index.has_value();
        const bool has_key = delta.base_last_accepted_public_action_key.has_value();
        return valid_profile_id(delta.strategy_profile_id) &&
               has_index == has_key &&
               (!has_key || environment::is_public_action_key_v2(
                                *delta.base_last_accepted_public_action_key)) &&
               environment::is_public_action_key_v2(delta.proposed_for_public_action_key) &&
               valid_optional_id(delta.active_goal_id) &&
               valid_optional_id(delta.active_line_id) &&
               valid_id_vector(delta.completed_line_node_ids) &&
               valid_id_vector(delta.achieved_goal_ids) &&
               valid_fact_vector(delta.public_resource_facts) &&
               valid_fact_vector(delta.public_restriction_facts) &&
               valid_fact_vector(delta.public_threat_facts) &&
               valid_reason_vector(delta.invalidation_reason_ids);
    } catch (...) {
        return false;
    }
}

std::optional<EpisodeLocalStrategyStateV2> reset_strategy_state_v2(
    const StrategyProfileV1& validated_profile) noexcept {
    try {
        if (!validate_strategy_profile(validated_profile) ||
            !valid_profile_id(validated_profile.profile_id)) {
            return std::nullopt;
        }
        EpisodeLocalStrategyStateV2 state;
        state.strategy_profile_id = validated_profile.profile_id;
        if (!valid_state_for_profile(state, validated_profile)) {
            return std::nullopt;
        }
        return state;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<StrategyReconciliationResultV2>
reconcile_strategy_state_with_evidence_v2(
    const EpisodeLocalStrategyStateV2& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept {
    try {
        if (!validate_strategy_state_v2(state) ||
            !observation_belongs_to_participant(current_observation, owning_participant) ||
            (state.last_accepted_decision_index.has_value() &&
             current_observation.decision_index <= *state.last_accepted_decision_index)) {
            return std::nullopt;
        }
        auto next_state = state;
        std::vector<std::string> invalidation_reason_ids;
        if (!reconcile_in_place(next_state, current_observation, invalidation_reason_ids) ||
            !validate_strategy_state_v2(next_state)) {
            return std::nullopt;
        }
        return StrategyReconciliationResultV2{std::move(next_state),
                                              std::move(invalidation_reason_ids)};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<EpisodeLocalStrategyStateV2> reconcile_strategy_state_v2(
    const EpisodeLocalStrategyStateV2& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept {
    const auto result = reconcile_strategy_state_with_evidence_v2(
        state, owning_participant, current_observation);
    if (!result.has_value()) {
        return std::nullopt;
    }
    return result->state;
}

std::optional<TeacherStateDeltaV2> propose_teacher_state_delta_v2(
    const EpisodeLocalStrategyStateV2& current_state,
    const environment::PublicEnvironmentObservation& current_observation,
    const std::uint8_t owning_participant,
    const StrategyProfileV1& validated_profile,
    const TeacherStateDeltaV2& requested_replacement) noexcept {
    try {
        if (!validate_strategy_profile(validated_profile) ||
            !valid_state_for_profile(current_state, validated_profile)) {
            return std::nullopt;
        }
        const auto reconciled = reconcile_strategy_state_with_evidence_v2(
            current_state, owning_participant, current_observation);
        if (!reconciled.has_value() ||
            !valid_state_for_profile(reconciled->state, validated_profile) ||
            (current_state.last_accepted_decision_index.has_value() &&
             current_observation.decision_index <=
                 *current_state.last_accepted_decision_index)) {
            return std::nullopt;
        }
        const auto current_facts = extract_public_fact_snapshot(current_observation);
        if (!current_facts.valid || !validate_teacher_state_delta_v2(requested_replacement) ||
            !requested_replacement.invalidation_reason_ids.empty() ||
            !valid_delta_for_profile(requested_replacement, validated_profile) ||
            !delta_current_facts_match_snapshot(requested_replacement, current_facts.snapshot) ||
            requested_replacement.base_last_accepted_decision_index !=
                reconciled->state.last_accepted_decision_index ||
            requested_replacement.base_last_accepted_public_action_key !=
                reconciled->state.last_accepted_public_action_key) {
            return std::nullopt;
        }

        TeacherStateDeltaV2 result = requested_replacement;
        result.strategy_profile_id = validated_profile.profile_id;
        result.base_last_accepted_decision_index = current_state.last_accepted_decision_index;
        result.base_last_accepted_public_action_key =
            current_state.last_accepted_public_action_key;
        result.invalidation_reason_ids = reconciled->invalidation_reason_ids;
        return valid_delta_for_profile(result, validated_profile)
                   ? std::optional<TeacherStateDeltaV2>(std::move(result))
                   : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<StrategyReconciliationResultV2>
commit_teacher_state_delta_with_evidence_v2(
    EpisodeLocalStrategyStateV2& current_state,
    const TeacherRankingResultV2& ranking_result,
    const StrategyProfileV1& validated_profile,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    try {
        if (!valid_state_for_profile(current_state, validated_profile) ||
            !validate_strategy_profile(validated_profile) ||
            !validate_teacher_ranking_result_v2(ranking_result) ||
            ranking_result.status != TeacherRankingStatus::Selected ||
            !ranking_result.selected_public_action_key.has_value() ||
            !ranking_result.proposed_state_delta.has_value() ||
            !environment::is_public_action_key_v2(*ranking_result.selected_public_action_key) ||
            !environment::is_public_action_key_v2(accepted_transition.selected_public_action_key) ||
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
            !delta_current_facts_match_snapshot(delta, proposal_facts.snapshot) ||
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
            !valid_reason_vector(delta.invalidation_reason_ids)) {
            return std::nullopt;
        }
        StrategyReconciliationResultV2 result;
        result.state = next_state;
        result.invalidation_reason_ids = delta.invalidation_reason_ids;
        current_state = std::move(next_state);
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

bool commit_teacher_state_delta_v2(
    EpisodeLocalStrategyStateV2& current_state,
    const TeacherRankingResultV2& ranking_result,
    const StrategyProfileV1& validated_profile,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    return commit_teacher_state_delta_with_evidence_v2(
               current_state, ranking_result, validated_profile, owning_participant,
               proposal_observation, accepted_transition)
        .has_value();
}

bool observe_step_rejected_v2(EpisodeLocalStrategyStateV2& state,
                              const environment::StepRejected& rejection) noexcept {
    try {
        return rejection.authoritative_state_unchanged && validate_strategy_state_v2(state);
    } catch (...) {
        return false;
    }
}

}  // namespace ygo::teacher
