#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/strategy_profile.hpp"

namespace ygo::teacher {

inline constexpr std::size_t kTeacherScoreDimensionCount = 9;

struct ScoreVector final {
    std::array<std::int64_t, kTeacherScoreDimensionCount> values{};

    bool operator==(const ScoreVector& other) const noexcept {
        return values == other.values;
    }
    bool operator!=(const ScoreVector& other) const noexcept {
        return !(*this == other);
    }
};

enum class TeacherFallbackLevel : std::uint8_t {
    F0 = 0,
    F1 = 1,
    F2 = 2,
    F3 = 3,
    F4 = 4,
};

struct TeacherDecisionExplanation final {
    std::string selected_public_action_key;
    ScoreVector selected_score_vector;
    std::optional<ScoreVector> runner_up_score_vector;
    ConfidenceClass confidence_class = ConfidenceClass::Fallback;
    TeacherFallbackLevel fallback_level = TeacherFallbackLevel::F4;
    std::optional<std::string> active_goal_id;
    std::optional<std::string> active_line_id;
    std::optional<std::string> active_line_node_id;
    std::vector<std::string> matched_intent_ids;
    std::vector<std::string> invalidation_reason_ids;
    std::vector<PublicFactValue> relevant_public_feature_values;
    std::string explanation_schema_id = std::string(kTeacherDiagnosticContractId);

    bool operator==(const TeacherDecisionExplanation& other) const noexcept {
        return selected_public_action_key == other.selected_public_action_key &&
               selected_score_vector == other.selected_score_vector &&
               runner_up_score_vector == other.runner_up_score_vector &&
               confidence_class == other.confidence_class &&
               fallback_level == other.fallback_level &&
               active_goal_id == other.active_goal_id &&
               active_line_id == other.active_line_id &&
               active_line_node_id == other.active_line_node_id &&
               matched_intent_ids == other.matched_intent_ids &&
               invalidation_reason_ids == other.invalidation_reason_ids &&
               relevant_public_feature_values == other.relevant_public_feature_values &&
               explanation_schema_id == other.explanation_schema_id;
    }
    bool operator!=(const TeacherDecisionExplanation& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace ygo::teacher
