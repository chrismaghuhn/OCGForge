#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_decision.hpp"

namespace ygo::policy {
struct PolicySelection;
}

namespace ygo::teacher {

struct TeacherRankingResultV2 final {
    TeacherRankingStatus status = TeacherRankingStatus::InvalidInput;
    std::vector<CandidateEvaluation> evaluations;
    std::optional<std::string> selected_public_action_key;
    std::optional<ScoreVector> selected_score_vector;
    std::optional<TeacherFallbackLevel> fallback_level;
    std::optional<TeacherStateDeltaV2> proposed_state_delta;
};

bool validate_teacher_ranking_result_v2(
    const TeacherRankingResultV2& value,
    std::string* diagnostic = nullptr) noexcept;

policy::PolicySelection teacher_policy_selection_from_result_v2(
    const TeacherRankingResultV2& value) noexcept;

}  // namespace ygo::teacher
