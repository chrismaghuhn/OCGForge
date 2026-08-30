#include "ygo/policy/production.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/trajectory/codec.hpp"

namespace ygo::policy {

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

}  // namespace ygo::policy
