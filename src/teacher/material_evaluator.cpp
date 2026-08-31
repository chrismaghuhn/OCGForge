#include "ygo/teacher/material_evaluator.hpp"

namespace ygo::teacher {

PublicEvaluatorOutcome MaterialEvaluator::evaluate(
    const CandidateFeatures& candidate,
    const PublicFactSnapshot& public_facts) const noexcept {
    (void)public_facts;
    PublicEvaluatorOutcome result;
    result.public_action_key = candidate.public_action_key;
    if (!candidate.amount.has_value()) {
        return result;
    }
    // The generic public descriptor does not prove that amount means an LP
    // expenditure or material cost. In particular, ASSIGN_AMOUNT is a
    // counter-allocation quantity, so no resource score is inferred here.
    result.status = CandidateEvaluationStatus::Unsupported;
    return result;
}

}  // namespace ygo::teacher
