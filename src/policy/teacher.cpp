#include "ygo/policy/teacher.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/policy/production_provenance.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec.hpp"
namespace ygo::policy {

teacher::TeacherPolicyBindingV1 make_teacher_policy_binding(
    const teacher::StrategyProfileV1& profile) {
    if (!teacher::validate_strategy_profile(profile)) {
        throw std::invalid_argument("Teacher binding requires a validated profile");
    }
    teacher::TeacherPolicyBindingV1 binding;
    binding.teacher_core_artifact_identity =
        std::string(kTeacherProducerImplementationIdentity);
    binding.strategy_profile_id = profile.profile_id;
    binding.score_contract_identity = std::string(teacher::kTeacherScoreContractId);
    binding.fallback_contract_identity = std::string(teacher::kTeacherFallbackContractId);
    binding.tie_break_contract_identity = std::string(teacher::kTeacherTieBreakContractId);
    binding.diagnostic_contract_identity =
        std::string(teacher::kTeacherDiagnosticContractId);
    binding.teacher_policy_binding_id = teacher::teacher_policy_binding_id(binding);
    return binding;
}

trajectory::PolicyArtifact make_teacher_policy_artifact(
    const teacher::StrategyProfileV1& profile) {
    const auto binding = make_teacher_policy_binding(profile);
    trajectory::PolicyArtifact artifact;
    artifact.policy_kind = trajectory::PolicyKind::DeterministicHeuristic;
    artifact.producer_implementation_identity =
        std::string(kTeacherProducerImplementationIdentity);
    artifact.inference_adapter_identity =
        std::string(kDirectExecutionInferenceAdapterIdentity);
    artifact.observation_adapter_identity =
        std::string(kPublicObservationAdapterIdentity);
    artifact.action_adapter_identity = std::string(kPublicActionKeyAdapterIdentity);
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

std::vector<trajectory::ParticipantPolicyAssignment>
make_teacher_participant_assignments(
    const trajectory::PolicyArtifact& swordsoul_artifact,
    const trajectory::PolicyArtifact& salamangreat_artifact,
    const environment::CertifiedEnvironmentConfig& config,
    const environment::SeatAssignment seat_assignment,
    const std::uint8_t starting_player,
    const std::array<trajectory::PolicyRole, 2>& policy_roles) {
    if (starting_player > 1 || config.locked_decks.size() != 2 ||
        (seat_assignment != environment::SeatAssignment::Normal &&
         seat_assignment != environment::SeatAssignment::Mirror)) {
        throw std::invalid_argument("invalid Teacher assignment configuration");
    }
    if (swordsoul_artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
        salamangreat_artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
        swordsoul_artifact.policy_artifact_id == salamangreat_artifact.policy_artifact_id) {
        throw std::invalid_argument("Teacher assignments require two distinct artifacts");
    }
    (void)trajectory::canonical_policy_artifact_bytes(swordsoul_artifact);
    (void)trajectory::canonical_policy_artifact_bytes(salamangreat_artifact);

    std::vector<trajectory::ParticipantPolicyAssignment> result;
    result.reserve(2);
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto deck_index = seat_assignment == environment::SeatAssignment::Mirror
                                    ? 1u - static_cast<unsigned int>(player)
                                    : static_cast<unsigned int>(player);
        const auto& artifact = deck_index == 0 ? swordsoul_artifact : salamangreat_artifact;
        trajectory::ParticipantPolicyAssignment assignment;
        assignment.player = player;
        assignment.seat_role = player == starting_player
                                    ? trajectory::SeatRole::StartingPlayer
                                    : trajectory::SeatRole::NonStartingPlayer;
        assignment.deck_role = deck_index == 0 ? trajectory::DeckRole::FirstLockedDeck
                                               : trajectory::DeckRole::SecondLockedDeck;
        assignment.resolved_locked_deck_id = config.locked_decks[deck_index].id;
        assignment.resolved_locked_deck_sha256 = config.locked_decks[deck_index].sha256;
        assignment.policy_role = policy_roles[player];
        assignment.policy_artifact_id = artifact.policy_artifact_id;
        assignment.assignment_epoch = 0;
        assignment.effective_from_decision_index = 0;
        assignment.league_context.reset();
        assignment.participant_policy_assignment_id =
            trajectory::compute_participant_policy_assignment_id(assignment);
        (void)trajectory::canonical_participant_policy_assignment_bytes(assignment);
        result.push_back(std::move(assignment));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.participant_policy_assignment_id < right.participant_policy_assignment_id;
    });
    return result;
}

bool is_published_teacher_profile_pair(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& binding,
    const trajectory::PolicyArtifact& artifact) {
    const auto matches = [&](const teacher::StrategyProfileV1& published_profile) {
        const auto published_binding = make_teacher_policy_binding(published_profile);
        const auto published_artifact = make_teacher_policy_artifact(published_profile);
        return teacher::canonical_strategy_profile_content_bytes(profile) ==
                   teacher::canonical_strategy_profile_content_bytes(published_profile) &&
               binding.teacher_policy_binding_id ==
                   published_binding.teacher_policy_binding_id &&
               artifact.policy_artifact_id == published_artifact.policy_artifact_id &&
               artifact.artifact_metadata_identity ==
                   std::optional<std::string>{published_binding.teacher_policy_binding_id};
    };
    return matches(teacher::make_swordsoul_tenyi_profile()) ||
           matches(teacher::make_salamangreat_profile());
}

