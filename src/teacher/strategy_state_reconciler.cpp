#include "ygo/teacher/strategy_state.hpp"

#include <cstddef>
#include <utility>

namespace ygo::teacher {
namespace {

bool retain_current_facts(std::vector<PublicFactValue>& facts,
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
    return true;
}

bool valid_reason_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!is_registered_invalidation_reason(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool observation_belongs_to_participant(
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant) noexcept {
    return owning_participant <= 1 &&
           observation.perspective_player == owning_participant;
}

bool reconcile_in_place(EpisodeLocalStrategyStateV1& state,
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

std::optional<StrategyReconciliationResult> reconcile_strategy_state_with_evidence(
    const EpisodeLocalStrategyStateV1& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept {
    try {
        if (!validate_strategy_state(state) ||
            !observation_belongs_to_participant(current_observation, owning_participant) ||
            (state.last_accepted_decision_index.has_value() &&
             current_observation.decision_index <=
                 *state.last_accepted_decision_index)) {
            return std::nullopt;
        }
        auto next_state = state;
        std::vector<std::string> invalidation_reason_ids;
        if (!reconcile_in_place(next_state, current_observation, invalidation_reason_ids) ||
            !validate_strategy_state(next_state)) {
            return std::nullopt;
        }
        return StrategyReconciliationResult{std::move(next_state),
                                             std::move(invalidation_reason_ids)};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<EpisodeLocalStrategyStateV1> reconcile_strategy_state(
    const EpisodeLocalStrategyStateV1& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept {
    const auto result = reconcile_strategy_state_with_evidence(
        state, owning_participant, current_observation);
    if (!result.has_value()) {
        return std::nullopt;
    }
    return result->state;
}

}  // namespace ygo::teacher
