#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

enum class TeacherFallbackLevel : std::uint8_t {
    F0 = 0,
    F1 = 1,
    F2 = 2,
    F3 = 3,
    F4 = 4,
};

enum class CandidateEvaluationStatus : std::uint8_t {
    Supported = 0,
    NotApplicable = 1,
    Unsupported = 2,
    Invalid = 3,
};

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

    // This is the intentionally minimal Task-3 staging shape. The future
    // owning tasks add value-owned explanation, state-delta, and diagnostic
    // fields; Task 3 does not invent placeholder semantics for them.
};

bool validate_teacher_ranking_result(const TeacherRankingResult& value,
                                     std::string* diagnostic = nullptr) noexcept;

policy::PolicySelection teacher_policy_selection_from_result(
    const TeacherRankingResult& value) noexcept;

}  // namespace ygo::teacher
