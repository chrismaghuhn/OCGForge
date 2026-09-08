#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/strategy_profile.hpp"
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

}  // namespace ygo::teacher::internal
