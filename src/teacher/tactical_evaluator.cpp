#include "ygo/teacher/tactical_evaluator.hpp"

namespace ygo::teacher {

PublicEvaluatorOutcome TacticalEvaluator::evaluate(
    const CandidateFeatures& candidate,
    const PublicFactSnapshot& public_facts) const noexcept {
    PublicEvaluatorOutcome result;
    result.public_action_key = candidate.public_action_key;
    if (candidate.action_kind != environment::EnvironmentActionKind::BattleCommand) {
        return result;
    }
    const auto life_points = public_facts.value("public.life_points.opponent");
    if (!life_points.has_value() || life_points->value_kind != PublicFactValueKind::U64) {
        result.status = CandidateEvaluationStatus::Unsupported;
        return result;
    }
    result.status = CandidateEvaluationStatus::Supported;
    result.contributions.push_back(
        {ScoreDimension::ImmediateTacticalNecessity,
         life_points->u64_value == 0 ? 0 : 1});
    return result;
}

}  // namespace ygo::teacher
