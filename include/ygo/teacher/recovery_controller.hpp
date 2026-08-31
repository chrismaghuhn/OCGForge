#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/teacher/goal_line_controller.hpp"

namespace ygo::teacher {

struct RecoverySelection final {
    PredicateEvaluationStatus status = PredicateEvaluationStatus::False;
    std::optional<std::string> recovery_edge_id;
    std::optional<std::string> target_goal_id;
    std::optional<std::string> target_line_id;

    bool operator==(const RecoverySelection& other) const noexcept {
        return status == other.status && recovery_edge_id == other.recovery_edge_id &&
               target_goal_id == other.target_goal_id && target_line_id == other.target_line_id;
    }
    bool operator!=(const RecoverySelection& other) const noexcept {
        return !(*this == other);
    }
};

RecoverySelection select_recovery_edge(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& pre_reconciliation_state,
    const environment::PublicEnvironmentObservation& current_observation,
    std::uint8_t owning_participant) noexcept;

}  // namespace ygo::teacher
