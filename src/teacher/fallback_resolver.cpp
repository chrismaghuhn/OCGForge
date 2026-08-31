#include "ygo/teacher/fallback_resolver.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/teacher/teacher_explanation_codec.hpp"

namespace ygo::teacher {
namespace {

enum class StageReadiness : std::uint8_t {
    Total,
    Unsupported,
    Blocked,
    Invalid,
};

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

bool better_evaluation(const CandidateEvaluation& left,
                       const CandidateEvaluation& right) noexcept {
    const auto score_order = compare_scores(*left.score, *right.score);
    return score_order > 0 ||
           (score_order == 0 && left.public_action_key < right.public_action_key);
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

bool valid_id_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

std::vector<CandidateEvaluation> base_evaluations(
    const std::vector<environment::EnvironmentActionCandidate>& candidates) {
    std::vector<CandidateEvaluation> result;
    result.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        CandidateEvaluation evaluation;
        evaluation.public_action_key = candidate.public_action_key;
        evaluation.status = CandidateEvaluationStatus::NotApplicable;
        result.push_back(std::move(evaluation));
    }
    return result;
}

TeacherRankingResult no_selection(TeacherRankingStatus status,
                                  std::vector<CandidateEvaluation> evaluations) {
    TeacherRankingResult result;
    result.status = status;
    result.evaluations = std::move(evaluations);
    return result;
}

bool stage_shape_is_valid(
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const std::vector<TeacherFallbackCandidateValue>& evaluations) noexcept {
    if (evaluations.size() != candidates.size()) {
        return false;
    }
    for (std::size_t index = 0; index < evaluations.size(); ++index) {
        const auto& evaluation = evaluations[index];
        if (!environment::is_public_action_key(evaluation.public_action_key) ||
            evaluation.public_action_key != candidates[index].public_action_key ||
            static_cast<std::uint8_t>(evaluation.status) >
                static_cast<std::uint8_t>(CandidateEvaluationStatus::Invalid) ||
            !valid_id_vector(evaluation.matched_intent_ids) ||
            !valid_id_vector(evaluation.matched_goal_ids) ||
            !valid_id_vector(evaluation.matched_line_ids) ||
            !valid_id_vector(evaluation.reason_ids)) {
            return false;
        }
    }
    return true;
}

StageReadiness compose_stage_value(const TeacherFallbackCandidateValue& value,
                                   ScoreVector& composed) noexcept {
    if (value.status == CandidateEvaluationStatus::Invalid) {
        return StageReadiness::Invalid;
    }
    if (value.status == CandidateEvaluationStatus::Unsupported) {
        return value.score.has_value() || !value.contributions.empty()
                   ? StageReadiness::Invalid
                   : StageReadiness::Unsupported;
    }
    if (!value.score.has_value()) {
        return StageReadiness::Blocked;
    }
    ScoreVector next;
    for (const auto& contribution : value.contributions) {
        if (!add_score_contribution(next, contribution.dimension, contribution.value)) {
            return StageReadiness::Invalid;
        }
    }
    if (next != *value.score) {
        return StageReadiness::Invalid;
    }
    composed = next;
    return StageReadiness::Total;
}

StageReadiness stage_readiness(
    const std::vector<TeacherFallbackCandidateValue>& evaluations) noexcept {
    bool saw_unsupported = false;
    bool saw_blocked = false;
    for (const auto& evaluation : evaluations) {
        ScoreVector composed;
        switch (compose_stage_value(evaluation, composed)) {
        case StageReadiness::Invalid:
            return StageReadiness::Invalid;
        case StageReadiness::Unsupported:
            saw_unsupported = true;
            break;
        case StageReadiness::Blocked:
            saw_blocked = true;
            break;
        case StageReadiness::Total:
            break;
        }
    }
    if (saw_blocked) {
        return StageReadiness::Blocked;
    }
    if (saw_unsupported) {
        return StageReadiness::Unsupported;
    }
    return StageReadiness::Total;
}

bool apply_stage(
    std::vector<CandidateEvaluation>& result,
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const std::vector<TeacherFallbackCandidateValue>& stage) {
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        ScoreVector composed;
        if (compose_stage_value(stage[index], composed) != StageReadiness::Total) {
            return false;
        }
        result[index].public_action_key = candidates[index].public_action_key;
        result[index].status = stage[index].status;
        result[index].score = composed;
        result[index].matched_intent_ids = stage[index].matched_intent_ids;
        result[index].matched_goal_ids = stage[index].matched_goal_ids;
        result[index].matched_line_ids = stage[index].matched_line_ids;
        result[index].reason_ids = stage[index].reason_ids;
    }
    return true;
}

ConfidenceClass confidence_for(const TeacherFallbackLevel level) noexcept {
    switch (level) {
    case TeacherFallbackLevel::F0:
        return ConfidenceClass::High;
    case TeacherFallbackLevel::F1:
        return ConfidenceClass::Medium;
    case TeacherFallbackLevel::F2:
        return ConfidenceClass::Medium;
    case TeacherFallbackLevel::F3:
        return ConfidenceClass::Low;
    case TeacherFallbackLevel::F4:
        return ConfidenceClass::Fallback;
    }
    return ConfidenceClass::Fallback;
}

void attach_explanation(TeacherRankingResult& result) noexcept {
    try {
        if (result.status != TeacherRankingStatus::Selected ||
            !result.selected_public_action_key.has_value() ||
            !result.selected_score_vector.has_value() || !result.fallback_level.has_value()) {
            return;
        }
        const auto selected = std::find_if(
            result.evaluations.begin(), result.evaluations.end(), [&](const auto& evaluation) {
                return evaluation.public_action_key == *result.selected_public_action_key;
            });
        if (selected == result.evaluations.end()) {
            return;
        }
        const CandidateEvaluation* runner_up = nullptr;
        for (const auto& evaluation : result.evaluations) {
            if (&evaluation == &*selected) {
                continue;
            }
            if (runner_up == nullptr || better_evaluation(evaluation, *runner_up)) {
                runner_up = &evaluation;
            }
        }

        TeacherDecisionExplanation explanation;
        explanation.selected_public_action_key = *result.selected_public_action_key;
        explanation.selected_score_vector = *result.selected_score_vector;
        if (runner_up != nullptr && runner_up->score.has_value()) {
            explanation.runner_up_score_vector = runner_up->score;
        }
        explanation.confidence_class = confidence_for(*result.fallback_level);
        explanation.fallback_level = *result.fallback_level;
        explanation.matched_intent_ids = selected->matched_intent_ids;
        explanation.invalidation_reason_ids = selected->reason_ids;
        if (validate_teacher_decision_explanation(explanation)) {
            result.explanation = std::move(explanation);
        }
    } catch (...) {
        // Explanation is optional audit data. Its inability to publish must
        // never alter the already selected public action or invoke a stage.
        result.explanation.reset();
    }
}

TeacherRankingResult selected_result(std::vector<CandidateEvaluation> evaluations,
                                     const std::size_t best_index,
                                     const TeacherFallbackLevel level) {
    TeacherRankingResult result;
    result.status = TeacherRankingStatus::Selected;
    result.evaluations = std::move(evaluations);
    result.selected_public_action_key = result.evaluations[best_index].public_action_key;
    result.selected_score_vector = result.evaluations[best_index].score;
    result.fallback_level = level;
    attach_explanation(result);
    return result;
}

}  // namespace

