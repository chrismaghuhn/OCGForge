#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/predicate_registry.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/strategy_state_v3.hpp"

namespace ygo::teacher {

struct RecoverySelection;

struct GoalLineSelection final {
    PredicateEvaluationStatus status = PredicateEvaluationStatus::False;
    std::optional<std::string> goal_id;
    std::optional<std::string> line_id;
    std::vector<std::string> ready_node_ids;

    bool operator==(const GoalLineSelection& other) const noexcept {
        return status == other.status && goal_id == other.goal_id &&
               line_id == other.line_id && ready_node_ids == other.ready_node_ids;
    }
    bool operator!=(const GoalLineSelection& other) const noexcept {
        return !(*this == other);
    }
};

PredicateEvaluationStatus evaluate_public_predicate_conjunction(
    const std::vector<PredicateRef>& predicates,
    const PublicFactSnapshot& public_facts,
    const StrategyProfileV1& profile) noexcept;

PredicateEvaluationStatus evaluate_resource_requirement(
    const ResourceRequirement& requirement,
    const StrategyProfileV1& profile,
    const PublicFactSnapshot& public_facts) noexcept;

GoalLineSelection select_goal_and_line(const StrategyProfileV1& profile,
                                       const EpisodeLocalStrategyStateV1& state,
                                       const PublicFactSnapshot& public_facts) noexcept;

GoalLineSelection select_goal_and_line_v2(const StrategyProfileV1& profile,
                                          const EpisodeLocalStrategyStateV2& state,
                                          const PublicFactSnapshot& public_facts) noexcept;

GoalLineSelection select_goal_and_line_v3(const StrategyProfileV1& profile,
                                          const EpisodeLocalStrategyStateV3& state,
                                          const PublicFactSnapshot& public_facts) noexcept;

PredicateEvaluationStatus match_candidate_intent_set(
    const StrategyProfileV1& profile,
    const std::vector<std::string>& intent_ids,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    std::uint8_t owning_participant,
    std::vector<std::string>& matched_ids) noexcept;

PredicateEvaluationStatus evaluate_node_completion(
    const LineNode& node,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept;

PredicateEvaluationStatus evaluate_goal_completion(
    const GoalDefinition& goal,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept;

PredicateEvaluationStatus evaluate_node_completion_v2(
    const LineNode& node,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept;

PredicateEvaluationStatus evaluate_node_completion_v3(
    const LineNode& node,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept;

PredicateEvaluationStatus evaluate_goal_completion_v2(
    const GoalDefinition& goal,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept;

PredicateEvaluationStatus evaluate_goal_completion_v3(
    const GoalDefinition& goal,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept;

PublicEvaluatorOutcome evaluate_goal_line_progress(
    const StrategyProfileV1& profile,
    const GoalLineSelection& selection,
    const RecoverySelection& recovery,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    std::uint8_t owning_participant) noexcept;

PublicEvaluatorOutcome evaluate_goal_line_progress_v2(
    const StrategyProfileV1& profile,
    const GoalLineSelection& selection,
    const RecoverySelection& recovery,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    std::uint8_t owning_participant,
    bool reconciled_continuation_commitment) noexcept;

PublicEvaluatorOutcome evaluate_goal_line_progress_v3(
    const StrategyProfileV1& profile,
    const GoalLineSelection& selection,
    const RecoverySelection& recovery,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    std::uint8_t owning_participant,
    bool reconciled_continuation_commitment) noexcept;

RecoverySelection select_recovery_edge_v2(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV2& pre_reconciliation_state,
    const environment::PublicEnvironmentObservation& current_observation,
    std::uint8_t owning_participant) noexcept;

RecoverySelection select_recovery_edge_v3(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV3& pre_reconciliation_state,
    const environment::PublicEnvironmentObservation& current_observation,
    std::uint8_t owning_participant) noexcept;

}  // namespace ygo::teacher
