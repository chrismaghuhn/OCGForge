#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"
#include "ygo/trajectory/shard.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "episodic_environment_test_access.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/protocol/continuation.hpp"

#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;
using namespace ygo::trajectory::dataset;
using namespace trajectory_test;
using ygo::observation::PlayerObservation;
using ygo::protocol::ActionCandidate;
using ygo::protocol::DecisionRequest;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool contains_bytes(const std::vector<std::uint8_t>& haystack,
                    const std::string_view needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) !=
           haystack.end();
}

std::unique_ptr<EpisodicEnvironment> make_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "privacy fixture could not create the certified environment");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

PlayerObservation private_observation(const std::uint8_t perspective,
                                      const std::uint32_t hidden_code) {
    PlayerObservation observation;
    observation.perspective_player = perspective;
    observation.engine_step_index = 91;
    observation.globals.life_points = {8000, 8000};
    observation.match_context.perspective_player = perspective;
    observation.match_context.own_deck.known = true;
    observation.match_context.opponent_deck.known = false;

    const auto hidden_controller = static_cast<std::uint8_t>(1 - perspective);
    const auto locator = std::string("p") + std::to_string(hidden_controller) +
                         ":SPELL_TRAP_ZONE:0";
    observation.zones.push_back({hidden_controller,
                                 observation::SemanticZone::SpellTrapZone,
                                 1,
                                 0,
                                 1,
                                 false});
    observation::ObservedCard hidden;
    hidden.locator = {locator};
    hidden.identity_known = false;
    hidden.controller = hidden_controller;
    hidden.zone = observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    observation.entities.push_back(std::move(hidden));
    observation.observation_hash = "PRIVATE-OBSERVATION-HASH-" + std::to_string(hidden_code);
    return observation;
}

DecisionRequest private_request(const std::uint32_t hidden_code) {
    DecisionRequest request;
    request.kind = protocol::DecisionRequestKind::CardSelection;
    request.decision_id = "PRIVATE-PROTOCOL-DECISION-" + std::to_string(hidden_code);
    request.engine_step_index = 91;
    request.player = 1;
    request.engine_message_type = 15;
    request.engine_message_name = "PRIVATE-MESSAGE-NAME";
    request.raw_message_hash = "PRIVATE-RAW-MESSAGE-HASH-" + std::to_string(hidden_code);
    ActionCandidate candidate;
    candidate.action_kind = protocol::ActionKind::CardSelection;
    candidate.semantic_key = "card.private." + std::to_string(hidden_code);
    candidate.source_card = hidden_code;
    candidate.source_controller = 0;
    candidate.source_location = 8;
    candidate.source_sequence = 0;
    candidate.source_index = 3;
    const std::string response = "PRIVATE-RAW-RESPONSE-BYTES";
    candidate.exact_response_bytes.assign(response.begin(), response.end());
    request.candidates.push_back(std::move(candidate));
    return request;
}

