#include "ygo/teacher/candidate_evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"

namespace ygo::teacher {
namespace {

bool valid_score_dimension(const ScoreDimension dimension) noexcept {
    return static_cast<std::uint8_t>(dimension) < kTeacherScoreDimensionCount;
}

bool valid_candidate_domain(
    const std::vector<environment::EnvironmentActionCandidate>& candidates) noexcept {
    if (candidates.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!environment::is_public_action_key(candidates[index].public_action_key)) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (candidates[previous].public_action_key == candidates[index].public_action_key) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

bool evaluate_candidate_domain(
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const CandidateEvaluator& evaluator,
    std::vector<CandidateEvaluation>& evaluations) noexcept {
    evaluations.clear();
    try {
        if (!evaluator || !valid_candidate_domain(candidates)) {
            return false;
        }

        evaluations.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            auto evaluation = evaluator(candidate);
            if (evaluation.public_action_key != candidate.public_action_key) {
                evaluations.clear();
                return false;
            }
            evaluations.push_back(std::move(evaluation));
        }

        TeacherRankingResult validation;
        validation.status = TeacherRankingStatus::InvalidInput;
        validation.evaluations = evaluations;
        if (!validate_teacher_ranking_result(validation)) {
            evaluations.clear();
            return false;
        }
        return true;
    } catch (...) {
        evaluations.clear();
        return false;
    }
}

bool add_score_contribution(ScoreVector& score,
                            const ScoreDimension dimension,
                            const std::int32_t contribution) noexcept {
    if (!valid_score_dimension(dimension) ||
        contribution < kTeacherScoreContributionMinimum ||
        contribution > kTeacherScoreContributionMaximum) {
        return false;
    }

    auto& total = score.values[static_cast<std::size_t>(dimension)];
    const auto delta = static_cast<std::int64_t>(contribution);
    if ((delta > 0 && total > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && total < std::numeric_limits<std::int64_t>::min() - delta)) {
        return false;
    }
    total += delta;
    return true;
}

}  // namespace ygo::teacher
