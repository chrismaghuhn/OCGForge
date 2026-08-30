#include "ygo/policy/production_provenance.hpp"

#include <string>
#include <utility>
#include <vector>

#include "ygo/policy/rng.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::policy {
namespace {

trajectory::ProvenanceRegistration registration(
    const trajectory::ProvenanceKind kind, const std::string_view identity) {
    trajectory::ProvenanceRegistration result;
    result.kind = kind;
    result.identity = std::string(identity);
    return result;
}

bool canonical_initialization_material(
    const std::vector<std::uint8_t>& raw_material) noexcept {
    return static_cast<bool>(decode_canonical_policy_rng_initialization_material(raw_material));
}

bool unique_initialization_identity(
    const trajectory::PolicyRngInitializationIdentity& identity) noexcept {
    if (identity.policy_rng_contract_identity != kSha256CounterPolicyRngContractIdentity) {
        return false;
    }
    const auto decoded = decode_canonical_policy_rng_initialization_material(
        identity.initialization_material);
    if (!decoded || decoded.value->policy_rng_stream_id != identity.policy_rng_stream_id) {
        return false;
    }
    try {
        return trajectory::compute_policy_rng_initialization_id(identity) ==
               identity.policy_rng_initialization_identity;
    } catch (...) {
        return false;
    }
}

trajectory::ProvenanceRegistration policy_rng_registration() {
    auto result = registration(trajectory::ProvenanceKind::PolicyRngContract,
                               kSha256CounterPolicyRngContractIdentity);
    trajectory::PolicyRngContractDescriptor descriptor;
    descriptor.initialization_material_is_canonical = canonical_initialization_material;
    descriptor.cursor_is_unique = unique_initialization_identity;
    result.policy_rng_descriptor = std::move(descriptor);
    return result;
}

}  // namespace

trajectory::ProvenanceResolver make_production_policy_provenance_resolver() {
    std::vector<trajectory::ProvenanceRegistration> registrations;
    registrations.push_back(registration(trajectory::ProvenanceKind::PolicyRngContract,
                                          trajectory::kNoPolicyRngContractId));
    registrations.push_back(registration(
        trajectory::ProvenanceKind::ProducerImplementation,
        kRandomLegalProducerImplementationIdentity));
    registrations.push_back(registration(
        trajectory::ProvenanceKind::InferenceAdapter,
        kDirectExecutionInferenceAdapterIdentity));
    registrations.push_back(registration(
        trajectory::ProvenanceKind::ObservationAdapter,
        kPublicObservationAdapterIdentity));
    registrations.push_back(registration(
        trajectory::ProvenanceKind::ActionAdapter,
        kPublicActionKeyAdapterIdentity));

    auto sampling = registration(trajectory::ProvenanceKind::SamplingContract,
                                 kUniformBelowU64SamplingContractIdentity);
    sampling.sampling_capabilities = trajectory::SamplingContractCapabilities{true, false};
    registrations.push_back(std::move(sampling));
    registrations.push_back(policy_rng_registration());
    return trajectory::ProvenanceResolver(std::move(registrations));
}

}  // namespace ygo::policy
