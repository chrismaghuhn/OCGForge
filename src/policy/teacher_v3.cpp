#include "ygo/policy/teacher_v3.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::policy {

teacher::TeacherPolicyBindingV1 make_teacher_policy_binding_v3(
    const teacher::StrategyProfileV1& profile) {
    if (!teacher::validate_strategy_profile(profile)) {
        throw std::invalid_argument("V3 Teacher binding requires a validated profile");
    }
    teacher::TeacherPolicyBindingV1 binding;
    binding.teacher_core_artifact_identity =
        std::string(kTeacherProducerImplementationIdentityV3);
    binding.strategy_profile_id = profile.profile_id;
    binding.score_contract_identity = std::string(teacher::kTeacherScoreContractId);
    binding.fallback_contract_identity = std::string(teacher::kTeacherFallbackContractId);
    binding.tie_break_contract_identity = std::string(teacher::kTeacherTieBreakContractId);
    binding.diagnostic_contract_identity.reset();
    binding.teacher_policy_binding_id = teacher::teacher_policy_binding_id(binding);
    return binding;
}

trajectory::PolicyArtifact make_teacher_policy_artifact_v3(
    const teacher::StrategyProfileV1& profile) {
    const auto binding = make_teacher_policy_binding_v3(profile);
    trajectory::PolicyArtifact artifact;
    artifact.policy_kind = trajectory::PolicyKind::DeterministicHeuristic;
    artifact.producer_implementation_identity =
        std::string(kTeacherProducerImplementationIdentityV3);
    artifact.inference_adapter_identity =
        std::string(kDirectExecutionInferenceAdapterIdentity);
    artifact.observation_adapter_identity = std::string(kPublicObservationAdapterIdentity);
    artifact.action_adapter_identity = std::string(kPublicActionKeyAdapterIdentityV3);
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

namespace {

PolicySelection invalid_selection(PolicyErrorCode code, std::string message) noexcept {
    PolicySelection result;
    result.error = PolicyError{code, std::move(message)};
    return result;
}

bool valid_v3_domain(
    const std::vector<environment::EnvironmentActionCandidate>& candidates) noexcept {
    if (candidates.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!environment::is_public_action_key_v3(candidates[index].public_action_key)) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (candidates[previous].public_action_key == candidates[index].public_action_key) {
                return false;
            }
        }
    }
    return true;
}

bool is_published_teacher_profile_pair_v3(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& binding,
    const trajectory::PolicyArtifact& artifact) {
    const auto matches = [&](const teacher::StrategyProfileV1& published_profile) {
        const auto published_binding = make_teacher_policy_binding_v3(published_profile);
        const auto published_artifact = make_teacher_policy_artifact_v3(published_profile);
        return teacher::canonical_strategy_profile_content_bytes(profile) ==
                   teacher::canonical_strategy_profile_content_bytes(published_profile) &&
               binding.teacher_policy_binding_id == published_binding.teacher_policy_binding_id &&
               artifact.policy_artifact_id == published_artifact.policy_artifact_id &&
               artifact.artifact_metadata_identity ==
                   std::optional<std::string>{published_binding.teacher_policy_binding_id};
    };
    return matches(teacher::make_swordsoul_tenyi_profile()) ||
           matches(teacher::make_salamangreat_profile());
}

}  // namespace

