#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v2.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/policy_provenance.hpp"
#include "ygo/trajectory/types.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

constexpr std::string_view kSwordsoulBindingV1 =
    "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c";
constexpr std::string_view kSalamangreatBindingV1 =
    "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56";
constexpr std::string_view kSwordsoulArtifactV1 =
    "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d";
constexpr std::string_view kSalamangreatArtifactV1 =
    "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527";
constexpr std::string_view kSwordsoulBindingV2 =
    "ocgforge.teacher_policy_binding.v1.4da70292d08b5608552d9f9246050c2ea5b9c3b52c962ea26a8f9816c5447a5f";
constexpr std::string_view kSalamangreatBindingV2 =
    "ocgforge.teacher_policy_binding.v1.0ec1d4ce29956c72e7e8537d24ba04834dbed4f820ff222d0209f9d7880bc7c0";
constexpr std::string_view kSwordsoulArtifactV2 =
    "policy_artifact.v1.efbd7962734c993d9374acc4c527f722a2413e7279b851d10340a83defccfc01";
constexpr std::string_view kSalamangreatArtifactV2 =
    "policy_artifact.v1.17b2395a97e820c645f59037e7203f17bab808f90c1b70936c1000591efdb40e";

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_artifact_shape(const PolicyArtifact& artifact,
                            const std::string_view producer,
                            const std::string_view action_adapter) {
    require(artifact.policy_kind == PolicyKind::DeterministicHeuristic &&
                artifact.producer_implementation_identity == producer &&
                artifact.inference_adapter_identity ==
                    kDirectExecutionInferenceAdapterIdentity &&
                artifact.observation_adapter_identity ==
                    kPublicObservationAdapterIdentity &&
                artifact.action_adapter_identity == action_adapter &&
                artifact.sampling_contract_identity ==
                    kTeacherDeterministicSamplingContractIdentity &&
                artifact.policy_rng_contract_identity == kNoPolicyRngContractId &&
                !artifact.model_checkpoint_identity.has_value() &&
                !artifact.search_contract_identity.has_value() &&
                !artifact.demonstration_source_identity.has_value() &&
                artifact.artifact_metadata_identity.has_value(),
            "Teacher artifact carried an incompatible provenance shape");
}

