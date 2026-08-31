#include "ygo/teacher/target_evaluator.hpp"

namespace ygo::teacher {

PublicEvaluatorOutcome TargetEvaluator::evaluate(
    const CandidateFeatures& candidate,
    const PublicFactSnapshot& public_facts) const noexcept {
    (void)public_facts;
    PublicEvaluatorOutcome result;
    result.public_action_key = candidate.public_action_key;
    if (candidate.target_is_redacted) {
        result.status = CandidateEvaluationStatus::Unsupported;
        return result;
    }
    if (!candidate.target_is_visible) {
        return result;
    }
    result.status = CandidateEvaluationStatus::Supported;
    result.contributions.push_back({ScoreDimension::PublicTargetValue, 1});
    return result;
}

}  // namespace ygo::teacher
