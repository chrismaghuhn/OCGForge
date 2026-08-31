#pragma once

#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/teacher/candidate_evaluator.hpp"

namespace ygo::teacher {

TeacherRankingResult resolve_teacher_ranking(
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const CandidateEvaluator& evaluator) noexcept;

}  // namespace ygo::teacher
