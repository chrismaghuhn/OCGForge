#include "ygo/teacher/recovery_controller.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/teacher/strategy_state.hpp"

namespace ygo::teacher {
namespace {

struct PreReconciliationPlanContext final {
    std::optional<std::string> active_goal_id;
    std::optional<std::string> active_line_id;
    std::vector<std::string> ready_node_ids;
};

bool canonical_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '_' || byte == '-')) {
            return false;
        }
    }
    return value.front() != '.' && value.back() != '.' &&
           value.find("..") == std::string_view::npos;
}

bool sorted_unique_ids(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_snapshot(const PublicFactSnapshot& snapshot) noexcept {
    try {
        const auto& registry = PublicFactRegistry::canonical();
        for (std::size_t index = 0; index < snapshot.values.size(); ++index) {
            if (!registry.validate(snapshot.values[index])) {
                return false;
            }
            if (index > 0 &&
                (snapshot.values[index - 1].fact_id == snapshot.values[index].fact_id ||
                 !(canonical_public_fact_value_bytes(snapshot.values[index - 1]) <
                   canonical_public_fact_value_bytes(snapshot.values[index])))) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

const GoalDefinition* find_goal(const StrategyProfileV1& profile,
                                const std::string_view id) noexcept {
    const auto it = std::find_if(profile.goals.begin(), profile.goals.end(), [&](const auto& goal) {
        return goal.goal_id == id;
    });
    return it == profile.goals.end() ? nullptr : &*it;
}

const LineDefinition* find_line(const StrategyProfileV1& profile,
                                const std::string_view id) noexcept {
    const auto it = std::find_if(profile.lines.begin(), profile.lines.end(), [&](const auto& line) {
        return line.line_id == id;
    });
    return it == profile.lines.end() ? nullptr : &*it;
}

bool valid_state_for_profile(const EpisodeLocalStrategyStateV1& state,
                             const StrategyProfileV1& profile) noexcept {
    if (!validate_strategy_state(state) || state.strategy_profile_id != profile.profile_id) {
        return false;
    }
    for (const auto& goal_id : state.achieved_goal_ids) {
        if (find_goal(profile, goal_id) == nullptr) {
            return false;
        }
    }
    if (state.active_goal_id.has_value() &&
        find_goal(profile, *state.active_goal_id) == nullptr) {
        return false;
    }
    if (!state.active_line_id.has_value()) {
        return state.completed_line_node_ids.empty();
    }
    const auto* line = find_line(profile, *state.active_line_id);
    if (line == nullptr || !state.active_goal_id.has_value() ||
        line->goal_id != *state.active_goal_id) {
        return false;
    }
    return std::all_of(state.completed_line_node_ids.begin(),
                       state.completed_line_node_ids.end(), [&](const auto& node_id) {
                           return std::any_of(line->nodes.begin(), line->nodes.end(),
                                              [&](const auto& node) {
                                                  return node.node_id == node_id;
                                              });
                       });
}

bool contains_id(const std::vector<std::string>& values, const std::string_view id) noexcept {
    return std::binary_search(values.begin(), values.end(), id);
}

std::vector<std::string> ready_nodes(const LineDefinition& line,
                                     const std::vector<std::string>& completed) {
    std::vector<std::string> result;
    for (const auto& node : line.nodes) {
        if (contains_id(completed, node.node_id)) {
            continue;
        }
        bool ready = true;
        for (const auto& dependency : line.dependencies) {
            if (dependency.successor_node_id == node.node_id &&
                !contains_id(completed, dependency.predecessor_node_id)) {
                ready = false;
                break;
            }
        }
        if (ready) {
            result.push_back(node.node_id);
        }
    }
    return result;
}

std::optional<PreReconciliationPlanContext> derive_context(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& state) noexcept {
    try {
        if (!validate_strategy_profile(profile) || !valid_state_for_profile(state, profile)) {
            return std::nullopt;
        }
        PreReconciliationPlanContext result;
        result.active_goal_id = state.active_goal_id;
        result.active_line_id = state.active_line_id;
        if (state.active_line_id.has_value()) {
            const auto* line = find_line(profile, *state.active_line_id);
            if (line == nullptr) {
                return std::nullopt;
            }
            result.ready_node_ids = ready_nodes(*line, state.completed_line_node_ids);
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

bool has_all_reasons(const std::vector<std::string>& required,
                     const std::vector<std::string>& present) noexcept {
    return std::all_of(required.begin(), required.end(), [&](const auto& reason) {
        return std::binary_search(present.begin(), present.end(), reason);
    });
}

bool recovery_source_matches(const RecoveryEdge& edge,
                             const PreReconciliationPlanContext& context) noexcept {
    switch (edge.source_kind) {
    case RecoverySourceKind::Goal:
        return context.active_goal_id.has_value() && *context.active_goal_id == edge.source_id;
    case RecoverySourceKind::Line:
        return context.active_line_id.has_value() && *context.active_line_id == edge.source_id;
    case RecoverySourceKind::Node:
        return std::binary_search(context.ready_node_ids.begin(), context.ready_node_ids.end(),
                                  edge.source_id);
    }
    return false;
}

RecoverySelection select_recovery_edge_internal(
    const StrategyProfileV1& profile,
    const PreReconciliationPlanContext& context,
    const std::vector<std::string>& invalidation_reason_ids,
    const PublicFactSnapshot& public_facts) noexcept {
    RecoverySelection result;
    try {
        if (!valid_snapshot(public_facts) || !sorted_unique_ids(context.ready_node_ids) ||
            (context.active_goal_id.has_value() && !canonical_token(*context.active_goal_id)) ||
            (context.active_line_id.has_value() && !canonical_token(*context.active_line_id)) ||
            !sorted_unique_ids(invalidation_reason_ids)) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        for (const auto& reason : invalidation_reason_ids) {
            if (!is_registered_invalidation_reason(reason)) {
                result.status = PredicateEvaluationStatus::Invalid;
                return result;
            }
        }

        const RecoveryEdge* selected = nullptr;
        bool saw_unsupported = false;
        bool saw_invalid = false;
        for (const auto& edge : profile.recovery_edges) {
            if (!recovery_source_matches(edge, context) || edge.invalidation_reason_ids.empty() ||
                !has_all_reasons(edge.invalidation_reason_ids, invalidation_reason_ids)) {
                continue;
            }
            if (context.active_line_id.has_value() &&
                (edge.source_kind == RecoverySourceKind::Line ||
                 edge.source_kind == RecoverySourceKind::Node)) {
                const auto* line = find_line(profile, *context.active_line_id);
                if (line == nullptr ||
                    !std::binary_search(line->recovery_edge_ids.begin(),
                                        line->recovery_edge_ids.end(), edge.recovery_edge_id)) {
                    continue;
                }
            }

            const auto status = edge.preconditions.empty()
                                    ? PredicateEvaluationStatus::True
                                    : evaluate_public_predicate_conjunction(
                                          edge.preconditions, public_facts, profile);
            if (status == PredicateEvaluationStatus::Invalid) {
                saw_invalid = true;
                continue;
            }
            if (status == PredicateEvaluationStatus::Unsupported) {
                saw_unsupported = true;
                continue;
            }
            if (status != PredicateEvaluationStatus::True) {
                continue;
            }

            const auto* target_goal = find_goal(profile, edge.target_goal_id);
            if (target_goal == nullptr) {
                saw_invalid = true;
                continue;
            }
            const auto* selected_goal =
                selected == nullptr ? nullptr : find_goal(profile, selected->target_goal_id);
            const bool preferred =
                selected == nullptr || selected_goal == nullptr ||
                target_goal->priority > selected_goal->priority ||
                (target_goal->priority == selected_goal->priority &&
                 static_cast<std::uint8_t>(edge.confidence_cap) <
                     static_cast<std::uint8_t>(selected->confidence_cap)) ||
                (target_goal->priority == selected_goal->priority &&
                 edge.confidence_cap == selected->confidence_cap &&
                 edge.recovery_edge_id < selected->recovery_edge_id);
            if (preferred) {
                selected = &edge;
            }
        }

        if (selected == nullptr) {
            result.status = saw_invalid ? PredicateEvaluationStatus::Invalid
                                         : (saw_unsupported ? PredicateEvaluationStatus::Unsupported
                                                            : PredicateEvaluationStatus::False);
            return result;
        }
        result.status = PredicateEvaluationStatus::True;
        result.recovery_edge_id = selected->recovery_edge_id;
        result.target_goal_id = selected->target_goal_id;
        result.target_line_id = selected->target_line_id;
        return result;
    } catch (...) {
        result = {};
        result.status = PredicateEvaluationStatus::Invalid;
        return result;
    }
}

}  // namespace

RecoverySelection select_recovery_edge(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& pre_reconciliation_state,
    const environment::PublicEnvironmentObservation& current_observation,
    const std::uint8_t owning_participant) noexcept {
    RecoverySelection result;
    try {
        if (!validate_strategy_profile(profile) ||
            !valid_state_for_profile(pre_reconciliation_state, profile) ||
            owning_participant > 1 ||
            current_observation.perspective_player != owning_participant) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        const auto context = derive_context(profile, pre_reconciliation_state);
        if (!context.has_value()) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        const auto reconciliation = reconcile_strategy_state_with_evidence(
            pre_reconciliation_state, owning_participant, current_observation);
        if (!reconciliation.has_value() ||
            reconciliation->state.strategy_profile_id != profile.profile_id) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        const auto facts = extract_public_fact_snapshot(current_observation);
        if (!facts.valid) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        return select_recovery_edge_internal(profile, *context,
                                             reconciliation->invalidation_reason_ids,
                                             facts.snapshot);
    } catch (...) {
        result.status = PredicateEvaluationStatus::Invalid;
        return result;
    }
}

}  // namespace ygo::teacher
