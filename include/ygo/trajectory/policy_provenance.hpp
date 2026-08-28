#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ygo/trajectory/types.hpp"

namespace ygo::trajectory {

struct PolicyProvenanceError final {
    std::string message;
};

// Validates identities and cross-references without resolving mutable external
// content. Callers may provide an explicit immutable fixture registry for
// content-addressed artifacts; an empty registry proves only exact contract
// identities and structural relationships.
class ProvenanceResolver final {
public:
    ProvenanceResolver() = default;
    explicit ProvenanceResolver(std::vector<std::string> immutable_content_ids);

    bool can_resolve(std::string_view identity) const noexcept;
    bool validate(const PolicyProvenanceEnvelope& value,
                  const environment::CertifiedEnvironmentConfig& config,
                  const environment::EpisodeSpec& spec,
                  std::string* error = nullptr) const;

private:
    std::vector<std::string> immutable_content_ids_;
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

}  // namespace ygo::trajectory
