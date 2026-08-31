#include "ygo/teacher/tactical_evaluator.hpp"

namespace ygo::teacher {

PublicEvaluatorOutcome TacticalEvaluator::evaluate(
    const CandidateFeatures& candidate,
    const PublicFactSnapshot& public_facts) const noexcept {
    (void)public_facts;
    PublicEvaluatorOutcome result;
    result.public_action_key = candidate.public_action_key;
    if (candidate.action_kind != environment::EnvironmentActionKind::BattleCommand) {
        return result;
    }
    // Opponent life-point presence does not prove tactical necessity or a
    // lethal/survival class for this candidate.
    result.status = CandidateEvaluationStatus::Unsupported;
    return result;
}

}  // namespace ygo::teacher