DeterministicTeacherPolicy::DeterministicTeacherPolicy(
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
        !trajectory::is_canonical_identity(
            participant_policy_assignment_id_,
            "participant_policy_assignment.v1.")) {
        throw std::invalid_argument("invalid Teacher policy session identity");
    }
    const auto reset = teacher::reset_strategy_state(profile_);
    if (!reset.has_value()) {
        throw std::invalid_argument("Teacher policy state reset failed");
    }
    state_ = *reset;
}

PolicySelection DeterministicTeacherPolicy::failure(
    const PolicyErrorCode code, std::string message) noexcept {
    PolicySelection result;
    result.error = PolicyError{code, std::move(message)};
    return result;
}

PolicySelection DeterministicTeacherPolicy::select(const PolicyInput& input) noexcept {
    try {
        if (pending_.has_value()) {
            return failure(PolicyErrorCode::LifecycleFailure,
                           "Teacher has an unresolved pending proposal");
        }
        if (input.observation.perspective_player != participant_) {
            return failure(PolicyErrorCode::InvalidConfiguration,
                           "Teacher received an observation for another participant");
        }
        teacher::TeacherCore core;
        auto ranking = core.propose(input, profile_, state_);
        auto selection = teacher::teacher_policy_selection_from_result(ranking);
        if (!selection || !selection.value.has_value() ||
            !ranking.proposed_state_delta.has_value()) {
            return selection;
        }
        if (selection.value->rng_cursor.has_value()) {
            return failure(PolicyErrorCode::InvalidConfiguration,
                           "Teacher unexpectedly produced a policy RNG cursor");
        }
        pending_ = PendingProposal{input.observation, std::move(ranking), *selection.value};
        return selection;
    } catch (const std::exception& error) {
        return failure(PolicyErrorCode::InvalidConfiguration, error.what());
    } catch (...) {
        return failure(PolicyErrorCode::InvalidConfiguration,
                       "Teacher selection failed");
    }
}

bool DeterministicTeacherPolicy::commit(
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    if (!pending_.has_value()) {
        return false;
    }
    const auto pending = std::move(*pending_);
    pending_.reset();
    return teacher::commit_teacher_state_delta(
        state_, pending.ranking, profile_, participant_, pending.observation,
        accepted_transition);
}

void DeterministicTeacherPolicy::reject_pending_proposal() noexcept {
    pending_.reset();
}

TeacherPolicySessionCreateResult create_teacher_policy_session(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment) noexcept {
    try {
        std::string diagnostic;
        const auto resolver = make_production_policy_provenance_resolver();
        const auto canonical_config = environment::CertifiedEnvironmentConfig::canonical();
        if (!teacher::validate_teacher_policy_binding(policy_binding, profile, &diagnostic) ||
            !teacher::validate_strategy_profile_binding(profile, canonical_config, &diagnostic) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::ProducerImplementation,
                                  kTeacherProducerImplementationIdentity) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::SamplingContract,
                                  kTeacherDeterministicSamplingContractIdentity) ||
            !resolver.can_resolve(trajectory::ProvenanceKind::ArtifactMetadataArtifact,
                                  policy_binding.teacher_policy_binding_id) ||
            !is_published_teacher_profile_pair(profile, policy_binding, artifact) ||
            artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
            artifact.producer_implementation_identity !=
                kTeacherProducerImplementationIdentity ||
            artifact.sampling_contract_identity !=
                kTeacherDeterministicSamplingContractIdentity ||
            artifact.policy_rng_contract_identity != trajectory::kNoPolicyRngContractId ||
            artifact.artifact_metadata_identity !=
                std::optional<std::string>{policy_binding.teacher_policy_binding_id} ||
            assignment.policy_artifact_id != artifact.policy_artifact_id ||
            assignment.player > 1 ||
            static_cast<std::uint8_t>(assignment.deck_role) != profile.own_deck_role ||
            assignment.resolved_locked_deck_id != profile.own_deck_id ||
            assignment.resolved_locked_deck_sha256 != profile.own_deck_sha256) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                diagnostic.empty() ? "invalid Teacher session binding"
                                                    : diagnostic}};
        }
        (void)trajectory::canonical_policy_artifact_bytes(artifact);
        (void)trajectory::canonical_participant_policy_assignment_bytes(assignment);
        TeacherPolicySession session{
            DeterministicTeacherPolicy(profile, policy_binding, assignment.player,
                                       assignment.participant_policy_assignment_id),
            artifact,
            assignment};
        return {std::optional<TeacherPolicySession>(std::move(session)), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "Teacher policy session construction failed"}};
    }
}

}  // namespace ygo::policy
