#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/strategy_profile.hpp"

namespace ygo::teacher {

inline constexpr std::int32_t kTeacherScoreContributionMinimum = -1'000'000;
inline constexpr std::int32_t kTeacherScoreContributionMaximum = 1'000'000;

using CandidateEvaluator =
    std::function<CandidateEvaluation(const environment::EnvironmentActionCandidate&)>;

// Evaluates one supplied candidate exactly once in supplied order. On
// failure, evaluations is cleared and no partial evidence is trusted.
bool evaluate_candidate_domain(
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const CandidateEvaluator& evaluator,
    std::vector<CandidateEvaluation>& evaluations) noexcept;

// Adds one bounded i32 contribution to one existing i64 score component.
// Invalid dimensions, out-of-range contributions, and overflow/underflow
// leave score unchanged and return false.
bool add_score_contribution(ScoreVector& score,
                            ScoreDimension dimension,
                            std::int32_t contribution) noexcept;

}  // namespace ygo::teacher