void test_internal_candidate_and_observation_values_are_not_projected() {
    auto environment = make_environment();
    constexpr std::uint32_t first_code = 14821890;
    constexpr std::uint32_t second_code = 7654321;
    auto first_request = private_request(first_code);
    auto second_request = private_request(second_code);
    auto first_observation = private_observation(1, first_code);
    auto second_observation = private_observation(1, second_code);
    ygo::observation::attach_decision_context(first_observation, first_request);
    ygo::observation::attach_decision_context(second_observation, second_request);

    const auto first_frame = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, first_request, first_observation, std::string(64, 'a'), 7);
    const auto second_frame = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, second_request, second_observation, std::string(64, 'a'), 7);
    const auto first_public = canonical_public_environment_observation_bytes(
        first_frame.public_observation);
    const auto second_public = canonical_public_environment_observation_bytes(
        second_frame.public_observation);
    require(first_public == second_public,
            "paired private observations produced different public observations");
    require(canonical_public_frame_snapshot_bytes(
                 PublicFrameSnapshot{first_frame.contract_id,
                                     first_frame.episode_semantic_id,
                                     first_frame.public_semantic_decision_id,
                                     first_frame.decision_index,
                                     first_frame.acting_player,
                                     first_frame.public_observation,
                                     first_frame.public_observation_digest,
                                     first_frame.request,
                                     first_frame.public_candidate_domain_digest}) ==
                canonical_public_frame_snapshot_bytes(
                    PublicFrameSnapshot{second_frame.contract_id,
                                        second_frame.episode_semantic_id,
                                        second_frame.public_semantic_decision_id,
                                        second_frame.decision_index,
                                        second_frame.acting_player,
                                        second_frame.public_observation,
                                        second_frame.public_observation_digest,
                                        second_frame.request,
                                        second_frame.public_candidate_domain_digest}),
            "paired private requests produced different public frame snapshots");

    const auto first_snapshot = PublicFrameSnapshot{
        first_frame.contract_id,
        first_frame.episode_semantic_id,
        first_frame.public_semantic_decision_id,
        first_frame.decision_index,
        first_frame.acting_player,
        first_frame.public_observation,
        first_frame.public_observation_digest,
        first_frame.request,
        first_frame.public_candidate_domain_digest};
    const auto first_snapshot_bytes = canonical_public_frame_snapshot_bytes(first_snapshot);
    const std::string first_text(first_snapshot_bytes.begin(), first_snapshot_bytes.end());
    require(first_text.find("PRIVATE-") == std::string::npos,
            "private protocol/request/response markers reached the public frame");
    require(first_text.find(std::to_string(first_code)) == std::string::npos &&
                first_text.find(std::to_string(second_code)) == std::string::npos,
            "hidden passcode reached the public frame");
    require(first_frame.request.candidates.front().public_action_key.find("PRIVATE-") ==
                std::string::npos,
            "private internal candidate identity reached the public action key");
}

void test_paired_world_projection_and_strict_observation_decode() {
    observation::PlayerObservation first;
    first.perspective_player = 0;
    first.decision_index = 7;
    first.match_context.perspective_player = 0;
    first.decision_context.kind = "yes_no";
    first.decision_context.player = 0;

    auto second = first;
    first.engine_step_index = 11;
    first.decision_context.decision_id = "protocol-decision-private-a";
    first.decision_context.engine_step_index = 11;
    first.decision_context.engine_message_type = 0x42;
    first.decision_context.engine_message_name = "private-message-a";
    first.decision_context.continuation_id = "private-continuation-a";
    first.observation_hash = "private-observation-hash-a";
    second.engine_step_index = 991;
    second.decision_context.decision_id = "protocol-decision-private-b";
    second.decision_context.engine_step_index = 991;
    second.decision_context.engine_message_type = 0x99;
    second.decision_context.engine_message_name = "private-message-b";
    second.decision_context.continuation_id = "private-continuation-b";
    second.observation_hash = "private-observation-hash-b";

    const auto first_public = project_public_observation(first);
    const auto second_public = project_public_observation(second);
    const auto first_bytes = canonical_public_environment_observation_bytes(first_public);
    const auto second_bytes = canonical_public_environment_observation_bytes(second_public);
    require(first_bytes == second_bytes,
            "paired worlds with identical public state produced different public bytes");

    PublicEnvironmentObservationInput decoded;
    require(decode_canonical_public_environment_observation(first_bytes, decoded),
            "canonical public observation did not strictly decode");
    require(canonical_public_environment_observation_bytes(decoded) == first_bytes,
            "public observation decoder did not preserve canonical bytes");

    ByteReader header(first_bytes);
    std::string schema;
    require(header.string(schema) && header.string(schema),
            "public observation header was not readable");
    auto mismatched_perspective = first_bytes;
    mismatched_perspective[header.position()] = 1;
    require(!decode_canonical_public_environment_observation(mismatched_perspective, decoded),
            "public observation accepted a safe state from another perspective");
}

