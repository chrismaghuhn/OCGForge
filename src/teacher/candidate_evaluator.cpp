#include "ygo/teacher/candidate_evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
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

CandidateEvaluation invalid_evaluation(
    const environment::EnvironmentActionCandidate& candidate) {
    CandidateEvaluation result;
    result.public_action_key = candidate.public_action_key;
    result.status = CandidateEvaluationStatus::Invalid;
    return result;
}

bool valid_evaluator_evaluation(const CandidateEvaluation& evaluation) {
    TeacherRankingResult validation;
    validation.status = TeacherRankingStatus::InvalidInput;
    validation.evaluations.push_back(evaluation);
    return validate_teacher_ranking_result(validation) &&
           evaluation.status != CandidateEvaluationStatus::Invalid;
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
            CandidateEvaluation evaluation;
            bool evaluator_failed = false;
            try {
                evaluation = evaluator(candidate);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (...) {
                evaluator_failed = true;
            }

            if (evaluator_failed ||
                evaluation.public_action_key != candidate.public_action_key ||
                !valid_evaluator_evaluation(evaluation)) {
                evaluations.push_back(invalid_evaluation(candidate));
            } else {
                evaluations.push_back(std::move(evaluation));
            }
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