TeacherRankingResult resolve_teacher_fallback(
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const TeacherFallbackStageSet& stages) noexcept {
    try {
        auto base = base_evaluations(candidates);
        if (!valid_candidate_domain(candidates)) {
            return no_selection(TeacherRankingStatus::InvalidInput, std::move(base));
        }

        for (std::size_t stage_index = 0; stage_index < stages.stage_evaluations.size();
             ++stage_index) {
            if (!stages.stage_evaluations[stage_index].has_value()) {
                continue;
            }
            const auto& stage = *stages.stage_evaluations[stage_index];
            if (!stage_shape_is_valid(candidates, stage)) {
                return no_selection(TeacherRankingStatus::InvalidInput, std::move(base));
            }
            switch (stage_readiness(stage)) {
            case StageReadiness::Invalid:
                return no_selection(TeacherRankingStatus::InvalidInput, std::move(base));
            case StageReadiness::Blocked:
                return no_selection(TeacherRankingStatus::Blocked, std::move(base));
            case StageReadiness::Unsupported:
                continue;
            case StageReadiness::Total: {
                if (!apply_stage(base, candidates, stage)) {
                    return no_selection(TeacherRankingStatus::InvalidInput, std::move(base));
                }
                std::size_t best_index = 0;
                for (std::size_t index = 1; index < base.size(); ++index) {
                    if (better_evaluation(base[index], base[best_index])) {
                        best_index = index;
                    }
                }
                return selected_result(std::move(base), best_index,
                                       static_cast<TeacherFallbackLevel>(stage_index));
            }
            }
        }

        ScoreVector zero;
        for (auto& evaluation : base) {
            evaluation.status = CandidateEvaluationStatus::Supported;
            evaluation.score = zero;
        }
        std::size_t best_index = 0;
        for (std::size_t index = 1; index < base.size(); ++index) {
            if (base[index].public_action_key < base[best_index].public_action_key) {
                best_index = index;
            }
        }
        return selected_result(std::move(base), best_index, TeacherFallbackLevel::F4);
    } catch (...) {
        try {
            return no_selection(TeacherRankingStatus::InvalidInput, base_evaluations(candidates));
        } catch (...) {
            return TeacherRankingResult{};
        }
    }
}

}  // namespace ygo::teacher
