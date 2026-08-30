#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/policy/policy.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/random_legal.hpp"
#include "ygo/trajectory/types.hpp"

namespace ygo::policy {

trajectory::PolicyArtifact make_random_legal_policy_artifact();

struct RandomLegalExecutionBinding final {
    trajectory::PolicyRngInitializationIdentity initialization;
    trajectory::PolicyRngStreamIdentity stream;
    PolicyExecutionBinding execution_binding;
};

struct RandomLegalPolicySession final {
    RandomLegalPolicy policy;
    RandomLegalExecutionBinding binding;
};

struct RandomLegalPolicySessionCreateResult final {
    std::optional<RandomLegalPolicySession> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::vector<trajectory::ParticipantPolicyAssignment>
make_random_legal_participant_assignments(
    const trajectory::PolicyArtifact& artifact,
    const environment::CertifiedEnvironmentConfig& config,
    environment::SeatAssignment seat_assignment,
    std::uint8_t starting_player,
    const std::array<trajectory::PolicyRole, 2>& policy_roles);

RandomLegalExecutionBinding make_random_legal_execution_binding(
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment,
    std::uint64_t explicit_policy_rng_root_seed,
    std::string policy_rng_stream_id);

RandomLegalPolicySessionCreateResult create_random_legal_policy_session(
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment,
    std::uint64_t explicit_policy_rng_root_seed,
    std::string policy_rng_stream_id) noexcept;

}  // namespace ygo::policy
