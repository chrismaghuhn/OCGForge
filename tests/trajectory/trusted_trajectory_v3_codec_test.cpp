#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_fixtures.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::trajectory;
namespace environment = ygo::environment;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
void require(const DecodeResult<T>& result, const std::string& message) {
    require(static_cast<bool>(result), message);
}

void require_hash(const std::vector<std::uint8_t>& bytes,
                  const std::string& expected,
                  const std::string& message) {
    require(ygo::trace::sha256_bytes(bytes) == expected,
            message + " (actual=" + ygo::trace::sha256_bytes(bytes) + ")");
}

PublicEnvironmentObservation observation(const std::uint8_t player,
                                         const std::uint64_t decision_index) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = player;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = player;
    source.globals.turn_player = 0;
    source.globals.turn_count = 1;
    source.globals.phase = 4;
    source.globals.chain_length = 0;
    source.globals.terminal = false;
    source.match_context.perspective_player = player;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "unselect_card";
    source.decision_context.player = player;
    return project_public_observation(source);
}

EnvironmentActionCandidate candidate(const PublicCardSelectionOperation operation,
                                     const std::string& locator,
                                     const bool v3 = true) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::CardSelection;
    result.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, locator};
    result.card_selection_operation = operation;
    PublicActionKeyInput input;
    input.action_kind = "card_selection";
    input.source_reference = result.source_reference;
    input.card_selection_operation = operation;
    result.public_action_key = v3 ? public_action_key_v3(input)
                                  : public_action_key_v2(input);
    return result;
}

EnvironmentActionCandidate cancel_candidate(const bool v3 = true) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::Cancel;
    PublicActionKeyInput input;
    input.action_kind = "cancel";
    result.public_action_key = v3 ? public_action_key_v3(input)
                                  : public_action_key_v2(input);
    return result;
}

PublicFrameSnapshotV3 frame(const CertifiedEnvironmentConfig& config,
                            const EpisodeSpec& spec) {
    PublicFrameSnapshotV3 result;
    result.episodic_environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV4ContractId);
    result.episode_semantic_id = episode_semantic_id(config, spec);
    result.decision_index = 0;
    result.acting_player = 0;
    result.public_observation = observation(0, 0);
    result.public_observation_digest = public_observation_digest(result.public_observation);
    result.request.kind = EnvironmentDecisionKind::UnselectCard;
    result.request.player = 0;
    result.request.candidates = {
        candidate(PublicCardSelectionOperation::Select, "p0:MONSTER_ZONE:0"),
        candidate(PublicCardSelectionOperation::Unselect, "p0:MONSTER_ZONE:1"),
        cancel_candidate(),
    };
    std::vector<std::string> keys;
    for (const auto& item : result.request.candidates) {
        keys.push_back(item.public_action_key);
    }
    result.public_candidate_domain_digest =
        public_candidate_domain_digest_v3("unselect_card", keys);
    PublicSemanticDecisionIdentityInputV3 identity;
    identity.episode_semantic_id = result.episode_semantic_id;
    identity.decision_index = result.decision_index;
    identity.acting_player = result.acting_player;
    identity.request_kind = "unselect_card";
    identity.public_observation_digest = result.public_observation_digest;
    identity.public_candidate_domain_digest = result.public_candidate_domain_digest;
    identity.public_action_keys = keys;
    result.public_semantic_decision_id = public_semantic_decision_id_v3(identity);
    return result;
}

EpisodeManifestV3 manifest(const CertifiedEnvironmentConfig& config,
                           const EpisodeSpec& spec) {
    EpisodeManifestV3 result;
    result.environment_semantic_id = environment_semantic_id(config);
    result.environment_identity_input = canonical_environment_identity_bytes(config);
    result.episode_semantic_id = episode_semantic_id(config, spec);
    result.episode_identity_input = canonical_episode_identity_bytes(config, spec);
    result.policy_provenance = trajectory_test::provenance();
    return result;
}

