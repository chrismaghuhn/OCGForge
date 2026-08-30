#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/observation/observed_zone.hpp"

namespace ygo::observation {
struct PlayerObservation;
}

namespace ygo::environment {

inline constexpr std::string_view kPublicEnvironmentObservationSchemaId =
    "ocgforge.public_environment_observation.v1";
inline constexpr std::string_view kPublicSafeStateSchemaId =
    "ocgforge.public_safe_state.v1";

// Only these decision-context fields are policy-facing. The v1
// PlayerObservation remains an observation-layer record and may contain
// internal decision_id/continuation_id values; they are deliberately not
// copied by project_public_observation.
struct PublicObservationDecisionContext final {
    std::optional<std::string> kind;
    std::optional<std::uint8_t> player;
    std::vector<ygo::observation::ObservationLocator> referenced_entities;
};

// canonical_safe_state_bytes is an opaque value produced by
// canonical_public_safe_state_bytes(PlayerObservation). It is intentionally
// private so callers cannot replace it with canonical_serialize(PlayerObservation)
// or arbitrary text after projection.
struct PublicEnvironmentObservationInput final {
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    PublicObservationDecisionContext decision_context;

    const std::vector<std::uint8_t>& canonical_safe_state_bytes() const noexcept {
        return canonical_safe_state_bytes_;
    }

private:
    std::vector<std::uint8_t> canonical_safe_state_bytes_;

    friend PublicEnvironmentObservationInput project_public_observation(
        const ygo::observation::PlayerObservation& observation);
    friend bool decode_canonical_public_environment_observation(
        const std::vector<std::uint8_t>& bytes,
        PublicEnvironmentObservationInput& output) noexcept;
};

// Serializes exactly the perspective-safe state owned by PlayerObservation.
// Internal observation metadata and attached decision identity are excluded.
std::vector<std::uint8_t> canonical_public_safe_state_bytes(
    const ygo::observation::PlayerObservation& observation);

// Builds the policy-facing observation, including the canonical safe-state
// bytes generated above and only safe attached decision-context metadata.
PublicEnvironmentObservationInput project_public_observation(
    const ygo::observation::PlayerObservation& observation);

std::vector<std::uint8_t> canonical_public_environment_observation_bytes(
    const PublicEnvironmentObservationInput& input);
std::string public_observation_digest(const PublicEnvironmentObservationInput& input);

// Strictly decodes the V1 public-observation container. The safe-state bytes
// remain opaque and are accepted only when they are a canonical V1 safe-state
// serialization; this helper never constructs or exposes a private
// PlayerObservation.
bool decode_canonical_public_environment_observation(
    const std::vector<std::uint8_t>& bytes,
    PublicEnvironmentObservationInput& output) noexcept;

}  // namespace ygo::environment