void test_global_identity_and_restricted_provenance_separation() {
    const auto original = terminal_envelope(17);
    auto changed = original;
    auto& artifact = changed.manifest.policy_provenance.policy_artifacts.front();
    artifact.producer_implementation_identity = "ocgforge.private_policy_metadata.v1";
    artifact.policy_artifact_id = compute_policy_artifact_id(artifact);
    for (auto& assignment : changed.manifest.policy_provenance.participant_assignments) {
        assignment.policy_artifact_id = artifact.policy_artifact_id;
        assignment.participant_policy_assignment_id =
            compute_participant_policy_assignment_id(assignment);
    }
    std::sort(changed.manifest.policy_provenance.participant_assignments.begin(),
              changed.manifest.policy_provenance.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    const auto player_zero = std::find_if(
        changed.manifest.policy_provenance.participant_assignments.begin(),
        changed.manifest.policy_provenance.participant_assignments.end(),
        [](const auto& assignment) { return assignment.player == 0; });
    require(player_zero != changed.manifest.policy_provenance.participant_assignments.end(),
            "changed provenance lost player-zero assignment");
    changed.records.front().acting_policy_assignment_id =
        player_zero->participant_policy_assignment_id;
    changed.records.front().policy_rng_decision_provenance =
        no_rng(player_zero->participant_policy_assignment_id, 0);

    const auto original_public_record =
        canonical_public_decision_record_bytes(original.records.front());
    const auto changed_public_record =
        canonical_public_decision_record_bytes(changed.records.front());
    require(original_public_record == changed_public_record,
            "policy provenance changed the public decision record");
    require(public_gameplay_trajectory_id(original) == public_gameplay_trajectory_id(changed),
            "policy provenance changed the global gameplay identity");
    require(trajectory_record_id(original) != trajectory_record_id(changed),
            "policy provenance did not change the collection record identity");
    require(!contains_bytes(original_public_record,
                           "ocgforge.private_policy_metadata.v1"),
            "policy provenance reached public decision bytes");
    require(!contains_bytes(original_public_record, trajectory_record_id(original)),
            "global trajectory record identity reached public decision bytes");
    require(!contains_bytes(original_public_record, public_gameplay_trajectory_id(original)),
            "global gameplay identity reached public decision bytes");
}

void test_restricted_material_is_not_public_or_dataset_data() {
    const auto envelope = terminal_envelope(17);
    CandidateTrajectoryShard shard;
    shard.entries.push_back(shard_entry(envelope));

    const std::string restricted_marker = "PRIVATE-RNG-INITIALIZATION-MATERIAL";
    PolicyRngInitializationIdentity rng;
    rng.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    rng.policy_rng_stream_id = "private-stream";
    rng.initialization_material.assign(restricted_marker.begin(), restricted_marker.end());
    rng.policy_rng_initialization_identity = compute_policy_rng_initialization_id(rng);
    RestrictedCollectionEvidenceBundle restricted;
    restricted.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256(shard);
    restricted.rng_initializations.push_back(
        {rng.policy_rng_initialization_identity, rng.initialization_material});
    const auto restricted_bytes = canonical_restricted_collection_evidence_bundle_bytes(restricted);
    require(contains_bytes(restricted_bytes, restricted_marker),
            "privacy fixture did not contain its restricted material");

    AdmissionReceipt receipt;
    receipt.candidate_shard_artifact_sha256 = restricted.candidate_shard_artifact_sha256;
    receipt.restricted_evidence_artifact_sha256 =
        trace::sha256_bytes(restricted_bytes);
    const auto record_id = trajectory_record_id(envelope);
    receipt.entries.push_back({record_id,
                               public_gameplay_trajectory_id(envelope),
                               envelope.manifest.environment_semantic_id,
                               envelope.manifest.episode_semantic_id,
                               shard.entries.front().episode_envelope_sha256,
                               0});
    const auto receipt_bytes = canonical_admission_receipt_bytes(receipt);
    require(!contains_bytes(receipt_bytes, restricted_marker),
            "restricted RNG material reached the admission receipt");

    DatasetManifest manifest;
    manifest.dataset_semantic_id = dataset::dataset_semantic_id({record_id});
    manifest.members.push_back({record_id,
                                public_gameplay_trajectory_id(envelope),
                                admission_receipt_id(receipt),
                                receipt.candidate_shard_artifact_sha256,
                                shard.entries.front().episode_envelope_sha256});
    const auto manifest_bytes = canonical_dataset_manifest_bytes(manifest);
    require(!contains_bytes(manifest_bytes, restricted_marker),
            "restricted RNG material reached the dataset manifest");
}

}  // namespace

int main() {
    try {
        test_paired_world_projection_and_strict_observation_decode();
        test_internal_candidate_and_observation_values_are_not_projected();
        test_global_identity_and_restricted_provenance_separation();
        test_restricted_material_is_not_public_or_dataset_data();
        std::cout << "trajectory privacy tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "trajectory privacy tests failed: " << error.what() << '\n';
        return 1;
    }
}
