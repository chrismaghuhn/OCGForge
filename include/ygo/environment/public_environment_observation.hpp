#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/observation/player_observation.hpp"

namespace ygo::environment {

inline constexpr std::string_view kPublicEnvironmentObservationSchemaId =
    "ocgforge.public_environment_observation.v1";

// Only these decision-context fields are policy-facing. The v1
// PlayerObservation remains an observation-layer record and may contain
// internal decision_id/continuation_id values; they are deliberately not
// copied by project_public_observation.
struct PublicObservationDecisionContext final {
    std::optional<std::string> kind;
    std::optional<std::uint8_t> player;
    std::vector<ygo::observation::ObservationLocator> referenced_entities;
};

// canonical_public_state_bytes must be produced by a separate serializer over
// the perspective-safe state fields. It must not be the canonical v1
// PlayerObservation serialization or its observation_hash-bearing form.
struct PublicEnvironmentObservationInput final {
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::string canonical_public_state_bytes;
    PublicObservationDecisionContext decision_context;
};

// Copies only safe context metadata from an attached PlayerObservation. The
// state bytes remain explicit so this prerequisite cannot silently publish the
// internal PlayerObservation serializer as the public observation schema.
PublicEnvironmentObservationInput project_public_observation(
    const ygo::observation::PlayerObservation& observation,
    std::string canonical_public_state_bytes);

std::vector<std::uint8_t> canonical_public_environment_observation_bytes(
    const PublicEnvironmentObservationInput& input);
std::string public_observation_digest(const PublicEnvironmentObservationInput& input);

}  // namespace ygo::environment