namespace detail {

bool validate_teacher_policy_session_v3(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment,
    std::string* diagnostic) noexcept {
    const auto fail = [diagnostic](const std::string& message) noexcept {
        if (diagnostic != nullptr) {
            *diagnostic = message;
        }
        return false;
    };
    try {
        const auto resolver = make_production_policy_provenance_resolver();
        const auto canonical_config = environment::CertifiedEnvironmentConfig::canonical_v4();
        if (!teacher::validate_teacher_policy_binding(policy_binding, profile, diagnostic)) {
            return false;
        }
        if (policy_binding.teacher_core_artifact_identity !=
                kTeacherProducerImplementationIdentityV3 ||
            policy_binding.diagnostic_contract_identity.has_value() ||
            !teacher::validate_strategy_profile_binding(profile, canonical_config, diagnostic)) {
            return fail("V3 Teacher binding/profile generation is invalid");
        }
        if (!resolver.can_resolve(trajectory::ProvenanceKind::ProducerImplementation,
                                  kTeacherProducerImplementationIdentityV3) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::InferenceAdapter,
                                  kDirectExecutionInferenceAdapterIdentity) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::ObservationAdapter,
                                  kPublicObservationAdapterIdentity) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::ActionAdapter,
                                  kPublicActionKeyAdapterIdentityV3) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::SamplingContract,
                                  kTeacherDeterministicSamplingContractIdentity) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::ArtifactMetadataArtifact,
                                  policy_binding.teacher_policy_binding_id)) {
            return fail("V3 Teacher provenance is not registered");
        }
        if (!is_published_teacher_profile_pair_v3(profile, policy_binding, artifact)) {
            return fail("V3 Teacher profile/provenance pair is not published");
        }
        if (artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
            artifact.producer_implementation_identity !=
                kTeacherProducerImplementationIdentityV3 ||
            artifact.inference_adapter_identity != kDirectExecutionInferenceAdapterIdentity ||
            artifact.observation_adapter_identity != kPublicObservationAdapterIdentity ||
            artifact.action_adapter_identity != kPublicActionKeyAdapterIdentityV3 ||
            artifact.sampling_contract_identity !=
                kTeacherDeterministicSamplingContractIdentity ||
            artifact.policy_rng_contract_identity != trajectory::kNoPolicyRngContractId ||
            artifact.artifact_metadata_identity !=
                std::optional<std::string>{policy_binding.teacher_policy_binding_id} ||
            artifact.policy_artifact_id != trajectory::compute_policy_artifact_id(artifact)) {
            return fail("V3 Teacher artifact provenance or identity is invalid");
        }
        if (assignment.player > 1 ||
            assignment.policy_artifact_id != artifact.policy_artifact_id ||
            static_cast<std::uint8_t>(assignment.deck_role) != profile.own_deck_role ||
            assignment.resolved_locked_deck_id != profile.own_deck_id ||
            assignment.resolved_locked_deck_sha256 != profile.own_deck_sha256 ||
            !trajectory::is_canonical_identity(
                assignment.participant_policy_assignment_id,
                "participant_policy_assignment.v1.") ||
            assignment.participant_policy_assignment_id !=
                trajectory::compute_participant_policy_assignment_id(assignment)) {
            return fail("V3 Teacher assignment provenance or identity is invalid");
        }
        (void)trajectory::canonical_policy_artifact_bytes(artifact);
        (void)trajectory::canonical_participant_policy_assignment_bytes(assignment);
        return true;
    } catch (const std::exception& error) {
        return fail(error.what());
    } catch (...) {
        return fail("V3 Teacher session validation threw");
    }
}

}  // namespace detail

DeterministicTeacherPolicyV3::DeterministicTeacherPolicyV3(
    teacher::StrategyProfileV1 profile,
    teacher::TeacherPolicyBindingV1 policy_binding,
    const std::uint8_t participant,
    std::string participant_policy_assignment_id)
    : profile_(std::move(profile)),
      policy_binding_(std::move(policy_binding)),
      participant_(participant),
      participant_policy_assignment_id_(std::move(participant_policy_assignment_id)) {
    if (participant_ > 1 ||
        !teacher::validate_teacher_policy_binding(policy_binding_, profile_) ||
        policy_binding_.teacher_core_artifact_identity !=
            kTeacherProducerImplementationIdentityV3 ||
        policy_binding_.diagnostic_contract_identity.has_value() ||
        !trajectory::is_canonical_identity(
            participant_policy_assignment_id_,
            "participant_policy_assignment.v1.")) {
        throw std::invalid_argument("invalid V3 Teacher policy session identity");
    }
    const auto reset = teacher::reset_strategy_state_v3(profile_);
    if (!reset.has_value()) {
        throw std::invalid_argument("V3 Teacher policy state reset failed");
    }
    state_ = *reset;
}