DecisionRecordV3 record(const EpisodeManifestV3& episode_manifest,
                        const PublicFrameSnapshotV3& public_frame) {
    DecisionRecordV3 result;
    result.frame = public_frame;
    result.selected_public_action_key = public_frame.request.candidates.front().public_action_key;
    result.transition_class = TransitionClass::AtomicEngineResponse;
    result.successor.kind = SuccessorKind::Terminal;
    const auto assignment = std::find_if(
        episode_manifest.policy_provenance.participant_assignments.begin(),
        episode_manifest.policy_provenance.participant_assignments.end(),
        [](const auto& value) { return value.player == 0; });
    require(assignment != episode_manifest.policy_provenance.participant_assignments.end(),
            "V3 fixture lacks player-zero assignment");
    result.acting_policy_assignment_id = assignment->participant_policy_assignment_id;
    result.policy_rng_decision_provenance =
        trajectory_test::no_rng(result.acting_policy_assignment_id, 0);
    return result;
}

EpisodeEnvelopeV3 envelope() {
    const auto config = CertifiedEnvironmentConfig::canonical_v4();
    EpisodeSpec spec;
    spec.contract_id = std::string(environment::kEpisodicEnvironmentV4ContractId);
    spec.root_seed = 19;
    const auto public_frame = frame(config, spec);
    EpisodeEnvelopeV3 result;
    result.manifest = manifest(config, spec);
    result.records.push_back(record(result.manifest, public_frame));
    TerminalClosureV3 terminal;
    terminal.winner = 0;
    terminal.win_reason = 1;
    terminal.semantic_action_count = 1;
    terminal.last_decision_index = 0;
    terminal.terminal_view_player_0 = observation(0, 1);
    terminal.terminal_view_player_0_digest =
        public_observation_digest(terminal.terminal_view_player_0);
    terminal.terminal_view_player_1 = observation(1, 1);
    terminal.terminal_view_player_1_digest =
        public_observation_digest(terminal.terminal_view_player_1);
    result.closure = std::move(terminal);
    return result;
}

void test_v3_leaf_and_cross_generation_codec() {
    const auto select = candidate(PublicCardSelectionOperation::Select,
                                  "p0:MONSTER_ZONE:0");
    const auto unselect = candidate(PublicCardSelectionOperation::Unselect,
                                    "p0:MONSTER_ZONE:1");
    require(canonical_public_environment_action_candidate_bytes_v3(select) !=
                canonical_public_environment_action_candidate_bytes_v3(unselect),
            "V3 Select and Unselect candidate bytes collided");
    require_hash(canonical_public_environment_action_candidate_bytes_v3(select),
                 "321b89ed8a2b4bab008f1715b4415ca697f94d2640a906b8da23ab7636e75b10",
                 "V3 Select candidate golden changed");
    require_hash(canonical_public_environment_action_candidate_bytes_v3(unselect),
                 "9a9288435e67a114ffd466a437cd1146fb2341a39946ee7d17fbcab98bea44e6",
                 "V3 Unselect candidate golden changed");
    require(decode_public_environment_action_candidate_v3(
                canonical_public_environment_action_candidate_bytes_v3(select)),
            "V3 Select candidate did not round-trip");
    require(!decode_public_environment_action_candidate_v3(
                canonical_public_environment_action_candidate_bytes_v2(
                    candidate(PublicCardSelectionOperation::Select,
                              "p0:MONSTER_ZONE:0", false))),
            "V3 candidate decoder accepted V2 bytes");
    require(!decode_public_environment_action_candidate_v2(
                canonical_public_environment_action_candidate_bytes_v3(select)),
            "V2 candidate decoder accepted V3 bytes");
}

