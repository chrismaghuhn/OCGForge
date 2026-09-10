#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/strategy_profile.hpp"

namespace ygo::environment {
struct AcceptedActionTransition;
struct StepRejected;
}

namespace ygo::teacher {

struct TeacherRankingResultV3;

struct EpisodeLocalStrategyStateV3 final {
    std::string strategy_profile_id;
    std::optional<std::string> active_goal_id;
    std::optional<std::string> active_line_id;
    std::vector<std::string> completed_line_node_ids;
    std::vector<std::string> achieved_goal_ids;
    std::vector<PublicFactValue> public_resource_facts;
    std::vector<PublicFactValue> public_restriction_facts;
    std::vector<PublicFactValue> public_threat_facts;
    std::optional<std::uint64_t> last_accepted_decision_index;
    std::optional<std::string> last_accepted_public_action_key;

    bool operator==(const EpisodeLocalStrategyStateV3& other) const noexcept {
        return strategy_profile_id == other.strategy_profile_id &&
               active_goal_id == other.active_goal_id && active_line_id == other.active_line_id &&
               completed_line_node_ids == other.completed_line_node_ids &&
               achieved_goal_ids == other.achieved_goal_ids &&
               public_resource_facts == other.public_resource_facts &&
               public_restriction_facts == other.public_restriction_facts &&
               public_threat_facts == other.public_threat_facts &&
               last_accepted_decision_index == other.last_accepted_decision_index &&
               last_accepted_public_action_key == other.last_accepted_public_action_key;
    }
    bool operator!=(const EpisodeLocalStrategyStateV3& other) const noexcept {
        return !(*this == other);
    }
};

struct TeacherStateDeltaV3 final {
    std::string strategy_profile_id;
    std::optional<std::uint64_t> base_last_accepted_decision_index;
    std::optional<std::string> base_last_accepted_public_action_key;
    std::string proposed_for_public_action_key;
    std::optional<std::string> active_goal_id;
    std::optional<std::string> active_line_id;
    std::vector<std::string> completed_line_node_ids;
    std::vector<std::string> achieved_goal_ids;
    std::vector<PublicFactValue> public_resource_facts;
    std::vector<PublicFactValue> public_restriction_facts;
    std::vector<PublicFactValue> public_threat_facts;
    std::vector<std::string> invalidation_reason_ids;

    bool operator==(const TeacherStateDeltaV3& other) const noexcept {
        return strategy_profile_id == other.strategy_profile_id &&
               base_last_accepted_decision_index == other.base_last_accepted_decision_index &&
               base_last_accepted_public_action_key == other.base_last_accepted_public_action_key &&
               proposed_for_public_action_key == other.proposed_for_public_action_key &&
               active_goal_id == other.active_goal_id && active_line_id == other.active_line_id &&
               completed_line_node_ids == other.completed_line_node_ids &&
               achieved_goal_ids == other.achieved_goal_ids &&
               public_resource_facts == other.public_resource_facts &&
               public_restriction_facts == other.public_restriction_facts &&
               public_threat_facts == other.public_threat_facts &&
               invalidation_reason_ids == other.invalidation_reason_ids;
    }
    bool operator!=(const TeacherStateDeltaV3& other) const noexcept {
        return !(*this == other);
    }
};

struct StrategyReconciliationResultV3 final {
    EpisodeLocalStrategyStateV3 state;
    std::vector<std::string> invalidation_reason_ids;

    bool operator==(const StrategyReconciliationResultV3& other) const noexcept {
        return state == other.state && invalidation_reason_ids == other.invalidation_reason_ids;
    }
    bool operator!=(const StrategyReconciliationResultV3& other) const noexcept {
        return !(*this == other);
    }
};

bool validate_strategy_state_v3(const EpisodeLocalStrategyStateV3& state) noexcept;
bool validate_teacher_state_delta_v3(const TeacherStateDeltaV3& delta) noexcept;

std::optional<EpisodeLocalStrategyStateV3> reset_strategy_state_v3(
    const StrategyProfileV1& validated_profile) noexcept;

std::optional<TeacherStateDeltaV3> propose_teacher_state_delta_v3(
    const EpisodeLocalStrategyStateV3& current_state,
    const environment::PublicEnvironmentObservation& current_observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& validated_profile,
    const TeacherStateDeltaV3& requested_replacement) noexcept;

std::optional<EpisodeLocalStrategyStateV3> reconcile_strategy_state_v3(
    const EpisodeLocalStrategyStateV3& state,
    std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept;

std::optional<StrategyReconciliationResultV3>
reconcile_strategy_state_with_evidence_v3(
    const EpisodeLocalStrategyStateV3& state,
    std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& current_observation) noexcept;

bool commit_teacher_state_delta_v3(
    EpisodeLocalStrategyStateV3& current_state,
    const TeacherRankingResultV3& ranking_result,
    const StrategyProfileV1& validated_profile,
    std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition) noexcept;

std::optional<StrategyReconciliationResultV3>
commit_teacher_state_delta_with_evidence_v3(
    EpisodeLocalStrategyStateV3& current_state,
    const TeacherRankingResultV3& ranking_result,
    const StrategyProfileV1& validated_profile,
    std::uint8_t owning_participant,
    const environment::PublicEnvironmentObservation& proposal_observation,
    const environment::AcceptedActionTransition& accepted_transition) noexcept;

bool observe_step_rejected_v3(EpisodeLocalStrategyStateV3& state,
                              const environment::StepRejected& rejection) noexcept;

}  // namespace ygo::teacher
