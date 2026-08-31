#include "ygo/policy/production.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/trajectory/codec.hpp"
#include "ygo/policy/rng.hpp"

namespace ygo::policy {

namespace {

bool same_initialization(
    const PolicyRngInitialization& actual,
    const trajectory::PolicyRngInitializationIdentity& expected) noexcept {
    return actual.policy_rng_contract_identity == expected.policy_rng_contract_identity &&
           actual.policy_rng_stream_id == expected.policy_rng_stream_id &&
           actual.initialization_material == expected.initialization_material &&
           actual.policy_rng_initialization_identity ==
               expected.policy_rng_initialization_identity;
}

}  // namespace

trajectory::PolicyArtifact make_random_legal_policy_artifact() {
    trajectory::PolicyArtifact artifact;
    artifact.policy_kind = trajectory::PolicyKind::RandomLegal;
    artifact.producer_implementation_identity =
        std::string(kRandomLegalProducerImplementationIdentity);
    artifact.inference_adapter_identity =
        std::string(kDirectExecutionInferenceAdapterIdentity);
    artifact.observation_adapter_identity = std::string(kPublicObservationAdapterIdentity);
    artifact.action_adapter_identity = std::string(kPublicActionKeyAdapterIdentity);
    artifact.sampling_contract_identity =
        std::string(kUniformBelowU64SamplingContractIdentity);
    artifact.policy_rng_contract_identity =
        std::string(kSha256CounterPolicyRngContractIdentity);
    artifact.model_checkpoint_identity.reset();
    artifact.search_contract_identity.reset();
    artifact.demonstration_source_identity.reset();
    artifact.artifact_metadata_identity.reset();
    artifact.policy_artifact_id = trajectory::compute_policy_artifact_id(artifact);
    return artifact;
}

std::vector<trajectory::ParticipantPolicyAssignment>
make_random_legal_participant_assignments(
    const trajectory::PolicyArtifact& artifact,
    const environment::CertifiedEnvironmentConfig& config,
    const environment::SeatAssignment seat_assignment,
    const std::uint8_t starting_player,
    const std::array<trajectory::PolicyRole, 2>& policy_roles) {
    if (starting_player > 1) {
        throw std::invalid_argument("starting player is outside the two-player environment");
    }
    if (seat_assignment != environment::SeatAssignment::Normal &&
        seat_assignment != environment::SeatAssignment::Mirror) {
        throw std::invalid_argument("seat assignment is unknown");
    }
    if (config.locked_decks.size() != 2) {
        throw std::invalid_argument("production policy requires exactly two locked decks");
    }

    // Validate the immutable artifact before binding it to participants.
    (void)trajectory::canonical_policy_artifact_bytes(artifact);

    std::vector<trajectory::ParticipantPolicyAssignment> assignments;
    assignments.reserve(2);
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto deck_index = seat_assignment == environment::SeatAssignment::Mirror
                                    ? 1u - static_cast<unsigned int>(player)
                                    : static_cast<unsigned int>(player);
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
        assignments.push_back(std::move(assignment));
    }

    std::sort(assignments.begin(), assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    return assignments;
}

RandomLegalExecutionBinding make_random_legal_execution_binding(
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment,
    const std::uint64_t explicit_policy_rng_root_seed,
    std::string policy_rng_stream_id) {
    if (artifact.policy_kind != trajectory::PolicyKind::RandomLegal ||
        artifact.policy_rng_contract_identity != kSha256CounterPolicyRngContractIdentity) {
        throw std::invalid_argument("execution binding requires the production RandomLegal artifact");
    }
    (void)trajectory::canonical_policy_artifact_bytes(artifact);
    (void)trajectory::canonical_participant_policy_assignment_bytes(assignment);
    if (assignment.policy_artifact_id != artifact.policy_artifact_id) {
        throw std::invalid_argument("execution binding assignment references another policy artifact");
    }

    PolicyRngInitializationInput input;
    input.policy_rng_root_seed = explicit_policy_rng_root_seed;
    input.participant_policy_assignment_id = assignment.participant_policy_assignment_id;
    input.policy_rng_stream_id = std::move(policy_rng_stream_id);
    const auto policy_initialization = make_policy_rng_initialization(input);
    if (!policy_initialization) {
        throw std::invalid_argument(policy_initialization.error->message);
    }

    RandomLegalExecutionBinding result;
    result.initialization.policy_rng_contract_identity =
        policy_initialization.value->policy_rng_contract_identity;
    result.initialization.policy_rng_stream_id =
        policy_initialization.value->policy_rng_stream_id;
    result.initialization.initialization_material =
        policy_initialization.value->initialization_material;
    result.initialization.policy_rng_initialization_identity =
        policy_initialization.value->policy_rng_initialization_identity;
    (void)trajectory::canonical_policy_rng_initialization_identity_bytes(
        result.initialization);

    result.stream.policy_artifact_id = artifact.policy_artifact_id;
    result.stream.participant_policy_assignment_id =
        assignment.participant_policy_assignment_id;
    result.stream.policy_rng_contract_identity =
        result.initialization.policy_rng_contract_identity;
    result.stream.policy_rng_stream_id = result.initialization.policy_rng_stream_id;
    result.stream.policy_rng_initialization_identity =
        result.initialization.policy_rng_initialization_identity;
    result.stream.policy_rng_identity = trajectory::compute_policy_rng_stream_id(result.stream);
    (void)trajectory::canonical_policy_rng_stream_identity_bytes(result.stream);

    result.execution_binding.policy_artifact_id = result.stream.policy_artifact_id;
    result.execution_binding.participant_policy_assignment_id =
        result.stream.participant_policy_assignment_id;
    result.execution_binding.policy_rng_contract_identity =
        result.stream.policy_rng_contract_identity;
    result.execution_binding.policy_rng_stream_id = result.stream.policy_rng_stream_id;
    result.execution_binding.policy_rng_initialization_identity =
        result.stream.policy_rng_initialization_identity;
    result.execution_binding.policy_rng_identity = result.stream.policy_rng_identity;
    return result;
}

RandomLegalPolicySessionCreateResult create_random_legal_policy_session(
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment,
    const std::uint64_t explicit_policy_rng_root_seed,
    std::string policy_rng_stream_id) noexcept {
    try {
        auto binding = make_random_legal_execution_binding(
            artifact, assignment, explicit_policy_rng_root_seed, std::move(policy_rng_stream_id));
        const auto input = decode_canonical_policy_rng_initialization_material(
            binding.initialization.initialization_material);
        if (!input) {
            return {std::nullopt, input.error};
        }
        auto policy = create_random_legal_policy(*input.value);
        if (!policy) {
            return {std::nullopt, policy.error};
        }
        if (policy.value->rng().cursor() != 0 ||
            !same_initialization(policy.value->rng().initialization(), binding.initialization)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "RandomLegal policy state does not match its execution binding"}};
        }
        RandomLegalPolicySession session{std::move(*policy.value), std::move(binding)};
        return {std::optional<RandomLegalPolicySession>(std::move(session)), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "RandomLegal session construction failed"}};
    }
}

}  // namespace ygo::policy
