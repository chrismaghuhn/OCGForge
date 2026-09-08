#pragma once

#include <optional>
#include <string>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_core_v2.hpp"
#include "ygo/teacher/teacher_decision_v2.hpp"
#include "ygo/trajectory/types.hpp"

namespace ygo::policy {

teacher::TeacherPolicyBindingV1 make_teacher_policy_binding_v2(
    const teacher::StrategyProfileV1& profile);

trajectory::PolicyArtifact make_teacher_policy_artifact_v2(
    const teacher::StrategyProfileV1& profile);

struct TeacherPolicySessionV2;
struct TeacherPolicySessionCreateResultV2;

class DeterministicTeacherPolicyV2 final {
public:
    DeterministicTeacherPolicyV2(const DeterministicTeacherPolicyV2&) = delete;
    DeterministicTeacherPolicyV2& operator=(const DeterministicTeacherPolicyV2&) = delete;
    DeterministicTeacherPolicyV2(DeterministicTeacherPolicyV2&&) = default;
    DeterministicTeacherPolicyV2& operator=(DeterministicTeacherPolicyV2&&) = default;

    PolicySelection select(const PolicyInput& input) noexcept;
    bool commit(const environment::AcceptedActionTransition& accepted_transition) noexcept;
    void reject_pending_proposal() noexcept;

    const teacher::EpisodeLocalStrategyStateV2& state() const noexcept { return state_; }
    const teacher::StrategyProfileV1& profile() const noexcept { return profile_; }
    const teacher::TeacherPolicyBindingV1& policy_binding() const noexcept {
        return policy_binding_;
    }
    std::uint8_t participant() const noexcept { return participant_; }
    const std::string& participant_policy_assignment_id() const noexcept {
        return participant_policy_assignment_id_;
    }
    bool has_pending_proposal() const noexcept { return pending_.has_value(); }
    std::optional<teacher::TeacherRankingResultV2> pending_ranking_result() const {
        if (!pending_.has_value()) {
            return std::nullopt;
        }
        return pending_->ranking;
    }

private:
    struct PendingProposal final {
        environment::PublicEnvironmentObservation observation;
        teacher::TeacherRankingResultV2 ranking;
        PolicySelectionResult selection;
    };

    DeterministicTeacherPolicyV2(teacher::StrategyProfileV1 profile,
                                 teacher::TeacherPolicyBindingV1 policy_binding,
                                 std::uint8_t participant,
                                 std::string participant_policy_assignment_id);

    static PolicySelection failure(PolicyErrorCode code, std::string message) noexcept;

    teacher::StrategyProfileV1 profile_;
    teacher::TeacherPolicyBindingV1 policy_binding_;
    teacher::EpisodeLocalStrategyStateV2 state_;
    std::uint8_t participant_ = 0;
    std::string participant_policy_assignment_id_;
    std::optional<PendingProposal> pending_;

    friend struct TeacherPolicySessionV2;
    friend struct TeacherPolicySessionCreateResultV2;
    friend TeacherPolicySessionCreateResultV2 create_teacher_policy_session_v2(
        const teacher::StrategyProfileV1& profile,
        const teacher::TeacherPolicyBindingV1& policy_binding,
        const trajectory::PolicyArtifact& artifact,
        const trajectory::ParticipantPolicyAssignment& assignment) noexcept;
};

struct TeacherPolicySessionV2 final {
    DeterministicTeacherPolicyV2 policy;
    trajectory::PolicyArtifact artifact;
    trajectory::ParticipantPolicyAssignment assignment;

    PolicyExecutionBinding execution_binding() const {
        PolicyExecutionBinding result;
        result.policy_artifact_id = artifact.policy_artifact_id;
        result.participant_policy_assignment_id =
            assignment.participant_policy_assignment_id;
        result.policy_rng_contract_identity = trajectory::kNoPolicyRngContractId;
        result.policy_rng_stream_id = trajectory::kNoPolicyRngContractId;
        result.policy_rng_initialization_identity = trajectory::kNoPolicyRngContractId;
        result.policy_rng_identity = trajectory::kNoPolicyRngContractId;
        return result;
    }
};

struct TeacherPolicySessionCreateResultV2 final {
    std::optional<TeacherPolicySessionV2> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

TeacherPolicySessionCreateResultV2 create_teacher_policy_session_v2(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment) noexcept;

}  // namespace ygo::policy
