#include "ygo/policy/teacher.hpp"

#include "ygo/policy/production_provenance.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation public_observation(const std::uint8_t participant,
                                                 const std::uint64_t decision_index) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = participant;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = participant;
    source.globals.turn_player = 0;
    source.globals.turn_count = 1;
    source.globals.phase = 0x04;
    source.globals.chain_length = 0;
    source.globals.terminal = false;
    source.match_context.perspective_player = participant;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "idle_command";
    source.decision_context.player = participant;
    return project_public_observation(source);
}

EnvironmentActionCandidate public_candidate() {
    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::YesNo;
    candidate.choice = PublicChoice{PublicChoiceKind::YesNo, 0, std::nullopt};
    PublicActionKeyInput key;
    key.action_kind = "yes_no";
    key.choice = candidate.choice;
    candidate.public_action_key = public_action_key(key);
    return candidate;
}

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(assignments.begin(), assignments.end(),
                                 [player](const auto& assignment) {
                                     return assignment.player == player;
                                 });
    require(it != assignments.end(), "teacher assignment is missing a player");
    return *it;
}

const PolicyArtifact& artifact_for_deck_role(const PolicyArtifact& swordsoul,
                                             const PolicyArtifact& salamangreat,
                                             const DeckRole role) {
    return role == DeckRole::FirstLockedDeck ? swordsoul : salamangreat;
}

