#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "ygo/trajectory/policy_provenance.hpp"

namespace trajectory_test {

using namespace ygo::trajectory;

inline ProvenanceRegistration registry_entry(const ProvenanceKind kind,
                                             std::string identity) {
    ProvenanceRegistration entry;
    entry.kind = kind;
    entry.identity = std::move(identity);
    return entry;
}

inline ProvenanceResolver test_provenance_resolver() {
    std::vector<ProvenanceRegistration> entries;
    entries.push_back(registry_entry(ProvenanceKind::ProducerImplementation,
                                      "ocgforge.test.producer.v1"));
    entries.push_back(registry_entry(ProvenanceKind::InferenceAdapter,
                                      "ocgforge.test.inference.v1"));
    entries.push_back(registry_entry(ProvenanceKind::ObservationAdapter,
                                      "ocgforge.test.observation.v1"));
    entries.push_back(registry_entry(ProvenanceKind::ActionAdapter,
                                      "ocgforge.test.action.v1"));
    entries.push_back(registry_entry(
        ProvenanceKind::ModelCheckpointArtifact,
        "model.v1." + std::string(64, 'b')));
    entries.push_back(registry_entry(ProvenanceKind::SearchContract,
                                     "ocgforge.test.search.v1"));
    entries.push_back(registry_entry(
        ProvenanceKind::DemonstrationSourceArtifact,
        "demonstration.v1." + std::string(64, 'd')));
    entries.push_back(registry_entry(
        ProvenanceKind::ArtifactMetadataArtifact,
        "metadata.v1." + std::string(64, 'a')));

    auto deterministic_sampling = registry_entry(
        ProvenanceKind::SamplingContract, "ocgforge.test.deterministic_sampling.v1");
    deterministic_sampling.sampling_capabilities = SamplingContractCapabilities{true, true};
    entries.push_back(std::move(deterministic_sampling));

    auto random_sampling = registry_entry(ProvenanceKind::SamplingContract,
                                          "ocgforge.test.random_sampling.v1");
    random_sampling.sampling_capabilities = SamplingContractCapabilities{true, false};
    entries.push_back(std::move(random_sampling));

    auto incomplete_sampling = registry_entry(ProvenanceKind::SamplingContract,
                                               "ocgforge.test.incomplete_sampling.v1");
    incomplete_sampling.sampling_capabilities = SamplingContractCapabilities{false, false};
    entries.push_back(std::move(incomplete_sampling));

    auto rng = registry_entry(ProvenanceKind::PolicyRngContract, "ocgforge.test.rng.v1");
    PolicyRngContractDescriptor rng_descriptor;
    rng_descriptor.initialization_material_is_canonical =
        [](const std::vector<std::uint8_t>& material) {
            return material == std::vector<std::uint8_t>{0x11, 0x22, 0x33};
        };
    rng_descriptor.state_is_canonical = [](const std::vector<std::uint8_t>& state) {
        return state == std::vector<std::uint8_t>{1, 2} ||
               state == std::vector<std::uint8_t>{3, 4};
    };
    rng_descriptor.cursor_is_unique = [](const PolicyRngInitializationIdentity& identity) {
        return identity.policy_rng_stream_id == "cursor-unique";
    };
    rng.policy_rng_descriptor = std::move(rng_descriptor);
    entries.push_back(std::move(rng));

    entries.push_back(registry_entry(ProvenanceKind::PolicyRngContract,
                                      kNoPolicyRngContractId));
    return ProvenanceResolver(std::move(entries));
}

}  // namespace trajectory_test
