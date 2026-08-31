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
    // Visibility is a public feature, but it does not prove target value or
    // threat semantics for a generic evaluator.
    result.status = CandidateEvaluationStatus::Unsupported;
    return result;
}

}  // namespace ygo::teacher