PolicyProvenanceEnvelope provenance_for(
    const PolicyArtifact& swordsoul, const PolicyArtifact& salamangreat,
    const std::vector<ParticipantPolicyAssignment>& assignments) {
    PolicyProvenanceEnvelope result;
    result.policy_artifacts = {swordsoul, salamangreat};
    std::sort(result.policy_artifacts.begin(), result.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.participant_assignments = assignments;
    return result;
}

void test_production_registrations_and_artifacts() {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_binding = make_teacher_policy_binding(swordsoul);
    const auto salamangreat_binding = make_teacher_policy_binding(salamangreat);
    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    const auto resolver = make_production_policy_provenance_resolver();

    require(resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                 kTeacherProducerImplementationIdentity),
            "Teacher producer registration is missing");
    const auto* sampling = resolver.sampling_contract_capabilities(
        kDeterministicLexicographicArgmaxSamplingContractIdentity);
    require(sampling != nullptr && sampling->complete && sampling->deterministic,
            "Teacher deterministic sampling registration is incomplete");
    require(resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                 swordsoul_binding.teacher_policy_binding_id) &&
                resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                     salamangreat_binding.teacher_policy_binding_id),
            "current Teacher binding metadata identities are not registered");

    for (const auto& artifact : {swordsoul_artifact, salamangreat_artifact}) {
        require(artifact.policy_kind == PolicyKind::DeterministicHeuristic &&
                    artifact.producer_implementation_identity ==
                        kTeacherProducerImplementationIdentity &&
                    artifact.inference_adapter_identity ==
                        kDirectExecutionInferenceAdapterIdentity &&
                    artifact.observation_adapter_identity ==
                        kPublicObservationAdapterIdentity &&
                    artifact.action_adapter_identity ==
                        kPublicActionKeyAdapterIdentity &&
                    artifact.sampling_contract_identity ==
                        kDeterministicLexicographicArgmaxSamplingContractIdentity &&
                    artifact.policy_rng_contract_identity == kNoPolicyRngContractId &&
                    artifact.model_checkpoint_identity == std::nullopt &&
                    artifact.search_contract_identity == std::nullopt &&
                    artifact.demonstration_source_identity == std::nullopt &&
                    artifact.artifact_metadata_identity.has_value(),
                "Teacher artifact has incompatible provenance fields");
        require(static_cast<bool>(decode_policy_artifact(
                       canonical_policy_artifact_bytes(artifact))),
                "Teacher artifact failed its canonical codec");
    }
    require(swordsoul_artifact.artifact_metadata_identity ==
                std::optional<std::string>{swordsoul_binding.teacher_policy_binding_id} &&
                salamangreat_artifact.artifact_metadata_identity ==
                    std::optional<std::string>{salamangreat_binding.teacher_policy_binding_id},
            "Teacher artifact metadata is not its exact binding ID");

    auto changed_profile = swordsoul;
    changed_profile.preferences.back().value++;
    changed_profile.profile_id = strategy_profile_id(changed_profile);
    const auto changed_artifact = make_teacher_policy_artifact(changed_profile);
    require(changed_artifact.policy_artifact_id != swordsoul_artifact.policy_artifact_id,
            "changing profile content did not change Teacher artifact identity");

    const auto config = CertifiedEnvironmentConfig::canonical();
    std::string binding_error;
    require(validate_strategy_profile_binding(swordsoul, config, &binding_error) &&
                validate_teacher_policy_binding(swordsoul_binding, swordsoul, &binding_error) &&
                validate_strategy_profile_binding(salamangreat, config, &binding_error) &&
                validate_teacher_policy_binding(salamangreat_binding, salamangreat,
                                                &binding_error),
            "current Teacher profile/binding pair failed environment validation: " +
                binding_error);
    const std::array<PolicyRole, 2> policy_roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    for (const auto seat_assignment : {SeatAssignment::Normal, SeatAssignment::Mirror}) {
        for (const auto starting_player : {std::uint8_t{0}, std::uint8_t{1}}) {
            const auto assignments = make_teacher_participant_assignments(
                swordsoul_artifact, salamangreat_artifact, config, seat_assignment,
                starting_player, policy_roles);
            const auto provenance = provenance_for(
                swordsoul_artifact, salamangreat_artifact, assignments);
            EpisodeSpec spec;
            spec.seat_assignment = seat_assignment;
            spec.starting_player = starting_player;
            std::string error;
            require(resolver.validate(provenance, config, spec, &error),
                    "Teacher provenance failed validation: " + error);
            for (std::uint8_t player = 0; player < 2; ++player) {
                const auto& assignment = assignment_for_player(assignments, player);
                const auto expected_deck_role = seat_assignment == SeatAssignment::Mirror
                                                    ? (player == 0 ? DeckRole::SecondLockedDeck
                                                                   : DeckRole::FirstLockedDeck)
                                                    : (player == 0 ? DeckRole::FirstLockedDeck
                                                                   : DeckRole::SecondLockedDeck);
                require(assignment.deck_role == expected_deck_role &&
                            assignment.seat_role ==
                                (player == starting_player ? SeatRole::StartingPlayer
                                                            : SeatRole::NonStartingPlayer),
                        "Teacher assignment did not preserve seat/deck mapping");
                const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                          ? swordsoul
                                          : salamangreat;
                const auto& artifact = artifact_for_deck_role(
                    swordsoul_artifact, salamangreat_artifact, assignment.deck_role);
                const auto binding = make_teacher_policy_binding(profile);
                auto session = create_teacher_policy_session(
                    profile, binding, artifact, assignment);
                require(static_cast<bool>(session),
                        "Teacher policy session failed construction");
                const auto before = session.value->policy.state();
                const std::vector<EnvironmentActionCandidate> candidates = {public_candidate()};
                const PolicyInput input{public_observation(player, 0), candidates};
                const auto selection = session.value->policy.select(input);
                require(static_cast<bool>(selection) && selection.value->rng_cursor == std::nullopt,
                        "Teacher selection did not use NONE RNG");
                const auto execution = session.value->execution_binding();
                require(execution.policy_artifact_id == artifact.policy_artifact_id &&
                            execution.participant_policy_assignment_id ==
                                assignment.participant_policy_assignment_id &&
                            execution.policy_rng_contract_identity == kNoPolicyRngContractId &&
                            execution.policy_rng_stream_id == kNoPolicyRngContractId &&
                            execution.policy_rng_initialization_identity ==
                                kNoPolicyRngContractId &&
                            execution.policy_rng_identity == kNoPolicyRngContractId,
                        "Teacher execution binding did not use canonical NONE identities");
                require(session.value->policy.state() == before &&
                            session.value->policy.has_pending_proposal(),
                        "Teacher selection mutated trusted state or lost its proposal");
                session.value->policy.reject_pending_proposal();
                require(session.value->policy.state() == before &&
                            !session.value->policy.has_pending_proposal(),
                        "Teacher rejection did not clear only the pending proposal");
            }
        }
    }

    const auto valid_assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config, SeatAssignment::Normal, 0,
        policy_roles);
    auto mismatched_assignment = assignment_for_player(valid_assignments, 0);
    mismatched_assignment.resolved_locked_deck_id = config.locked_decks[1].id;
    mismatched_assignment.resolved_locked_deck_sha256 = config.locked_decks[1].sha256;
    mismatched_assignment.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(mismatched_assignment);
    require(!create_teacher_policy_session(
                  swordsoul, swordsoul_binding, swordsoul_artifact, mismatched_assignment),
            "Teacher session accepted an assignment with the wrong resolved deck");
}