PolicySelection DeterministicTeacherPolicyV3::failure(
    const PolicyErrorCode code, std::string message) noexcept {
    return invalid_selection(code, std::move(message));
}

PolicySelection DeterministicTeacherPolicyV3::select(const PolicyInput& input) noexcept {
    return select_impl(input, nullptr);
}

PolicySelection DeterministicTeacherPolicyV3::select_with_diagnostics(
    const PolicyInput& input,
    teacher::TeacherRankingDiagnosticsV3& diagnostics) noexcept {
    return select_impl(input, &diagnostics);
}

PolicySelection DeterministicTeacherPolicyV3::select_impl(
    const PolicyInput& input,
    teacher::TeacherRankingDiagnosticsV3* diagnostics) noexcept {
    try {
        if (diagnostics != nullptr) *diagnostics = {};
        if (pending_.has_value()) {
            return failure(PolicyErrorCode::LifecycleFailure,
                           "V3 Teacher has an unresolved pending proposal");
        }
        if (input.observation.perspective_player != participant_) {
            return failure(PolicyErrorCode::InvalidConfiguration,
                           "V3 Teacher received an observation for another participant");
        }
        if (!valid_v3_domain(input.candidates)) {
            return failure(PolicyErrorCode::InvalidCandidateDomain,
                           "V3 Teacher requires a complete homogeneous V3 candidate domain");
        }

        teacher::TeacherCoreV3 core;
        auto ranking = core.propose(input, profile_, state_, diagnostics);
        auto selection = teacher::teacher_policy_selection_from_result_v3(ranking);
        if (!selection) {
            return selection;
        }
        if (!ranking.proposed_state_delta.has_value()) {
            return failure(PolicyErrorCode::LifecycleFailure,
                           "V3 Teacher selected without a proposed state delta");
        }
        if (selection.value->rng_cursor.has_value()) {
            return failure(PolicyErrorCode::InvalidConfiguration,
                           "V3 Teacher unexpectedly produced a policy RNG cursor");
        }
        pending_ = PendingProposal{input.observation, std::move(ranking), *selection.value};
        return selection;
    } catch (const std::exception& error) {
        return failure(PolicyErrorCode::InvalidConfiguration, error.what());
    } catch (...) {
        return failure(PolicyErrorCode::InvalidConfiguration, "V3 Teacher selection failed");
    }
}

bool DeterministicTeacherPolicyV3::commit(
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    if (!pending_.has_value()) {
        return false;
    }
    const auto pending = std::move(*pending_);
    pending_.reset();
    return teacher::commit_teacher_state_delta_with_evidence_v3(
        state_, pending.ranking, profile_, participant_, pending.observation,
        accepted_transition)
        .has_value();
}

void DeterministicTeacherPolicyV3::reject_pending_proposal() noexcept {
    pending_.reset();
}

TeacherPolicySessionCreateResultV3 create_teacher_policy_session_v3(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment) noexcept {
    try {
        std::string diagnostic;
        if (!detail::validate_teacher_policy_session_v3(
                profile, policy_binding, artifact, assignment, &diagnostic)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                diagnostic.empty() ? "invalid V3 Teacher session binding"
                                                    : diagnostic}};
        }
        TeacherPolicySessionV3 session{
            DeterministicTeacherPolicyV3(profile, policy_binding, assignment.player,
                                         assignment.participant_policy_assignment_id),
            artifact,
            assignment};
        return {std::optional<TeacherPolicySessionV3>(std::move(session)), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "V3 Teacher policy session construction failed"}};
    }
}

}  // namespace ygo::policy
