#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_core.hpp"
#include "ygo/trajectory/types.hpp"

namespace ygo::policy {

teacher::TeacherPolicyBindingV1 make_teacher_policy_binding(
    const teacher::StrategyProfileV1& profile);
trajectory::PolicyArtifact make_teacher_policy_artifact(
    const teacher::StrategyProfileV1& profile);

struct TeacherPolicySessionCreateResult;

std::vector<trajectory::ParticipantPolicyAssignment>
make_teacher_participant_assignments(
    const trajectory::PolicyArtifact& swordsoul_artifact,
    const trajectory::PolicyArtifact& salamangreat_artifact,
    const environment::CertifiedEnvironmentConfig& config,
    environment::SeatAssignment seat_assignment,
    std::uint8_t starting_player,
    const std::array<trajectory::PolicyRole, 2>& policy_roles);

class DeterministicTeacherPolicy final {
public:
    DeterministicTeacherPolicy(const DeterministicTeacherPolicy&) = delete;
    DeterministicTeacherPolicy& operator=(const DeterministicTeacherPolicy&) = delete;
    DeterministicTeacherPolicy(DeterministicTeacherPolicy&&) = default;
    DeterministicTeacherPolicy& operator=(DeterministicTeacherPolicy&&) = default;

    PolicySelection select(const PolicyInput& input) noexcept;

    bool commit(const environment::AcceptedActionTransition& accepted_transition) noexcept;
    void reject_pending_proposal() noexcept;

    const teacher::EpisodeLocalStrategyStateV1& state() const noexcept { return state_; }
    const teacher::StrategyProfileV1& profile() const noexcept { return profile_; }
    const teacher::TeacherPolicyBindingV1& policy_binding() const noexcept {
        return policy_binding_;
    }
    std::uint8_t participant() const noexcept { return participant_; }
    const std::string& participant_policy_assignment_id() const noexcept {
        return participant_policy_assignment_id_;
    }
    bool has_pending_proposal() const noexcept { return pending_.has_value(); }
    std::optional<teacher::TeacherRankingResult> pending_ranking_result() const {
        if (!pending_.has_value()) {
            return std::nullopt;
        }
        return pending_->ranking;
    }

private:
    struct PendingProposal final {
        environment::PublicEnvironmentObservation observation;
        teacher::TeacherRankingResult ranking;
        PolicySelectionResult selection;
    };

    DeterministicTeacherPolicy(teacher::StrategyProfileV1 profile,
                               teacher::TeacherPolicyBindingV1 policy_binding,
                               std::uint8_t participant,
                               std::string participant_policy_assignment_id);

    static PolicySelection failure(PolicyErrorCode code, std::string message) noexcept;

    teacher::StrategyProfileV1 profile_;
    teacher::TeacherPolicyBindingV1 policy_binding_;
    teacher::EpisodeLocalStrategyStateV1 state_;
    std::uint8_t participant_ = 0;
    std::string participant_policy_assignment_id_;
    std::optional<PendingProposal> pending_;

    friend struct TeacherPolicySession;
    friend struct TeacherPolicySessionCreateResult;
    friend struct TeacherPolicySessionFactory;
    friend TeacherPolicySessionCreateResult create_teacher_policy_session(
        const teacher::StrategyProfileV1& profile,
        const teacher::TeacherPolicyBindingV1& policy_binding,
        const trajectory::PolicyArtifact& artifact,
        const trajectory::ParticipantPolicyAssignment& assignment) noexcept;
};

struct TeacherPolicySession final {
    DeterministicTeacherPolicy policy;
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

struct TeacherPolicySessionCreateResult final {
    std::optional<TeacherPolicySession> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

TeacherPolicySessionCreateResult create_teacher_policy_session(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment) noexcept;

}  // namespace ygo::policy