void test_none_rng_codec_and_stale_binding() {
    PolicyRngDecisionProvenance attribution;
    attribution.decision_index = 3;
    attribution.acting_policy_assignment_id =
        "participant_policy_assignment.v1." + std::string(64, 'a');
    attribution.mode = PolicyRngMode::None;
    attribution.policy_rng_identity = kNoPolicyRngContractId;
    attribution.policy_rng_contract_identity = kNoPolicyRngContractId;
    attribution.policy_rng_stream_id = kNoPolicyRngContractId;
    attribution.policy_rng_initialization_identity = kNoPolicyRngContractId;
    const auto bytes = canonical_policy_rng_decision_provenance_bytes(attribution);
    const auto decoded = decode_policy_rng_decision_provenance(bytes);
    require(static_cast<bool>(decoded) && decoded.value->mode == PolicyRngMode::None &&
                !decoded.value->pre_cursor.has_value() && !decoded.value->post_cursor.has_value() &&
                !decoded.value->pre_state.has_value() && !decoded.value->post_state.has_value(),
            "NONE RNG attribution did not pass its canonical codec");

    auto binding = make_teacher_policy_binding(make_swordsoul_tenyi_profile());
    binding.teacher_policy_binding_id =
        "ocgforge.teacher_policy_binding.v1." + std::string(64, 'b');
    require(!validate_teacher_policy_binding(binding),
            "stale/forged Teacher binding ID passed validation");
    const auto resolver = make_production_policy_provenance_resolver();
    require(!resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                  binding.teacher_policy_binding_id),
            "production resolver accepted an unregistered Teacher binding ID");
}

}  // namespace

int main() {
    try {
        test_production_registrations_and_artifacts();
        test_none_rng_codec_and_stale_binding();
        const auto swordsoul = make_swordsoul_tenyi_profile();
        const auto salamangreat = make_salamangreat_profile();
        std::cout << "swordsoul_profile_id=" << swordsoul.profile_id << '\n';
        std::cout << "salamangreat_profile_id=" << salamangreat.profile_id << '\n';
        std::cout << "swordsoul_binding_id="
                  << make_teacher_policy_binding(swordsoul).teacher_policy_binding_id << '\n';
        std::cout << "salamangreat_binding_id="
                  << make_teacher_policy_binding(salamangreat).teacher_policy_binding_id << '\n';
        std::cout << "swordsoul_policy_artifact_id="
                  << make_teacher_policy_artifact(swordsoul).policy_artifact_id << '\n';
        std::cout << "salamangreat_policy_artifact_id="
                  << make_teacher_policy_artifact(salamangreat).policy_artifact_id << '\n';
        std::cout << "teacher_provenance_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_provenance_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
