#include "ygo/policy/teacher_v2.hpp"

#include <stdexcept>
#include <string>

#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::policy {

teacher::TeacherPolicyBindingV1 make_teacher_policy_binding_v2(
    const teacher::StrategyProfileV1& profile) {
    if (!teacher::validate_strategy_profile(profile)) {
        throw std::invalid_argument("V2 Teacher binding requires a validated profile");
    }
    teacher::TeacherPolicyBindingV1 binding;
    binding.teacher_core_artifact_identity =
        std::string(kTeacherProducerImplementationIdentityV2);
    binding.strategy_profile_id = profile.profile_id;
    binding.score_contract_identity = std::string(teacher::kTeacherScoreContractId);
    binding.fallback_contract_identity = std::string(teacher::kTeacherFallbackContractId);
    binding.tie_break_contract_identity = std::string(teacher::kTeacherTieBreakContractId);
    binding.diagnostic_contract_identity = std::string(teacher::kTeacherDiagnosticContractId);
    binding.teacher_policy_binding_id = teacher::teacher_policy_binding_id(binding);
    return binding;
}

trajectory::PolicyArtifact make_teacher_policy_artifact_v2(
    const teacher::StrategyProfileV1& profile) {
    const auto binding = make_teacher_policy_binding_v2(profile);
    trajectory::PolicyArtifact artifact;
    artifact.policy_kind = trajectory::PolicyKind::DeterministicHeuristic;
    artifact.producer_implementation_identity =
        std::string(kTeacherProducerImplementationIdentityV2);
    artifact.inference_adapter_identity =
        std::string(kDirectExecutionInferenceAdapterIdentity);
    artifact.observation_adapter_identity = std::string(kPublicObservationAdapterIdentity);
    artifact.action_adapter_identity = std::string(kPublicActionKeyAdapterIdentityV2);
    artifact.sampling_contract_identity =
        std::string(kTeacherDeterministicSamplingContractIdentity);
    artifact.policy_rng_contract_identity = trajectory::kNoPolicyRngContractId;
    artifact.model_checkpoint_identity.reset();
    artifact.search_contract_identity.reset();
    artifact.demonstration_source_identity.reset();
    artifact.artifact_metadata_identity = binding.teacher_policy_binding_id;
    artifact.policy_artifact_id = trajectory::compute_policy_artifact_id(artifact);
    return artifact;
}

}  // namespace ygo::policy
