#include "ygo/teacher/strategy_state.hpp"

#include <cstddef>
#include <utility>

#include "strategy_state_common.hpp"

namespace ygo::teacher {
std::optional<StrategyReconciliationResult> reconcile_strategy_state_with_evidence(
    const EpisodeLocalStrategyStateV1& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept {
    return internal::reconcile_strategy_state_with_evidence_impl<
        EpisodeLocalStrategyStateV1, StrategyReconciliationResult>(
        state, owning_participant, current_observation, validate_strategy_state);
}

std::optional<EpisodeLocalStrategyStateV1> reconcile_strategy_state(
    const EpisodeLocalStrategyStateV1& state,
    const std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept {
    return internal::reconcile_strategy_state_impl<EpisodeLocalStrategyStateV1,
                                                   StrategyReconciliationResult>(
        state, owning_participant, current_observation, validate_strategy_state);
}

}  // namespace ygo::teacher
