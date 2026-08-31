#include "ygo/teacher/recovery_controller.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace ygo::teacher {
namespace {

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
        return context.pre_active_goal_id().has_value() &&
               *context.pre_active_goal_id() == edge.source_id;
    case RecoverySourceKind::Line:
        return context.pre_active_line_id().has_value() &&
               *context.pre_active_line_id() == edge.source_id;
    case RecoverySourceKind::Node:
        return std::binary_search(context.pre_ready_node_ids().begin(),
                                  context.pre_ready_node_ids().end(), edge.source_id);
    }
    return false;
}

}  // namespace

RecoverySelection select_recovery_edge(
    const StrategyProfileV1& profile,
    const PreReconciliationPlanContext& context,
    const StrategyReconciliationResult& reconciliation_evidence,
    const PublicFactSnapshot& public_facts) noexcept {
    RecoverySelection result;
    try {
        if (!validate_strategy_profile(profile) || !valid_snapshot(public_facts) ||
            !validate_strategy_state(reconciliation_evidence.state) ||
            reconciliation_evidence.state.strategy_profile_id != profile.profile_id ||
            !sorted_unique_ids(context.pre_ready_node_ids()) ||
            (context.pre_active_goal_id().has_value() &&
             !canonical_token(*context.pre_active_goal_id())) ||
            (context.pre_active_line_id().has_value() &&
             !canonical_token(*context.pre_active_line_id())) ||
            !sorted_unique_ids(reconciliation_evidence.invalidation_reason_ids)) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        for (const auto& reason : reconciliation_evidence.invalidation_reason_ids) {
            if (!is_registered_invalidation_reason(reason)) {
                result.status = PredicateEvaluationStatus::Invalid;
                return result;
            }
        }
        if (context.pre_active_goal_id().has_value() &&
            find_goal(profile, *context.pre_active_goal_id()) == nullptr) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        if (context.pre_active_line_id().has_value()) {
            const auto* line = find_line(profile, *context.pre_active_line_id());
            if (line == nullptr || !context.pre_active_goal_id().has_value() ||
                line->goal_id != *context.pre_active_goal_id()) {
                result.status = PredicateEvaluationStatus::Invalid;
                return result;
            }
        }

        const RecoveryEdge* selected = nullptr;
        bool saw_unsupported = false;
        bool saw_invalid = false;
        for (const auto& edge : profile.recovery_edges) {
            if (!recovery_source_matches(edge, context) || edge.invalidation_reason_ids.empty() ||
                !has_all_reasons(edge.invalidation_reason_ids,
                                 reconciliation_evidence.invalidation_reason_ids)) {
                continue;
            }
            if (context.pre_active_line_id().has_value() &&
                (edge.source_kind == RecoverySourceKind::Line ||
                 edge.source_kind == RecoverySourceKind::Node)) {
                const auto* line = find_line(profile, *context.pre_active_line_id());
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

}  // namespace ygo::teacher
