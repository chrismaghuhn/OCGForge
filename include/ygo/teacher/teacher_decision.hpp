#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_explanation.hpp"

namespace ygo::policy {
struct PolicySelection;
}

namespace ygo::teacher {

enum class TeacherRankingStatus : std::uint8_t {
    Selected = 0,
    InvalidInput = 1,
    Blocked = 2,
    Unsupported = 3,
};

enum class CandidateEvaluationStatus : std::uint8_t {
    Supported = 0,
    NotApplicable = 1,
    Unsupported = 2,
    Invalid = 3,
};

struct CandidateEvaluation final {
    std::string public_action_key;
    CandidateEvaluationStatus status = CandidateEvaluationStatus::Invalid;
    std::optional<ScoreVector> score;
    std::vector<std::string> matched_intent_ids;
    std::vector<std::string> matched_goal_ids;
    std::vector<std::string> matched_line_ids;
    std::vector<std::string> reason_ids;
};

struct TeacherRankingResult final {
    TeacherRankingStatus status = TeacherRankingStatus::InvalidInput;
    std::vector<CandidateEvaluation> evaluations;
    std::optional<std::string> selected_public_action_key;
    std::optional<ScoreVector> selected_score_vector;
    std::optional<TeacherFallbackLevel> fallback_level;
    std::optional<TeacherDecisionExplanation> explanation;
    std::optional<TeacherStateDeltaV1> proposed_state_delta;

    // The state-delta field is value-owned and is introduced by Task 6. The
    // explanation field is value-owned and is introduced by Task 8. Optional
    // explanation data is derived audit data and is not gameplay identity;
    // its codec gate is deliberately separate from gameplay-result validation.
};

bool validate_teacher_ranking_result(const TeacherRankingResult& value,
                                     std::string* diagnostic = nullptr) noexcept;

policy::PolicySelection teacher_policy_selection_from_result(
    const TeacherRankingResult& value) noexcept;

}  // namespace ygo::teacher
