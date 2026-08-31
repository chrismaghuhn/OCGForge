#include "ygo/teacher/strategy_state.hpp"

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

bool reconcile_in_place(EpisodeLocalStrategyStateV1& state,
                        const environment::PublicEnvironmentObservation& observation) {
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
    }
    return true;
}

}  // namespace

std::optional<EpisodeLocalStrategyStateV1> reconcile_strategy_state(
    const EpisodeLocalStrategyStateV1& state,
    const environment::PublicEnvironmentObservation& next_observation) noexcept {
    try {
        if (!validate_strategy_state(state)) {
            return std::nullopt;
        }
        auto next_state = state;
        if (!reconcile_in_place(next_state, next_observation) ||
            !validate_strategy_state(next_state)) {
            return std::nullopt;
        }
        return next_state;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace ygo::teacher
