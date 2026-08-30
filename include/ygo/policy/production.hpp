#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "ygo/policy/production_provenance.hpp"
#include "ygo/trajectory/types.hpp"

namespace ygo::policy {

trajectory::PolicyArtifact make_random_legal_policy_artifact();

std::vector<trajectory::ParticipantPolicyAssignment>
make_random_legal_participant_assignments(
    const trajectory::PolicyArtifact& artifact,
    const environment::CertifiedEnvironmentConfig& config,
    environment::SeatAssignment seat_assignment,
    std::uint8_t starting_player,
    const std::array<trajectory::PolicyRole, 2>& policy_roles);

}  // namespace ygo::policy
