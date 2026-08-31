#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ygo/teacher/teacher_explanation.hpp"

namespace ygo::policy {
struct PolicyError;
struct PolicySelection;
}

namespace ygo::teacher {

struct TeacherStateDelta;
using TeacherStateDeltaPtr = std::shared_ptr<const TeacherStateDelta>;

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
    std::optional<std::uint8_t> fallback_level;

    // These pointers are optional handles to future-owned values. Task 3
    // never constructs, serializes, or interprets either value.
    TeacherDecisionExplanationPtr explanation;
    TeacherStateDeltaPtr proposed_state_delta;

    // Reuses the existing policy error type; this is not a Teacher-specific
    // gameplay result channel.
    using PolicyErrorPtr = std::shared_ptr<const policy::PolicyError>;
    PolicyErrorPtr diagnostic;
};

bool validate_teacher_ranking_result(const TeacherRankingResult& value,
                                     std::string* diagnostic = nullptr) noexcept;

policy::PolicySelection teacher_policy_selection_from_result(
    const TeacherRankingResult& value) noexcept;

}  // namespace ygo::teacher
