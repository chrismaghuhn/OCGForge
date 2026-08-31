#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/teacher_decision.hpp"

namespace ygo::teacher {

struct TeacherFallbackCandidateValue final {
    // This is stage-local value data aligned by position with the supplied
    // candidate vector for evidence preservation only. The public key is the
    // identity; this is not a second CandidateEvaluation record.
    std::string public_action_key;
    CandidateEvaluationStatus status = CandidateEvaluationStatus::Unsupported;
    std::optional<ScoreVector> score;
    std::vector<EvaluatorScoreContribution> contributions;
    std::vector<std::string> matched_intent_ids;
    std::vector<std::string> matched_goal_ids;
    std::vector<std::string> matched_line_ids;
    std::vector<std::string> reason_ids;
};

struct TeacherFallbackStageSet final {
    // Fixed positions are F0, F1, F2, and F3. F4 is the resolver-owned
    // public-key completion and is never supplied by a caller.
    std::array<std::optional<std::vector<TeacherFallbackCandidateValue>>, 4>
        stage_evaluations;
};

TeacherRankingResult resolve_teacher_fallback(
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const TeacherFallbackStageSet& stages) noexcept;

}  // namespace ygo::teacher
