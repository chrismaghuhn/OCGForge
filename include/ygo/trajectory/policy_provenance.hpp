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

    // Contract identities are exact, locally recognized versioned contract
    // literals. They are not content-addressed files.
    bool can_resolve_contract(std::string_view identity) const noexcept;
    // Content identities are resolved only from the explicit immutable
    // registry supplied to this resolver, and an exact contract literal is
    // never accepted as content. No mutable alias or online lookup is
    // permitted.
    bool can_resolve_content(std::string_view identity) const noexcept;
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
