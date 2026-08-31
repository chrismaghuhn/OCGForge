#include "ygo/policy/production_provenance.hpp"

#include <string>
#include <utility>
#include <vector>

#include "ygo/policy/rng.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
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

trajectory::ProvenanceRegistration teacher_binding_registration(
    const teacher::StrategyProfileV1& profile) {
    const auto binding = make_teacher_policy_binding(profile);
    return registration(trajectory::ProvenanceKind::ArtifactMetadataArtifact,
                        binding.teacher_policy_binding_id);
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
    auto teacher_sampling = registration(
        trajectory::ProvenanceKind::SamplingContract,
        kTeacherDeterministicSamplingContractIdentity);
    teacher_sampling.sampling_capabilities = trajectory::SamplingContractCapabilities{true, true};
    registrations.push_back(std::move(teacher_sampling));
    registrations.push_back(policy_rng_registration());
    registrations.push_back(registration(
        trajectory::ProvenanceKind::ProducerImplementation,
        kTeacherProducerImplementationIdentity));
    registrations.push_back(teacher_binding_registration(
        teacher::make_swordsoul_tenyi_profile()));
    registrations.push_back(teacher_binding_registration(
        teacher::make_salamangreat_profile()));
    return trajectory::ProvenanceResolver(std::move(registrations));
}

}  // namespace ygo::policy
