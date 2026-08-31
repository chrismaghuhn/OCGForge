#include "ygo/teacher/interaction_evaluator.hpp"

namespace ygo::teacher {

PublicEvaluatorOutcome InteractionEvaluator::evaluate(
    const CandidateFeatures& candidate,
    const PublicFactSnapshot& public_facts) const noexcept {
    (void)public_facts;
    PublicEvaluatorOutcome result;
    result.public_action_key = candidate.public_action_key;
    if (candidate.action_kind != environment::EnvironmentActionKind::Chain &&
        !candidate.has_continuation_operation) {
        return result;
    }
    // Chain length is public context, but by itself does not prove the
    // strategic timing value of this interaction.
    result.status = CandidateEvaluationStatus::Unsupported;
    return result;
}

}  // namespace ygo::teacher