void test_v1_and_v2_provenance() {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();

    const auto swordsoul_binding_v1 = make_teacher_policy_binding(swordsoul);
    const auto salamangreat_binding_v1 = make_teacher_policy_binding(salamangreat);
    const auto swordsoul_artifact_v1 = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact_v1 = make_teacher_policy_artifact(salamangreat);
    require(swordsoul_binding_v1.teacher_policy_binding_id == kSwordsoulBindingV1 &&
                salamangreat_binding_v1.teacher_policy_binding_id ==
                    kSalamangreatBindingV1 &&
                swordsoul_artifact_v1.policy_artifact_id == kSwordsoulArtifactV1 &&
                salamangreat_artifact_v1.policy_artifact_id == kSalamangreatArtifactV1,
            "historical Teacher provenance identity changed");
    require_artifact_shape(swordsoul_artifact_v1,
                           kTeacherProducerImplementationIdentity,
                           kPublicActionKeyAdapterIdentity);
    require_artifact_shape(salamangreat_artifact_v1,
                           kTeacherProducerImplementationIdentity,
                           kPublicActionKeyAdapterIdentity);

    const auto swordsoul_binding_v2 = make_teacher_policy_binding_v2(swordsoul);
    const auto salamangreat_binding_v2 = make_teacher_policy_binding_v2(salamangreat);
    const auto swordsoul_artifact_v2 = make_teacher_policy_artifact_v2(swordsoul);
    const auto salamangreat_artifact_v2 = make_teacher_policy_artifact_v2(salamangreat);

    require(kTeacherProducerImplementationIdentityV2 ==
                "ocgforge.policy.teacher_core.v2" &&
                kPublicActionKeyAdapterIdentityV2 ==
                    "ocgforge.policy.public_action_key.v2",
            "V2 provenance constants are not the frozen identities");
    require(validate_teacher_policy_binding(swordsoul_binding_v2, swordsoul) &&
                validate_teacher_policy_binding(salamangreat_binding_v2, salamangreat) &&
                !swordsoul_binding_v2.diagnostic_contract_identity.has_value() &&
                !salamangreat_binding_v2.diagnostic_contract_identity.has_value(),
            "V2 Teacher binding failed canonical validation");
    require(swordsoul_binding_v2.strategy_profile_id == swordsoul.profile_id &&
                salamangreat_binding_v2.strategy_profile_id == salamangreat.profile_id &&
                swordsoul_binding_v2.teacher_policy_binding_id == kSwordsoulBindingV2 &&
                salamangreat_binding_v2.teacher_policy_binding_id == kSalamangreatBindingV2 &&
                swordsoul_binding_v2.teacher_policy_binding_id !=
                    swordsoul_binding_v1.teacher_policy_binding_id &&
                salamangreat_binding_v2.teacher_policy_binding_id !=
                    salamangreat_binding_v1.teacher_policy_binding_id,
            "V2 bindings did not change content identity while retaining profiles");

    require_artifact_shape(swordsoul_artifact_v2,
                           kTeacherProducerImplementationIdentityV2,
                           kPublicActionKeyAdapterIdentityV2);
    require_artifact_shape(salamangreat_artifact_v2,
                           kTeacherProducerImplementationIdentityV2,
                           kPublicActionKeyAdapterIdentityV2);
    require(swordsoul_artifact_v2.artifact_metadata_identity ==
                std::optional<std::string>{swordsoul_binding_v2.teacher_policy_binding_id} &&
                salamangreat_artifact_v2.artifact_metadata_identity ==
                    std::optional<std::string>{salamangreat_binding_v2.teacher_policy_binding_id},
            "V2 artifact metadata did not bind to its V2 policy binding");
    require(swordsoul_artifact_v2.policy_artifact_id == kSwordsoulArtifactV2 &&
                salamangreat_artifact_v2.policy_artifact_id == kSalamangreatArtifactV2,
            "V2 artifact identity changed from the frozen deterministic goldens");
    require(swordsoul_artifact_v2.policy_artifact_id != swordsoul_artifact_v1.policy_artifact_id &&
                salamangreat_artifact_v2.policy_artifact_id !=
                    salamangreat_artifact_v1.policy_artifact_id,
            "V2 artifact identity did not change from the historical artifact");
    require(static_cast<bool>(decode_policy_artifact(
                canonical_policy_artifact_bytes(swordsoul_artifact_v2))) &&
                static_cast<bool>(decode_policy_artifact(
                    canonical_policy_artifact_bytes(salamangreat_artifact_v2))),
            "V2 artifact failed its canonical codec");

    const auto resolver = make_production_policy_provenance_resolver();
    require(resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                 kTeacherProducerImplementationIdentity) &&
                resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                     kTeacherProducerImplementationIdentityV2) &&
                resolver.can_resolve(ProvenanceKind::ActionAdapter,
                                     kPublicActionKeyAdapterIdentity) &&
                resolver.can_resolve(ProvenanceKind::ActionAdapter,
                                     kPublicActionKeyAdapterIdentityV2) &&
                resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                     swordsoul_binding_v1.teacher_policy_binding_id) &&
                resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                     salamangreat_binding_v1.teacher_policy_binding_id) &&
                resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                     swordsoul_binding_v2.teacher_policy_binding_id) &&
                resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                     salamangreat_binding_v2.teacher_policy_binding_id),
            "production provenance resolver did not contain both Teacher generations");

    std::cout << "V2_SWORDSOUL_BINDING_ID="
              << swordsoul_binding_v2.teacher_policy_binding_id << '\n'
              << "V2_SALAMANGREAT_BINDING_ID="
              << salamangreat_binding_v2.teacher_policy_binding_id << '\n'
              << "V2_SWORDSOUL_ARTIFACT_ID="
              << swordsoul_artifact_v2.policy_artifact_id << '\n'
              << "V2_SALAMANGREAT_ARTIFACT_ID="
              << salamangreat_artifact_v2.policy_artifact_id << '\n';
}

}  // namespace

int main() {
    try {
        test_v1_and_v2_provenance();
        std::cout << "teacher_v3_provenance_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_v3_provenance_test: " << error.what() << '\n';
        return 1;
    }
}
