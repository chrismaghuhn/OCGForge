#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/predicate_registry.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"

namespace ygo::teacher {

struct RecoverySelection;

struct PreReconciliationPlanContext final {
public:
    const std::optional<std::string>& pre_active_goal_id() const noexcept {
        return pre_active_goal_id_;
    }
    const std::optional<std::string>& pre_active_line_id() const noexcept {
        return pre_active_line_id_;
    }
    const std::vector<std::string>& pre_ready_node_ids() const noexcept {
        return pre_ready_node_ids_;
    }

    bool operator==(const PreReconciliationPlanContext& other) const noexcept {
        return pre_active_goal_id_ == other.pre_active_goal_id_ &&
               pre_active_line_id_ == other.pre_active_line_id_ &&
               pre_ready_node_ids_ == other.pre_ready_node_ids_;
    }
    bool operator!=(const PreReconciliationPlanContext& other) const noexcept {
        return !(*this == other);
    }

private:
    PreReconciliationPlanContext(std::optional<std::string> active_goal_id,
                                 std::optional<std::string> active_line_id,
                                 std::vector<std::string> ready_node_ids)
        : pre_active_goal_id_(std::move(active_goal_id)),
          pre_active_line_id_(std::move(active_line_id)),
          pre_ready_node_ids_(std::move(ready_node_ids)) {}

    std::optional<std::string> pre_active_goal_id_;
    std::optional<std::string> pre_active_line_id_;
    std::vector<std::string> pre_ready_node_ids_;

    friend std::optional<PreReconciliationPlanContext>
    derive_pre_reconciliation_plan_context(const StrategyProfileV1&,
                                           const EpisodeLocalStrategyStateV1&) noexcept;
};

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

std::optional<PreReconciliationPlanContext> derive_pre_reconciliation_plan_context(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& state) noexcept;

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

PredicateEvaluationStatus match_candidate_intent_set(
    const StrategyProfileV1& profile,
    const std::vector<std::string>& intent_ids,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    std::uint8_t owning_participant,
    std::vector<std::string>& matched_ids) noexcept;

PredicateEvaluationStatus evaluate_node_completion(
    // The accepted transition and subsequent observation are intentionally
    // required here; a bare boolean or caller-supplied fact snapshot cannot
    // establish the same-participant later-frame contract.
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

PublicEvaluatorOutcome evaluate_goal_line_progress(
    const StrategyProfileV1& profile,
    const GoalLineSelection& selection,
    const RecoverySelection& recovery,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    std::uint8_t owning_participant) noexcept;

}  // namespace ygo::teacher