void test_v3_nested_canonical_round_trip_and_tamper_rejection() {
    const auto value = envelope();
    const auto bytes = canonical_episode_envelope_bytes_v3(value);
    require_hash(bytes,
                 "870b182ae356c1ee066f39bf42d666e97a697b6077a3414f3255148766ecd4f0",
                 "V3 envelope golden changed");
    const auto decoded = decode_episode_envelope_v3(bytes);
    require(decoded, "V3 episode envelope did not round-trip");
    require(canonical_episode_envelope_bytes_v3(*decoded.value) == bytes,
            "V3 envelope decode/re-encode changed canonical bytes");

    auto swapped_perspectives = value;
    auto& swapped_terminal = std::get<TerminalClosureV3>(swapped_perspectives.closure);
    std::swap(swapped_terminal.terminal_view_player_0.perspective_player,
              swapped_terminal.terminal_view_player_1.perspective_player);
    swapped_terminal.terminal_view_player_0_digest = public_observation_digest(
        swapped_terminal.terminal_view_player_0);
    swapped_terminal.terminal_view_player_1_digest = public_observation_digest(
        swapped_terminal.terminal_view_player_1);
    bool swapped_rejected = false;
    try {
        (void)canonical_episode_envelope_bytes_v3(swapped_perspectives);
    } catch (...) {
        swapped_rejected = true;
    }
    require(swapped_rejected,
            "V3 codec accepted terminal views with swapped perspectives");

    auto wrong_perspective = value;
    auto& wrong_terminal = std::get<TerminalClosureV3>(wrong_perspective.closure);
    wrong_terminal.terminal_view_player_0.perspective_player = 1;
    wrong_terminal.terminal_view_player_0_digest = public_observation_digest(
        wrong_terminal.terminal_view_player_0);
    wrong_terminal.terminal_view_player_1_digest = public_observation_digest(
        wrong_terminal.terminal_view_player_1);
    bool wrong_rejected = false;
    try {
        (void)canonical_episode_envelope_bytes_v3(wrong_perspective);
    } catch (...) {
        wrong_rejected = true;
    }
    require(wrong_rejected,
            "V3 codec accepted a terminal view with the wrong perspective");

    require(!decode_episode_envelope_v3(canonical_episode_envelope_bytes(
                trajectory_test::terminal_envelope(19))),
            "V3 envelope decoder accepted a V1 envelope");

    auto tampered = bytes;
    tampered.back() ^= 0x01U;
    require(!decode_episode_envelope_v3(tampered),
            "V3 envelope decoder accepted tampered bytes");

    InterruptedClosureV3 interrupted;
    interrupted.record_count = 0;
    require(decode_episode_closure_v3(canonical_episode_closure_bytes_v3(interrupted)),
            "V3 interrupted closure did not round-trip");
    FailedClosureV3 failed;
    failed.failure_code = environment::FailureCode::CoreError;
    failed.failure_stage = environment::FailureStage::Advance;
    require(decode_episode_closure_v3(canonical_episode_closure_bytes_v3(failed)),
            "V3 failed closure did not round-trip");

    const auto gameplay_id = public_gameplay_trajectory_id_v3(value);
    const auto record_id = trajectory_record_id_v3(value);
    require(gameplay_id.rfind("public_gameplay_trajectory.v3.", 0) == 0 &&
                record_id.rfind("trajectory_record.v3.", 0) == 0,
            "V3 trajectory identities used the wrong generation");
    require(gameplay_id ==
                "public_gameplay_trajectory.v3.a126de8e3c43cf4a549d259057a6391c98f5d6fe267926e2c96a9a36d64ba304" &&
                record_id ==
                    "trajectory_record.v3.34d19984d96cd753b3f60bccaf8e3ff398188f60664697b801e2d9cc58ceb1b4",
            "V3 trajectory identity golden changed");
    std::cout << "V3_GAMEPLAY_ID=" << gameplay_id << '\n'
              << "V3_RECORD_ID=" << record_id << '\n';

    auto provenance_changed = value;
    for (auto& artifact : provenance_changed.manifest.policy_provenance.policy_artifacts) {
        artifact.producer_implementation_identity = "ocgforge.test.alternate_producer.v1";
        artifact.policy_artifact_id = compute_policy_artifact_id(artifact);
    }
    for (auto& assignment :
         provenance_changed.manifest.policy_provenance.participant_assignments) {
        assignment.policy_artifact_id =
            provenance_changed.manifest.policy_provenance.policy_artifacts.front().policy_artifact_id;
        assignment.participant_policy_assignment_id =
            compute_participant_policy_assignment_id(assignment);
    }
    std::sort(provenance_changed.manifest.policy_provenance.policy_artifacts.begin(),
              provenance_changed.manifest.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    std::sort(provenance_changed.manifest.policy_provenance.participant_assignments.begin(),
              provenance_changed.manifest.policy_provenance.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    const auto changed_assignment = std::find_if(
        provenance_changed.manifest.policy_provenance.participant_assignments.begin(),
        provenance_changed.manifest.policy_provenance.participant_assignments.end(),
        [](const auto& assignment) { return assignment.player == 0; });
    require(changed_assignment !=
                provenance_changed.manifest.policy_provenance.participant_assignments.end(),
            "changed V3 provenance omitted player-zero assignment");
    provenance_changed.records.front().acting_policy_assignment_id =
        changed_assignment->participant_policy_assignment_id;
    provenance_changed.records.front().policy_rng_decision_provenance
        .acting_policy_assignment_id = changed_assignment->participant_policy_assignment_id;
    require(public_gameplay_trajectory_id_v3(provenance_changed) == gameplay_id &&
                trajectory_record_id_v3(provenance_changed) != record_id,
            "V3 gameplay and record identities did not separate provenance");

    require_hash(canonical_public_environment_decision_request_bytes_v3(
                     value.records.front().frame.request),
                 "5bd7dec7aec4edcce82fd4ae7ba795e6660a4cb7bc0a66a0d637284815611d9d",
                 "V3 request golden changed");
    require_hash(canonical_public_frame_snapshot_bytes_v3(
                     value.records.front().frame),
                 "e322d31c0bb4d95d2c688b1cc854d3b758ec7cdf03ac9c39993b02575e04d6ec",
                 "V3 frame golden changed");
    require_hash(canonical_public_decision_record_bytes_v3(
                     value.records.front()),
                 "0d9d593654fa8b6ed416f52b1ea1c6d1ab7163689a6d4330df0aac3012398cf0",
                 "V3 public record golden changed");
    require_hash(canonical_collection_decision_record_bytes_v3(
                     value.records.front()),
                 "296abdfee80954395083bc2af9a6b4445b6fbfb33c2b4eb427bbe64be8277359",
                 "V3 collection record golden changed");
    require_hash(canonical_episode_manifest_bytes_v3(value.manifest),
                 "5fb3220f0aba956c9d6c0880b4c8074e33f4f8ca78a33540ed86581829ea3aa4",
                 "V3 manifest golden changed");
    require_hash(canonical_episode_closure_bytes_v3(value.closure),
                 "b210f8d4fa5287d713d16ec493a938978d6abf61c99f1567b7def9202998f9dd",
                 "V3 terminal closure golden changed");
    InterruptedClosureV3 golden_interrupted;
    require_hash(canonical_episode_closure_bytes_v3(golden_interrupted),
                 "ab33eab40eed7004e3e0af4cdcf38b465685fe15fa27ebd64d99326b4d851aae",
                 "V3 interrupted closure golden changed");
    FailedClosureV3 golden_failed;
    require_hash(canonical_episode_closure_bytes_v3(golden_failed),
                 "abecd3267a363d0a04878646d9870519702afcafa7041faf50793957150e5b70",
                 "V3 failed closure golden changed");

    std::cout << "V3_CANDIDATE_SELECT_SHA256="
              << ygo::trace::sha256_bytes(canonical_public_environment_action_candidate_bytes_v3(
                     candidate(PublicCardSelectionOperation::Select,
                               "p0:MONSTER_ZONE:0")))
              << '\n';
    std::cout << "V3_CANDIDATE_UNSELECT_SHA256="
              << ygo::trace::sha256_bytes(canonical_public_environment_action_candidate_bytes_v3(
                     candidate(PublicCardSelectionOperation::Unselect,
                               "p0:MONSTER_ZONE:1")))
              << '\n';
    std::cout << "V3_REQUEST_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_public_environment_decision_request_bytes_v3(
                         value.records.front().frame.request))
              << '\n';
    std::cout << "V3_FRAME_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_public_frame_snapshot_bytes_v3(
                         value.records.front().frame))
              << '\n';
    std::cout << "V3_PUBLIC_RECORD_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_public_decision_record_bytes_v3(
                         value.records.front()))
              << '\n';
    std::cout << "V3_COLLECTION_RECORD_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_collection_decision_record_bytes_v3(
                         value.records.front()))
              << '\n';
    std::cout << "V3_MANIFEST_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_episode_manifest_bytes_v3(value.manifest))
              << '\n';
    std::cout << "V3_TERMINAL_CLOSURE_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_episode_closure_bytes_v3(value.closure))
              << '\n';
    InterruptedClosureV3 empty_interrupted;
    std::cout << "V3_INTERRUPTED_CLOSURE_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_episode_closure_bytes_v3(empty_interrupted))
              << '\n';
    FailedClosureV3 empty_failed;
    std::cout << "V3_FAILED_CLOSURE_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_episode_closure_bytes_v3(empty_failed))
              << '\n';
    std::cout << "V3_ENVELOPE_SHA256=" << ygo::trace::sha256_bytes(bytes) << '\n';
}

}  // namespace

int main() {
    try {
        test_v3_leaf_and_cross_generation_codec();
        test_v3_nested_canonical_round_trip_and_tamper_rejection();
        std::cout << "trusted trajectory V3 codec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
