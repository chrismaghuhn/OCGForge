#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/trajectory/types.hpp"

namespace ygo::trajectory {

struct PolicyProvenanceError final {
    std::string message;
};

enum class ProvenanceKind : std::uint8_t {
    ProducerImplementation = 0,
    InferenceAdapter = 1,
    ObservationAdapter = 2,
    ActionAdapter = 3,
    SamplingContract = 4,
    PolicyRngContract = 5,
    SearchContract = 6,
    ImmutableContentArtifact = 7,
    ModelCheckpointArtifact = 8,
    DemonstrationSourceArtifact = 9,
    ArtifactMetadataArtifact = 10,
};

struct SamplingContractCapabilities final {
    bool complete = false;
    bool deterministic = false;
};

struct PolicyRngContractDescriptor final {
    // These callbacks are immutable authority supplied with the registry. A
    // missing callback proves that the corresponding opaque bytes are not
    // admissible under the declared RNG contract.
    std::function<bool(const std::vector<std::uint8_t>&)> initialization_material_is_canonical;
    std::function<bool(const std::vector<std::uint8_t>&)> state_is_canonical;
    std::function<bool(const PolicyRngInitializationIdentity&)> cursor_is_unique;
};

struct ProvenanceRegistration final {
    ProvenanceKind kind = ProvenanceKind::ImmutableContentArtifact;
    std::string identity;
    std::optional<SamplingContractCapabilities> sampling_capabilities;
    std::optional<PolicyRngContractDescriptor> policy_rng_descriptor;
};

// Validates identities and cross-references against an immutable, explicitly
// typed authority registry. The default instance contains only the accepted
// no-RNG contract; it intentionally contains no policy implementation or test
// fixture identities.
class ProvenanceResolver final {
public:
    ProvenanceResolver();
    explicit ProvenanceResolver(std::vector<ProvenanceRegistration> registrations);

    bool can_resolve(ProvenanceKind kind, std::string_view identity) const noexcept;
    const SamplingContractCapabilities* sampling_contract_capabilities(
        std::string_view identity) const noexcept;
    const PolicyRngContractDescriptor* policy_rng_contract_descriptor(
        std::string_view identity) const noexcept;
    bool validate(const PolicyProvenanceEnvelope& value,
                  const environment::CertifiedEnvironmentConfig& config,
                  const environment::EpisodeSpec& spec,
                  std::string* error = nullptr) const;

private:
    std::vector<ProvenanceRegistration> registrations_;
};

bool validate_policy_provenance(const PolicyProvenanceEnvelope& value,
                                const environment::CertifiedEnvironmentConfig& config,
                                const environment::EpisodeSpec& spec,
                                const ProvenanceResolver& resolver,
                                std::string* error = nullptr);

bool validate_policy_rng_initialization_material(
    const PolicyRngInitializationIdentity& identity,
    const std::vector<std::uint8_t>& raw_material,
    std::string* error = nullptr);

bool validate_policy_rng_initialization_material(
    const PolicyRngInitializationIdentity& identity,
    const std::vector<std::uint8_t>& raw_material,
    const ProvenanceResolver& resolver,
    std::string* error = nullptr);

}  // namespace ygo::trajectory
