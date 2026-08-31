#include "ygo/teacher/material_evaluator.hpp"

#include <cstdint>
#include <limits>

namespace ygo::teacher {

PublicEvaluatorOutcome MaterialEvaluator::evaluate(
    const CandidateFeatures& candidate,
    const PublicFactSnapshot& public_facts) const noexcept {
    PublicEvaluatorOutcome result;
    result.public_action_key = candidate.public_action_key;
    if (!candidate.amount.has_value()) {
        return result;
    }
    const auto self_life_points = public_facts.value("public.life_points.self");
    if (!self_life_points.has_value() || self_life_points->value_kind != PublicFactValueKind::U64) {
        result.status = CandidateEvaluationStatus::Unsupported;
        return result;
    }

    const auto amount = static_cast<std::int64_t>(*candidate.amount);
    const auto magnitude = amount < 0 ? -amount : amount;
    if (magnitude > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
        result.status = CandidateEvaluationStatus::Invalid;
        return result;
    }
    if (static_cast<std::uint64_t>(magnitude) > self_life_points->u64_value) {
        result.status = CandidateEvaluationStatus::Unsupported;
        return result;
    }

    // The public candidate amount and public life-point budget are only a
    // bounded cost signal; this evaluator does not claim legality.
    result.status = CandidateEvaluationStatus::Supported;
    result.contributions.push_back(
        {ScoreDimension::ResourcePreservationAndCost,
         -static_cast<std::int32_t>(magnitude)});
    return result;
}

}  // namespace ygo::teacher
