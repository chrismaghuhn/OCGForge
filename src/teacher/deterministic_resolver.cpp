#include "ygo/teacher/deterministic_resolver.hpp"

#include <cstddef>
#include <utility>

namespace ygo::teacher {
namespace {

int compare_scores(const ScoreVector& left, const ScoreVector& right) noexcept {
    for (std::size_t index = 0; index < kTeacherScoreDimensionCount; ++index) {
        if (left.values[index] > right.values[index]) {
            return 1;
        }
        if (left.values[index] < right.values[index]) {
            return -1;
        }
    }
    return 0;
}

TeacherRankingResult no_selection(TeacherRankingStatus status,
                                  std::vector<CandidateEvaluation> evaluations) {
    TeacherRankingResult result;
    result.status = status;
    result.evaluations = std::move(evaluations);
    return result;
}

}  // namespace

TeacherRankingResult resolve_teacher_ranking(
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const CandidateEvaluator& evaluator) noexcept {
    std::vector<CandidateEvaluation> evaluations;
    if (!evaluate_candidate_domain(candidates, evaluator, evaluations)) {
        return no_selection(TeacherRankingStatus::InvalidInput, {});
    }

    for (const auto& evaluation : evaluations) {
        if (evaluation.status == CandidateEvaluationStatus::Invalid) {
            return no_selection(TeacherRankingStatus::InvalidInput,
                                std::move(evaluations));
        }
        if (evaluation.status == CandidateEvaluationStatus::Unsupported) {
            return no_selection(TeacherRankingStatus::Unsupported,
                                std::move(evaluations));
        }
        if (!evaluation.score.has_value()) {
            return no_selection(TeacherRankingStatus::Blocked,
                                std::move(evaluations));
        }
    }

    std::size_t best_index = 0;
    for (std::size_t index = 1; index < evaluations.size(); ++index) {
        const auto score_order =
            compare_scores(*evaluations[index].score, *evaluations[best_index].score);
        if (score_order > 0 ||
            (score_order == 0 &&
             evaluations[index].public_action_key < evaluations[best_index].public_action_key)) {
            best_index = index;
        }
    }

    TeacherRankingResult result;
    result.status = TeacherRankingStatus::Selected;
    result.evaluations = std::move(evaluations);
    result.selected_public_action_key = result.evaluations[best_index].public_action_key;
    result.selected_score_vector = result.evaluations[best_index].score;
    if (!validate_teacher_ranking_result(result)) {
        return no_selection(TeacherRankingStatus::InvalidInput,
                            std::move(result.evaluations));
    }
    return result;
}

}  // namespace ygo::teacher
