#include "ygo/teacher/interaction_evaluator.hpp"

#include <cstdint>
#include <limits>

namespace ygo::teacher {

PublicEvaluatorOutcome InteractionEvaluator::evaluate(
    const CandidateFeatures& candidate,
    const PublicFactSnapshot& public_facts) const noexcept {
    PublicEvaluatorOutcome result;
    result.public_action_key = candidate.public_action_key;
    if (candidate.action_kind != environment::EnvironmentActionKind::Chain &&
        !candidate.has_continuation_operation) {
        return result;
    }
    const auto chain_length = public_facts.value("public.chain.length");
    if (!chain_length.has_value() || chain_length->value_kind != PublicFactValueKind::U64) {
        result.status = CandidateEvaluationStatus::Unsupported;
        return result;
    }
    if (chain_length->u64_value >
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        result.status = CandidateEvaluationStatus::Invalid;
        return result;
    }
    result.status = CandidateEvaluationStatus::Supported;
    result.contributions.push_back(
        {ScoreDimension::InteractionTiming,
         static_cast<std::int32_t>(chain_length->u64_value)});
    return result;
}

}  // namespace ygo::teacher
