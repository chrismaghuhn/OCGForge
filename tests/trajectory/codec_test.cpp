#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trace/sha256.hpp"
#include "provenance_test_support.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_golden(const std::vector<std::uint8_t>& bytes, const std::string& expected,
                    const std::string& message) {
    require(trace::sha256_bytes(bytes) == expected, message + " (SHA-256 mismatch)");
}

template <typename Function>
void require_throw(Function&& function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

PublicEnvironmentObservationInput public_observation(const std::uint8_t perspective,
                                                     const std::uint64_t decision_index) {
    observation::PlayerObservation source;
    source.perspective_player = perspective;
    source.decision_index = decision_index;
    source.match_context.perspective_player = perspective;
    source.match_context.own_deck.known = true;
    source.match_context.opponent_deck.known = false;
    source.decision_context.kind = "yes_no";
    source.decision_context.player = perspective;
    return project_public_observation(source);
}

PolicyArtifact deterministic_artifact() {
    PolicyArtifact artifact;
    artifact.policy_kind = PolicyKind::DeterministicHeuristic;
    artifact.producer_implementation_identity = "ocgforge.test.producer.v1";
    artifact.inference_adapter_identity = "ocgforge.test.inference.v1";
    artifact.observation_adapter_identity = "ocgforge.test.observation.v1";
    artifact.action_adapter_identity = "ocgforge.test.action.v1";
    artifact.sampling_contract_identity = "ocgforge.test.deterministic_sampling.v1";
    artifact.policy_rng_contract_identity = kNoPolicyRngContractId;
    artifact.policy_artifact_id = compute_policy_artifact_id(artifact);
    return artifact;
}

ParticipantPolicyAssignment assignment(const PolicyArtifact& artifact, const std::uint8_t player,
                                       const DeckRole deck_role, const SeatRole seat_role,
                                       const std::uint64_t effective = 0) {
    ParticipantPolicyAssignment result;
    result.player = player;
    result.seat_role = seat_role;
    result.deck_role = deck_role;
    result.resolved_locked_deck_id = deck_role == DeckRole::FirstLockedDeck ?
        "ocgforge.swordsoul_tenyi.ml_v1" : "ocgforge.salamangreat.ml_v1";
    result.resolved_locked_deck_sha256 = deck_role == DeckRole::FirstLockedDeck ?
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7" :
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
    result.policy_artifact_id = artifact.policy_artifact_id;
    result.assignment_epoch = 0;
    result.effective_from_decision_index = effective;
    result.participant_policy_assignment_id = compute_participant_policy_assignment_id(result);
    return result;
}

EnvironmentActionCandidate continuation_candidate(
    const EnvironmentActionKind action_kind, const std::string& operation,
    const bool submits_engine_response, const std::optional<std::uint32_t> source_index = {},
    const std::optional<std::int32_t> amount = {}) {
    PublicActionKeyInput key_input;
    key_input.action_kind = std::string(environment_action_kind_name(action_kind));
    key_input.source_index = source_index;
    key_input.amount = amount;
    key_input.continuation_operation = operation;
    EnvironmentActionCandidate value;
    value.action_kind = action_kind;
    value.source_index = source_index;
    value.amount = amount;
    value.continuation_operation = operation;
    value.submits_engine_response = submits_engine_response;
    value.public_action_key = public_action_key(key_input);
    return value;
}

EnvironmentDecisionRequest continuation_request(
    const std::string& kind, EnvironmentActionCandidate candidate_value,
    const bool can_finish = false, const bool can_cancel = false) {
    EnvironmentDecisionRequest value;
    value.kind = EnvironmentDecisionKind::CardSelection;
    value.player = 0;
    EnvironmentContinuationView continuation;
    continuation.continuation_kind = kind;
    continuation.remaining_indices = {0, 1};
    continuation.available_mask = 3;
    continuation.min_count = 1;
    continuation.max_count = 1;
    continuation.continuation_steps = 2;
    continuation.can_finish = can_finish;
    continuation.can_cancel = can_cancel;
    value.continuation = continuation;
    value.candidates.push_back(std::move(candidate_value));
    return value;
}

EnvironmentDecisionRequest atomic_request(EnvironmentActionCandidate candidate_value) {
    EnvironmentDecisionRequest value;
    value.kind = EnvironmentDecisionKind::YesNo;
    value.player = 0;
    value.candidates.push_back(std::move(candidate_value));
    return value;
}

DecisionRecord record_fixture(const CertifiedEnvironmentConfig& config,
                              const EpisodeSpec& spec,
                              const ParticipantPolicyAssignment& p0_assignment) {
    const auto episode_id = episode_semantic_id(config, spec);
    const auto observation = public_observation(0, 0);
    PublicActionKeyInput key_input;
    key_input.action_kind = "yes_no";
    key_input.choice = PublicChoice{PublicChoiceKind::YesNo, 1, std::nullopt};
    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::YesNo;
    candidate.choice = key_input.choice;
    candidate.public_action_key = public_action_key(key_input);
    candidate.continuation_operation = "";
    candidate.submits_engine_response = true;
    EnvironmentDecisionRequest request;
    request.kind = EnvironmentDecisionKind::YesNo;
    request.player = 0;
    request.candidates.push_back(candidate);
    PublicFrameSnapshot frame;
    frame.episode_semantic_id = episode_id;
    frame.public_observation = observation;
    frame.decision_index = 0;
    frame.acting_player = 0;
    frame.request = request;
    frame.public_observation_digest = public_observation_digest(observation);
    frame.public_candidate_domain_digest = public_candidate_domain_digest(
        "yes_no", {candidate.public_action_key});
    PublicSemanticDecisionIdentityInput decision_identity;
    decision_identity.episode_semantic_id = episode_id;
    decision_identity.decision_index = 0;
    decision_identity.acting_player = 0;
    decision_identity.request_kind = "yes_no";
    decision_identity.public_observation_digest = frame.public_observation_digest;
    decision_identity.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    frame.public_semantic_decision_id = public_semantic_decision_id(decision_identity);

    DecisionRecord record;
    record.frame = std::move(frame);
    record.selected_public_action_key = candidate.public_action_key;
    record.transition_class = TransitionClass::AtomicEngineResponse;
    record.successor.kind = SuccessorKind::Terminal;
    record.acting_policy_assignment_id = p0_assignment.participant_policy_assignment_id;
    record.policy_rng_decision_provenance.decision_index = 0;
    record.policy_rng_decision_provenance.acting_policy_assignment_id =
        p0_assignment.participant_policy_assignment_id;
    record.policy_rng_decision_provenance.policy_rng_identity = kNoPolicyRngContractId;
    record.policy_rng_decision_provenance.policy_rng_contract_identity = kNoPolicyRngContractId;
    record.policy_rng_decision_provenance.policy_rng_stream_id = kNoPolicyRngContractId;
    record.policy_rng_decision_provenance.policy_rng_initialization_identity = kNoPolicyRngContractId;
    record.policy_rng_decision_provenance.mode = PolicyRngMode::None;
    return record;
}

EpisodeEnvelope envelope_fixture() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    EpisodeSpec spec;
    spec.root_seed = 17;
    const auto artifact = deterministic_artifact();
    auto p0 = assignment(artifact, 0, DeckRole::FirstLockedDeck, SeatRole::StartingPlayer);
    auto p1 = assignment(artifact, 1, DeckRole::SecondLockedDeck, SeatRole::NonStartingPlayer);
    EpisodeManifest manifest;
    manifest.environment_semantic_id = config.environment_semantic_id;
    manifest.environment_identity_input = canonical_environment_identity_bytes(config);
    manifest.episode_semantic_id = episode_semantic_id(config, spec);
    manifest.episode_identity_input = canonical_episode_identity_bytes(config, spec);
    manifest.policy_provenance.policy_artifacts = {artifact};
    manifest.policy_provenance.participant_assignments = {p0, p1};
    if (manifest.policy_provenance.participant_assignments[1].participant_policy_assignment_id <
        manifest.policy_provenance.participant_assignments[0].participant_policy_assignment_id) {
        std::swap(manifest.policy_provenance.participant_assignments[0],
                  manifest.policy_provenance.participant_assignments[1]);
    }
    EpisodeEnvelope envelope;
    envelope.manifest = std::move(manifest);
    envelope.records.push_back(record_fixture(config, spec, p0));
    TerminalClosure terminal;
    terminal.winner = 0;
    terminal.win_reason = 1;
    terminal.semantic_action_count = 1;
    terminal.last_decision_index = 0;
    terminal.terminal_view_player_0 = public_observation(0, 1);
    terminal.terminal_view_player_0_digest = public_observation_digest(terminal.terminal_view_player_0);
    terminal.terminal_view_player_1 = public_observation(1, 1);
    terminal.terminal_view_player_1_digest = public_observation_digest(terminal.terminal_view_player_1);
    envelope.closure = std::move(terminal);
    return envelope;
}

void test_primitive_strictness() {
    ByteWriter writer;
    writer.string("ocgforge.test.v1");
    writer.boolean(true);
    writer.u32be(7);
    writer.raw({1, 2, 3});
    ByteReader reader(writer.data());
    std::string string_value;
    bool boolean_value = false;
    std::uint32_t integer = 0;
    std::vector<std::uint8_t> raw;
    require(reader.string(string_value) && string_value == "ocgforge.test.v1",
            "primitive string did not round-trip");
    require(reader.boolean(boolean_value) && boolean_value, "primitive bool did not round-trip");
    require(reader.u32be(integer) && integer == 7, "primitive integer did not round-trip");
    require(reader.raw(3, raw) && raw == std::vector<std::uint8_t>({1, 2, 3}) && reader.at_end(),
            "primitive raw did not round-trip");
    require(is_valid_utf8("valid-\xE2\x98\x83"), "valid UTF-8 was rejected");
    require(!is_valid_utf8("\xC0\x80"), "overlong UTF-8 was accepted");
    require(!is_lower_hex_digest(std::string(64, 'A')), "upper-case digest was accepted");

    ByteWriter malformed_provenance;
    malformed_provenance.string(kPolicyProvenanceContractId);
    malformed_provenance.string(kPolicyProvenanceContractId);
    malformed_provenance.u32be(std::numeric_limits<std::uint32_t>::max());
    require(!decode_policy_provenance_envelope(malformed_provenance.data()),
            "provenance count exceeded the remaining input without failing closed");

    ByteWriter malformed_request;
    malformed_request.string(kTrustedTrajectoryContractId);
    malformed_request.string("yes_no");
    malformed_request.u8(0);
    malformed_request.u32be(std::numeric_limits<std::uint32_t>::max());
    require(!decode_public_environment_decision_request(malformed_request.data()),
            "candidate count exceeded the remaining input without failing closed");
}

void test_policy_codecs() {
    auto artifact = deterministic_artifact();
    const auto artifact_bytes = canonical_policy_artifact_bytes(artifact);
    require_golden(artifact_bytes,
                   "1b889841f7d7168a26f43d9192b3ee524479833fcdcadfd922c3cc07809d8cb8",
                   "policy artifact golden");
    const auto decoded_artifact = decode_policy_artifact(artifact_bytes);
    require(decoded_artifact && canonical_policy_artifact_bytes(*decoded_artifact.value) == artifact_bytes,
            "policy artifact did not strictly round-trip");
    auto assignment_value = assignment(artifact, 0, DeckRole::FirstLockedDeck,
                                       SeatRole::StartingPlayer);
    const auto assignment_bytes = canonical_participant_policy_assignment_bytes(assignment_value);
    require_golden(assignment_bytes,
                   "89df692ffb5c46f706cfe53121eaf4defb1699091893eb85b5da2557ae91b0b4",
                   "participant assignment golden");
    const auto decoded_assignment = decode_participant_policy_assignment(assignment_bytes);
    require(decoded_assignment && canonical_participant_policy_assignment_bytes(*decoded_assignment.value) ==
                                       assignment_bytes,
            "participant assignment did not strictly round-trip");
    PolicyRngInitializationIdentity none;
    none.policy_rng_contract_identity = kNoPolicyRngContractId;
    none.policy_rng_stream_id = kNoPolicyRngContractId;
    none.policy_rng_initialization_identity = kNoPolicyRngContractId;
    const auto none_bytes = canonical_policy_rng_initialization_identity_bytes(none);
    require_golden(none_bytes,
                   "7bc39e6f0375614ffa3bda3ababc91efc51d5a08e78e4d85752ac779522a0ba0",
                   "NONE RNG initialization golden");
    const auto decoded_none = decode_policy_rng_initialization_identity(none_bytes);
    require(decoded_none && canonical_policy_rng_initialization_identity_bytes(*decoded_none.value) ==
                                  none_bytes,
            "NONE RNG did not strictly round-trip");
    PolicyRngDecisionProvenance provenance;
    provenance.acting_policy_assignment_id = assignment_value.participant_policy_assignment_id;
    provenance.policy_rng_identity = kNoPolicyRngContractId;
    provenance.policy_rng_contract_identity = kNoPolicyRngContractId;
    provenance.policy_rng_stream_id = kNoPolicyRngContractId;
    provenance.policy_rng_initialization_identity = kNoPolicyRngContractId;
    provenance.mode = PolicyRngMode::None;
    const auto rng_bytes = canonical_policy_rng_decision_provenance_bytes(provenance);
    require_golden(rng_bytes,
                   "a1abfc52b91580d78dc0ec260ceb51cad825e83635a0820afcf2a0fec047115b",
                   "NONE RNG decision golden");
    const auto decoded_rng = decode_policy_rng_decision_provenance(rng_bytes);
    require(decoded_rng && canonical_policy_rng_decision_provenance_bytes(*decoded_rng.value) ==
                                rng_bytes,
            "NONE decision RNG did not strictly round-trip");
    PolicyRngInitializationIdentity initialized;
    initialized.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    initialized.policy_rng_stream_id = "main";
    initialized.initialization_material = {0x01, 0x02, 0x03};
    initialized.policy_rng_initialization_identity = compute_policy_rng_initialization_id(initialized);
    const auto initialized_bytes = canonical_policy_rng_initialization_identity_bytes(initialized);
    require_golden(initialized_bytes,
                   "879760849e945af6f07b47efdef84e26844f60b7955249becf1509789b2ddbb8",
                   "initialized RNG golden");
    const auto decoded_initialized = decode_policy_rng_initialization_identity(initialized_bytes);
    require(decoded_initialized &&
                canonical_policy_rng_initialization_identity_bytes(*decoded_initialized.value) ==
                    initialized_bytes,
            "initialized RNG did not strictly round-trip");
    PolicyRngStreamIdentity stream;
    stream.policy_artifact_id = artifact.policy_artifact_id;
    stream.participant_policy_assignment_id = assignment_value.participant_policy_assignment_id;
    stream.policy_rng_contract_identity = initialized.policy_rng_contract_identity;
    stream.policy_rng_stream_id = initialized.policy_rng_stream_id;
    stream.policy_rng_initialization_identity = initialized.policy_rng_initialization_identity;
    stream.policy_rng_identity = compute_policy_rng_stream_id(stream);
    const auto stream_bytes = canonical_policy_rng_stream_identity_bytes(stream);
    require_golden(stream_bytes,
                   "65534e4163d27244d2502ae65c48922d370f97dbf6274312596f3a3e91eef7d3",
                   "RNG stream golden");
    const auto decoded_stream = decode_policy_rng_stream_identity(stream_bytes);
    require(decoded_stream && canonical_policy_rng_stream_identity_bytes(*decoded_stream.value) ==
                                  stream_bytes,
            "RNG stream did not strictly round-trip");
    PolicyRngDecisionProvenance cursor_provenance;
    cursor_provenance.decision_index = 3;
    cursor_provenance.acting_policy_assignment_id = assignment_value.participant_policy_assignment_id;
    cursor_provenance.policy_rng_identity = stream.policy_rng_identity;
    cursor_provenance.policy_rng_contract_identity = stream.policy_rng_contract_identity;
    cursor_provenance.policy_rng_stream_id = stream.policy_rng_stream_id;
    cursor_provenance.policy_rng_initialization_identity = stream.policy_rng_initialization_identity;
    cursor_provenance.mode = PolicyRngMode::Cursor;
    cursor_provenance.pre_cursor = 5;
    cursor_provenance.post_cursor = 6;
    const auto cursor_bytes = canonical_policy_rng_decision_provenance_bytes(cursor_provenance);
    require_golden(cursor_bytes,
                   "1356976f4a889f786d5ec2034ab41ffb3d5fd23b85f66401a91a88fe24682bec",
                   "CURSOR RNG golden");
    const auto decoded_cursor = decode_policy_rng_decision_provenance(cursor_bytes);
    require(decoded_cursor && canonical_policy_rng_decision_provenance_bytes(*decoded_cursor.value) ==
                                  cursor_bytes,
            "CURSOR decision RNG did not strictly round-trip");
    cursor_provenance.mode = PolicyRngMode::State;
    cursor_provenance.pre_cursor.reset();
    cursor_provenance.post_cursor.reset();
    cursor_provenance.pre_state = std::vector<std::uint8_t>{0x11, 0x12};
    cursor_provenance.post_state = std::vector<std::uint8_t>{0x21, 0x22};
    const auto state_bytes = canonical_policy_rng_decision_provenance_bytes(cursor_provenance);
    require_golden(state_bytes,
                   "d310e79f552018f1272f623ee089ef51dbd4c84d0e3236c7d589358caffabcf3",
                   "STATE RNG golden");
    const auto decoded_state = decode_policy_rng_decision_provenance(state_bytes);
    require(decoded_state && canonical_policy_rng_decision_provenance_bytes(*decoded_state.value) ==
                                 state_bytes,
            "STATE decision RNG did not strictly round-trip");
    auto policy_envelope = PolicyProvenanceEnvelope{};
    policy_envelope.policy_artifacts = {artifact};
    policy_envelope.participant_assignments = {assignment_value};
    const auto provenance_bytes = canonical_policy_provenance_envelope_bytes(policy_envelope);
    require(static_cast<bool>(decode_policy_provenance_envelope(provenance_bytes)),
            "policy provenance envelope did not decode");
    require_golden(provenance_bytes,
                   "7343194916eb776f6aff1fd902605758fa41d358c64633cb74221baeafdf989e",
                   "policy provenance envelope golden");
    auto corrupted = artifact_bytes;
    corrupted.back() ^= 1;
    require(!decode_policy_artifact(corrupted), "corrupt policy artifact was accepted");
    auto unknown_kind = artifact;
    unknown_kind.policy_kind = static_cast<PolicyKind>(0xff);
    bool unknown_kind_rejected = false;
    try {
        (void)canonical_policy_artifact_bytes(unknown_kind);
    } catch (const std::exception&) {
        unknown_kind_rejected = true;
    }
    require(unknown_kind_rejected, "policy artifact writer accepted an unknown enum");
}

void test_provenance_codec_lexical_strictness() {
    auto artifact = deterministic_artifact();
    artifact.producer_implementation_identity = "latest";
    artifact.policy_artifact_id = compute_policy_artifact_id(artifact);
    require_throw([&] { (void)canonical_policy_artifact_bytes(artifact); },
                  "policy artifact codec accepted a mutable producer alias");

    artifact = deterministic_artifact();
    artifact.producer_implementation_identity = "ocgforge..v1";
    artifact.policy_artifact_id = compute_policy_artifact_id(artifact);
    require_throw([&] { (void)canonical_policy_artifact_bytes(artifact); },
                  "policy artifact codec accepted an empty identity segment");

    PolicyRngInitializationIdentity initialization;
    initialization.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    initialization.policy_rng_stream_id = "../stream";
    initialization.initialization_material = {0x01, 0x02};
    initialization.policy_rng_initialization_identity =
        compute_policy_rng_initialization_id(initialization);
    require_throw(
        [&] { (void)canonical_policy_rng_initialization_identity_bytes(initialization); },
        "RNG initialization codec accepted a path-like stream token");

    const auto valid_artifact = deterministic_artifact();
    const auto valid_assignment = assignment(valid_artifact, 0, DeckRole::FirstLockedDeck,
                                             SeatRole::StartingPlayer);
    PolicyRngStreamIdentity stream;
    stream.policy_artifact_id = valid_artifact.policy_artifact_id;
    stream.participant_policy_assignment_id = valid_assignment.participant_policy_assignment_id;
    stream.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    stream.policy_rng_stream_id = "main";
    stream.policy_rng_initialization_identity =
        "policy_rng_initialization.v1." + std::string(64, '0');
    stream.policy_rng_identity = compute_policy_rng_stream_id(stream);
    stream.policy_rng_stream_id = "stream/with/slash";
    require_throw([&] { (void)canonical_policy_rng_stream_identity_bytes(stream); },
                  "RNG stream codec accepted a path-like stream token");
}

void test_public_codecs() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    EpisodeSpec spec;
    spec.root_seed = 17;
    auto artifact = deterministic_artifact();
    auto p0 = assignment(artifact, 0, DeckRole::FirstLockedDeck, SeatRole::StartingPlayer);
    const auto record = record_fixture(config, spec, p0);
    const auto candidate_bytes = canonical_public_environment_action_candidate_bytes(
        record.frame.request.candidates.front());
    require_golden(candidate_bytes,
                   "5e452ec11678c89f04dca5c25c04ea49c3fcf1e09f71897c71d744c75e48816c",
                   "public candidate golden");
    require(static_cast<bool>(decode_public_environment_action_candidate(candidate_bytes)),
            "candidate did not decode");
    auto unknown_action = record.frame.request.candidates.front();
    unknown_action.action_kind = static_cast<EnvironmentActionKind>(0xff);
    bool unknown_action_rejected = false;
    try {
        (void)canonical_public_environment_action_candidate_bytes(unknown_action);
    } catch (const std::exception&) {
        unknown_action_rejected = true;
    }
    require(unknown_action_rejected, "public candidate writer accepted an unknown enum");
    const auto request_bytes = canonical_public_environment_decision_request_bytes(record.frame.request);
    require_golden(request_bytes,
                   "01825c5d6ef7d5b615658cab5e683c0703e9706f954da7c4cedbdddcf9f7e312",
                   "public request golden");
    require(static_cast<bool>(decode_public_environment_decision_request(request_bytes)),
            "request did not decode");
    const auto frame_bytes = canonical_public_frame_snapshot_bytes(record.frame);
    require_golden(frame_bytes,
                   "b495fa88d8325fa812c6f92a560ea809b9af86ecdf4f2f7ad015ef36b818a9bf",
                   "public frame golden");
    const auto decoded_frame = decode_public_frame_snapshot(frame_bytes);
    require(decoded_frame && canonical_public_frame_snapshot_bytes(*decoded_frame.value) == frame_bytes,
            "frame did not strictly round-trip");
    const auto public_record_bytes = canonical_public_decision_record_bytes(record);
    require_golden(public_record_bytes,
                   "f431a146c1b320e278b11bc0660fea06ecd7652a88fee1e0e8a801e72cc14f30",
                   "public record golden");
    require(static_cast<bool>(decode_public_decision_record(public_record_bytes)),
            "public record did not decode");
    const auto evidence = RestrictedReplayEvidence{
        std::string(kEpisodicEnvironmentV2ContractId), record.frame.episode_semantic_id,
        InterruptionReason::SemanticActionBudget, 10, 10, 4, 1, 12};
    const auto evidence_bytes = canonical_restricted_replay_evidence_bytes(evidence);
    require_golden(evidence_bytes,
                   "04362060ce022c1fc5cdfb28e1b08b0901af91e6e7f2b27a67cecc663d17af66",
                   "restricted evidence golden");
    require(static_cast<bool>(decode_restricted_replay_evidence(evidence_bytes)),
            "restricted evidence did not decode");
}

void test_continuation_operation_validation() {
    const auto valid_pick = continuation_candidate(EnvironmentActionKind::Pick, "pick", false, 0);
    const auto valid_amount = continuation_candidate(EnvironmentActionKind::AssignAmount, "amount",
                                                     false, 0, 2);
    const auto valid_finish = continuation_candidate(EnvironmentActionKind::Finish, "finish", true);
    const auto valid_cancel = continuation_candidate(EnvironmentActionKind::Cancel, "cancel", true);
    const auto valid_bypass = continuation_candidate(EnvironmentActionKind::Cancel, "bypass", true);

    require(static_cast<bool>(decode_public_environment_decision_request(
                canonical_public_environment_decision_request_bytes(
                    continuation_request("unordered", valid_pick)))),
            "valid pick continuation was rejected");
    require(static_cast<bool>(decode_public_environment_decision_request(
                canonical_public_environment_decision_request_bytes(
                    continuation_request("counter", valid_amount)))),
            "valid amount continuation was rejected");
    require(static_cast<bool>(decode_public_environment_decision_request(
                canonical_public_environment_decision_request_bytes(
                    continuation_request("unordered", valid_finish, true)))),
            "valid finish continuation was rejected");
    require(static_cast<bool>(decode_public_environment_decision_request(
                canonical_public_environment_decision_request_bytes(
                    continuation_request("unordered", valid_cancel, false, true)))),
            "valid cancel continuation was rejected");
    require(static_cast<bool>(decode_public_environment_decision_request(
                canonical_public_environment_decision_request_bytes(
                    continuation_request("ordering", valid_bypass)))),
            "valid ordering bypass continuation was rejected");

    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                atomic_request(continuation_candidate(EnvironmentActionKind::YesNo, "pick", true)));
        },
        "atomic request retained a continuation operation");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                atomic_request(continuation_candidate(EnvironmentActionKind::YesNo, "", false)));
        },
        "atomic candidate without a response was accepted");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("unordered",
                                     continuation_candidate(EnvironmentActionKind::YesNo, "pick", false,
                                                            0)));
        },
        "pick operation with a non-Pick action kind was accepted");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("counter", valid_pick));
        },
        "pick operation was accepted for counter allocation");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("unordered",
                                     continuation_candidate(EnvironmentActionKind::Pick, "pick", true, 0)));
        },
        "pick operation that submits an engine response was accepted");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("counter",
                                     continuation_candidate(EnvironmentActionKind::AssignAmount, "amount",
                                                            false, {}, 2)));
        },
        "amount operation without a source index was accepted");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("unordered", valid_amount));
        },
        "amount operation outside counter allocation was accepted");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("counter",
                                     continuation_candidate(EnvironmentActionKind::AssignAmount, "amount",
                                                            false, 0, -1)));
        },
        "negative amount operation was accepted");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("counter",
                                     continuation_candidate(EnvironmentActionKind::AssignAmount, "amount",
                                                            false, 2, 2)));
        },
        "amount source outside the public remaining indices was accepted");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("unordered",
                                     continuation_candidate(EnvironmentActionKind::Finish, "finish", true),
                                     false));
        },
        "finish operation ignored can_finish=false");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("unordered",
                                     continuation_candidate(EnvironmentActionKind::Cancel, "cancel", true),
                                     false, false));
        },
        "cancel operation ignored can_cancel=false");
    require_throw(
        [&] {
            (void)canonical_public_environment_decision_request_bytes(
                continuation_request("unordered", valid_bypass));
        },
        "bypass operation was accepted outside ordering continuation");
}

void test_envelope_codec_and_identity() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    auto envelope = envelope_fixture();
    const auto bytes = canonical_episode_envelope_bytes(envelope);
    require_golden(canonical_policy_provenance_envelope_bytes(envelope.manifest.policy_provenance),
                   "bcf59e201c99217b99d9c16037b35ff8f3b44eee34a38bf10b354e663e5cede3",
                   "manifest provenance golden");
    require_golden(canonical_episode_manifest_bytes(envelope.manifest),
                   "6c6fbb7dc59a1aa484ed2281817d999e71674101df72a63a2d0fa902bab5de9a",
                   "episode manifest golden");
    require_golden(canonical_episode_closure_bytes(envelope.closure),
                   "043d4e445a5739a9fc4e41854d3f274eed1012902cb2db54962382b78e7802d4",
                   "terminal closure golden");
    require_golden(bytes,
                   "396c7aba75f3cb023d67dc72947a0e6a66bb6635a009495173be4fb5c8e1ddec",
                   "episode envelope golden");
    const auto decoded = decode_episode_envelope(bytes);
    require(decoded && canonical_episode_envelope_bytes(*decoded.value) == bytes,
            "episode envelope did not strictly round-trip");
    const auto public_id = public_gameplay_trajectory_id(envelope);
    require(is_canonical_identity(public_id, "public_gameplay_trajectory.v1."),
            "public gameplay digest is not usable as a lexical identity");
    const auto record_id = trajectory_record_id(envelope);
    require(public_id ==
                "public_gameplay_trajectory.v1.85286cadabd1c0105356af83aac4ad3c49c225598c3eb4e4534a6cb6803661a6",
            "public gameplay identity golden");
    require(record_id ==
                "trajectory_record.v1.dff90dfce7b9ae9ad556d6262f9e60a5658adfe92b1dc9bdf8769e8f1894055f",
            "trajectory record identity golden");
    require(is_canonical_identity(record_id, "trajectory_record.v1."),
            "trajectory record identity is not canonical");
    auto corrupted = bytes;
    corrupted[corrupted.size() / 2] ^= 1;
    require(!decode_episode_envelope(corrupted), "corrupt envelope was accepted");
    auto truncated = bytes;
    truncated.pop_back();
    require(!decode_episode_envelope(truncated), "truncated envelope was accepted");

    auto mixed_episode = envelope;
    EpisodeSpec other_spec;
    other_spec.root_seed = 18;
    mixed_episode.records.front().frame.episode_semantic_id =
        episode_semantic_id(config, other_spec);
    PublicSemanticDecisionIdentityInput mixed_identity;
    mixed_identity.episode_semantic_id = mixed_episode.records.front().frame.episode_semantic_id;
    mixed_identity.decision_index = mixed_episode.records.front().frame.decision_index;
    mixed_identity.acting_player = mixed_episode.records.front().frame.acting_player;
    mixed_identity.request_kind = std::string(environment_decision_kind_name(
        mixed_episode.records.front().frame.request.kind));
    mixed_identity.public_observation_digest =
        mixed_episode.records.front().frame.public_observation_digest;
    mixed_identity.public_candidate_domain_digest =
        mixed_episode.records.front().frame.public_candidate_domain_digest;
    mixed_episode.records.front().frame.public_semantic_decision_id =
        public_semantic_decision_id(mixed_identity);
    require_throw([&] { (void)canonical_episode_envelope_bytes(mixed_episode); },
                  "mixed-episode frame was admitted into an envelope");
}

void test_closure_variants() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    EpisodeSpec spec;
    spec.root_seed = 17;
    const auto episode_id = episode_semantic_id(config, spec);
    auto record = record_fixture(config, spec, assignment(deterministic_artifact(), 0,
                                                          DeckRole::FirstLockedDeck,
                                                          SeatRole::StartingPlayer));
    InterruptedClosure interrupted;
    interrupted.record_count = 0;
    interrupted.pending_unacted_frame = record.frame;
    EpisodeClosure interrupted_value = interrupted;
    const auto interrupted_bytes = canonical_episode_closure_bytes(interrupted_value);
    require_golden(interrupted_bytes,
                   "39eae5a8a8c27f704ee92e9ef45fb3f9a10735138e4f3c8229a1cef14a268ca9",
                   "interrupted closure golden");
    require(static_cast<bool>(decode_episode_closure(interrupted_bytes)),
            "interrupted closure did not decode");
    FailedClosure failed;
    failed.failure_code = FailureCode::CoreError;
    failed.failure_stage = FailureStage::Advance;
    failed.mutation_may_have_occurred = true;
    failed.record_count = 0;
    EpisodeClosure failed_value = failed;
    const auto failed_bytes = canonical_episode_closure_bytes(failed_value);
    require_golden(failed_bytes,
                   "6534b091e2ea30fdd3f81c99545fbcd4c2176bdd072cbe09cbeb3b3e8779b84a",
                   "failed closure golden");
    require(static_cast<bool>(decode_episode_closure(failed_bytes)), "failed closure did not decode");
    auto wrong_perspective = std::get<TerminalClosure>(envelope_fixture().closure);
    wrong_perspective.terminal_view_player_0.perspective_player = 1;
    bool rejected = false;
    try {
        (void)canonical_episode_closure_bytes(EpisodeClosure{wrong_perspective});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "terminal closure accepted a player-1 view in the player-0 slot");
    (void)episode_id;
}

void test_draw_terminal_codec_and_identity() {
    auto envelope = envelope_fixture();
    auto draw = std::get<TerminalClosure>(envelope.closure);
    draw.winner = 2;
    const EpisodeClosure draw_closure = draw;
    const auto bytes = canonical_episode_closure_bytes(draw_closure);
    require_golden(bytes,
                   "26e01fa7ada3aeaa5c19a15acc9348eda9815b41cf70fb0970ca3db4913d6b5b",
                   "draw terminal closure golden");
    const auto decoded = decode_episode_closure(bytes);
    require(decoded && std::get<TerminalClosure>(*decoded.value).winner == 2 &&
                canonical_episode_closure_bytes(*decoded.value) == bytes,
            "draw terminal closure did not strictly round-trip");

    envelope.closure = draw_closure;
    const auto draw_identity = public_gameplay_trajectory_id(envelope);
    require(draw_identity ==
                "public_gameplay_trajectory.v1.c01f9ff5c1bd7928371578e0459c6cc10600fb8b04aae5724868153a3fa90fe2",
            "draw terminal public gameplay identity golden");
    require(is_canonical_identity(draw_identity, "public_gameplay_trajectory.v1."),
            "draw terminal did not receive a canonical public gameplay identity");
    require(draw_identity != public_gameplay_trajectory_id(envelope_fixture()),
            "draw terminal did not change the public gameplay identity");

    observation::PlayerObservation public_draw;
    public_draw.perspective_player = 0;
    public_draw.globals.terminal = true;
    public_draw.globals.winner = std::uint8_t{2};
    public_draw.globals.win_reason = std::uint8_t{0};
    public_draw.match_context.perspective_player = 0;
    public_draw.match_context.own_deck.known = true;
    public_draw.match_context.opponent_deck.known = false;
    observation::VisibleGameEvent draw_event;
    draw_event.event_index = 0;
    draw_event.kind = observation::VisibleEventKind::Win;
    draw_event.winner = std::uint8_t{2};
    draw_event.win_reason = std::uint8_t{0};
    public_draw.visible_events.push_back(draw_event);
    const auto draw_view = project_public_observation(public_draw);
    require(!canonical_public_environment_observation_bytes(draw_view).empty(),
            "public terminal projection rejected a legitimate draw winner");

    auto invalid_bytes = bytes;
    const auto winner_offset = sizeof(std::uint32_t) +
                               std::string(kTrustedTrajectoryContractId).size() + 1;
    require(winner_offset < invalid_bytes.size(), "draw closure winner offset is invalid");
    invalid_bytes[winner_offset] = 3;
    require(!decode_episode_closure(invalid_bytes),
            "terminal closure accepted an out-of-range winner");
}

void test_typed_provenance_authority() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    EpisodeSpec spec;
    spec.root_seed = 17;
    const auto envelope = envelope_fixture();

    ProvenanceResolver production_resolver;
    std::string error;
    require(!production_resolver.validate(envelope.manifest.policy_provenance, config, spec, &error),
            "default production resolver trusted test fixture identities");

    const auto resolver = trajectory_test::test_provenance_resolver();
    require(resolver.validate(envelope.manifest.policy_provenance, config, spec, &error),
            "explicit test provenance registry did not validate its fixture: " + error);
    require(resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                 "ocgforge.test.producer.v1"),
            "typed producer identity was not resolved");
    require(!resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                  "ocgforge.test.deterministic_sampling.v1"),
            "sampling identity crossed into producer authority");
    require(!resolver.can_resolve(ProvenanceKind::ActionAdapter,
                                  "ocgforge.test.rng.v1"),
            "RNG identity crossed into action-adapter authority");
    require(!resolver.can_resolve(ProvenanceKind::InferenceAdapter,
                                  "ocgforge.test.action.v1"),
            "action identity crossed into inference authority");
    require(!production_resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                             "ocgforge.test.producer.v1"),
            "default resolver exposes test producer authority");

    const auto require_invalid_registration = [](const ProvenanceKind kind,
                                                 const std::string& identity,
                                                 const std::string& message) {
        require_throw(
            [&] {
                std::vector<ProvenanceRegistration> entries = {
                    trajectory_test::registry_entry(kind, identity)};
                (void)ProvenanceResolver(std::move(entries));
            },
            message);
    };
    require_invalid_registration(ProvenanceKind::ProducerImplementation, "latest",
                                 "mutable alias was accepted as a producer identity");
    require_invalid_registration(ProvenanceKind::ProducerImplementation,
                                 "ocgforge.test.producer",
                                 "unversioned contract was accepted as a producer identity");
    require_invalid_registration(ProvenanceKind::ProducerImplementation,
                                 "C:\\models\\producer.v1.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                 "host-local path was accepted as a producer identity");
    require_invalid_registration(ProvenanceKind::ModelCheckpointArtifact,
                                 "model.v1." + std::string(63, 'b'),
                                 "truncated content address was accepted");
    require_invalid_registration(ProvenanceKind::ModelCheckpointArtifact,
                                 "model.v1." + std::string(64, 'A'),
                                 "noncanonical content address was accepted");
}

void test_policy_kind_sampling_rng_consistency() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    EpisodeSpec spec;
    spec.root_seed = 17;
    const auto resolver = trajectory_test::test_provenance_resolver();

    const auto rebind = [](PolicyProvenanceEnvelope value, PolicyArtifact artifact) {
        value.policy_artifacts = {std::move(artifact)};
        for (auto& assignment_value : value.participant_assignments) {
            assignment_value.policy_artifact_id = value.policy_artifacts.front().policy_artifact_id;
            assignment_value.participant_policy_assignment_id =
                compute_participant_policy_assignment_id(assignment_value);
        }
        std::sort(value.participant_assignments.begin(), value.participant_assignments.end(),
                  [](const auto& left, const auto& right) {
                      return left.participant_policy_assignment_id <
                             right.participant_policy_assignment_id;
                  });
        return value;
    };
    const auto validate = [&](const PolicyArtifact& artifact) {
        return resolver.validate(
            rebind(envelope_fixture().manifest.policy_provenance, artifact), config, spec);
    };

    auto neural = deterministic_artifact();
    neural.policy_kind = PolicyKind::NeuralCheckpoint;
    neural.model_checkpoint_identity = "model.v1." + std::string(64, 'b');
    neural.policy_artifact_id = compute_policy_artifact_id(neural);
    require(validate(neural), "deterministic neural artifact was rejected");

    auto search = deterministic_artifact();
    search.policy_kind = PolicyKind::SearchAssisted;
    search.search_contract_identity = "ocgforge.test.search.v1";
    search.policy_artifact_id = compute_policy_artifact_id(search);
    require(validate(search), "deterministic search artifact was rejected");

    auto demonstration = deterministic_artifact();
    demonstration.policy_kind = PolicyKind::ImportedDemonstration;
    demonstration.demonstration_source_identity =
        "demonstration.v1." + std::string(64, 'd');
    demonstration.policy_artifact_id = compute_policy_artifact_id(demonstration);
    require(validate(demonstration), "deterministic demonstration artifact was rejected");

    for (const auto kind : {PolicyKind::NeuralCheckpoint, PolicyKind::SearchAssisted,
                            PolicyKind::ImportedDemonstration}) {
        auto stochastic = deterministic_artifact();
        stochastic.policy_kind = kind;
        stochastic.sampling_contract_identity = "ocgforge.test.random_sampling.v1";
        stochastic.policy_rng_contract_identity = "ocgforge.test.rng.v1";
        if (kind == PolicyKind::NeuralCheckpoint) {
            stochastic.model_checkpoint_identity = "model.v1." + std::string(64, 'b');
        } else if (kind == PolicyKind::SearchAssisted) {
            stochastic.search_contract_identity = "ocgforge.test.search.v1";
        } else {
            stochastic.demonstration_source_identity =
                "demonstration.v1." + std::string(64, 'd');
        }
        stochastic.policy_artifact_id = compute_policy_artifact_id(stochastic);
        require(validate(stochastic), "stochastic policy artifact was rejected");

        auto missing_rng = stochastic;
        missing_rng.policy_rng_contract_identity = kNoPolicyRngContractId;
        missing_rng.policy_artifact_id = compute_policy_artifact_id(missing_rng);
        require(!validate(missing_rng),
                "stochastic policy artifact with NONE RNG was admitted");
    }

    auto deterministic_neural_with_rng = deterministic_artifact();
    deterministic_neural_with_rng.policy_kind = PolicyKind::NeuralCheckpoint;
    deterministic_neural_with_rng.model_checkpoint_identity =
        "model.v1." + std::string(64, 'b');
    deterministic_neural_with_rng.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    deterministic_neural_with_rng.policy_artifact_id =
        compute_policy_artifact_id(deterministic_neural_with_rng);
    require(validate(deterministic_neural_with_rng),
            "deterministic neural sampling with explicit policy RNG was rejected");

    auto sampling_in_action = deterministic_artifact();
    sampling_in_action.action_adapter_identity = "ocgforge.test.deterministic_sampling.v1";
    sampling_in_action.policy_artifact_id = compute_policy_artifact_id(sampling_in_action);
    require(!validate(sampling_in_action),
            "sampling contract was accepted as an action adapter in a policy artifact");

    auto rng_in_observation = deterministic_artifact();
    rng_in_observation.observation_adapter_identity = "ocgforge.test.rng.v1";
    rng_in_observation.policy_artifact_id = compute_policy_artifact_id(rng_in_observation);
    require(!validate(rng_in_observation),
            "RNG contract was accepted as an observation adapter in a policy artifact");
}

void test_identity_input_codecs() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto environment_bytes = canonical_environment_identity_bytes(config);
    const auto decoded_environment = decode_environment_identity_input(environment_bytes);
    require(decoded_environment && is_current_certified_environment(*decoded_environment.value),
            "certified environment identity did not strictly resolve");
    EpisodeSpec spec;
    spec.root_seed = 99;
    spec.seat_assignment = SeatAssignment::Mirror;
    spec.starting_player = 1;
    const auto episode_bytes = canonical_episode_identity_bytes(config, spec);
    const auto decoded_episode = decode_episode_identity_input(episode_bytes, config);
    require(decoded_episode && decoded_episode.value->root_seed == spec.root_seed &&
                decoded_episode.value->seat_assignment == spec.seat_assignment &&
                decoded_episode.value->starting_player == spec.starting_player,
            "episode identity did not strictly resolve");
    auto corrupted = episode_bytes;
    corrupted.back() ^= 1;
    require(!decode_episode_identity_input(corrupted, config),
            "corrupt episode identity input was accepted");
}

void test_provenance_resolution() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    EpisodeSpec spec;
    spec.root_seed = 17;
    const auto artifact = deterministic_artifact();
    auto p0 = assignment(artifact, 0, DeckRole::FirstLockedDeck, SeatRole::StartingPlayer);
    auto p1 = assignment(artifact, 1, DeckRole::SecondLockedDeck, SeatRole::NonStartingPlayer);
    PolicyProvenanceEnvelope envelope;
    envelope.policy_artifacts = {artifact};
    envelope.participant_assignments = {p0, p1};
    if (envelope.participant_assignments[1].participant_policy_assignment_id <
        envelope.participant_assignments[0].participant_policy_assignment_id) {
        std::swap(envelope.participant_assignments[0], envelope.participant_assignments[1]);
    }
    ProvenanceResolver resolver = trajectory_test::test_provenance_resolver();
    std::string error;
    require(resolver.validate(envelope, config, spec, &error),
            "explicit contract provenance did not resolve: " + error);
    auto bad = envelope;
    bad.participant_assignments.front().resolved_locked_deck_sha256[0] = '0';
    require(!resolver.validate(bad, config, spec),
            "mismatched certified deck provenance was accepted");

    const auto provenance_for = [&](const SeatAssignment seat_assignment,
                                    const std::uint8_t starting_player) {
        PolicyProvenanceEnvelope result;
        const auto policy = deterministic_artifact();
        result.policy_artifacts = {policy};
        for (std::uint8_t player = 0; player < 2; ++player) {
            const auto deck_index = seat_assignment == SeatAssignment::Mirror ? 1 - player : player;
            result.participant_assignments.push_back(
                assignment(policy, player,
                           deck_index == 0 ? DeckRole::FirstLockedDeck
                                           : DeckRole::SecondLockedDeck,
                           player == starting_player ? SeatRole::StartingPlayer
                                                     : SeatRole::NonStartingPlayer));
        }
        std::sort(result.participant_assignments.begin(), result.participant_assignments.end(),
                  [](const auto& left, const auto& right) {
                      return left.participant_policy_assignment_id <
                             right.participant_policy_assignment_id;
                  });
        return result;
    };

    for (const auto seat_assignment : {SeatAssignment::Normal, SeatAssignment::Mirror}) {
        for (const auto starting_player : {std::uint8_t{0}, std::uint8_t{1}}) {
            auto combination = provenance_for(seat_assignment, starting_player);
            EpisodeSpec combination_spec = spec;
            combination_spec.seat_assignment = seat_assignment;
            combination_spec.starting_player = starting_player;
            error.clear();
            require(resolver.validate(combination, config, combination_spec, &error),
                    "seat/deck provenance combination did not resolve: " + error);
        }
    }

    auto mirror_wrong_deck = provenance_for(SeatAssignment::Mirror, 0);
    auto mirror_player_zero = std::find_if(
        mirror_wrong_deck.participant_assignments.begin(),
        mirror_wrong_deck.participant_assignments.end(),
        [](const auto& value) { return value.player == 0; });
    require(mirror_player_zero != mirror_wrong_deck.participant_assignments.end(),
            "mirror provenance fixture lacks player zero");
    mirror_player_zero->deck_role = DeckRole::FirstLockedDeck;
    mirror_player_zero->resolved_locked_deck_id = "ocgforge.swordsoul_tenyi.ml_v1";
    mirror_player_zero->resolved_locked_deck_sha256 =
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
    mirror_player_zero->participant_policy_assignment_id =
        compute_participant_policy_assignment_id(*mirror_player_zero);
    std::sort(mirror_wrong_deck.participant_assignments.begin(),
              mirror_wrong_deck.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    EpisodeSpec mirror_spec = spec;
    mirror_spec.seat_assignment = SeatAssignment::Mirror;
    require(!resolver.validate(mirror_wrong_deck, config, mirror_spec),
            "mirror provenance accepted a pre-swap player/deck mapping");

    const auto provenance_with_artifact = [&](const PolicyArtifact& policy) {
        PolicyProvenanceEnvelope result;
        result.policy_artifacts = {policy};
        result.participant_assignments = {
            assignment(policy, 0, DeckRole::FirstLockedDeck, SeatRole::StartingPlayer),
            assignment(policy, 1, DeckRole::SecondLockedDeck, SeatRole::NonStartingPlayer)};
        std::sort(result.participant_assignments.begin(), result.participant_assignments.end(),
                  [](const auto& left, const auto& right) {
                      return left.participant_policy_assignment_id <
                             right.participant_policy_assignment_id;
                  });
        return result;
    };

    auto random_artifact = deterministic_artifact();
    random_artifact.policy_kind = PolicyKind::RandomLegal;
    random_artifact.sampling_contract_identity = "ocgforge.test.random_sampling.v1";
    random_artifact.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    random_artifact.policy_artifact_id = compute_policy_artifact_id(random_artifact);
    auto random_provenance = provenance_with_artifact(random_artifact);
    require(resolver.validate(random_provenance, config, spec),
            "complete RANDOM_LEGAL sampling provenance was rejected");

    auto incomplete_deterministic = deterministic_artifact();
    incomplete_deterministic.sampling_contract_identity =
        "ocgforge.test.incomplete_sampling.v1";
    incomplete_deterministic.policy_artifact_id =
        compute_policy_artifact_id(incomplete_deterministic);
    require(!resolver.validate(provenance_with_artifact(incomplete_deterministic), config, spec),
            "incomplete sampling contract was admitted for deterministic policy");

    auto deterministic_with_random_sampling = deterministic_artifact();
    deterministic_with_random_sampling.sampling_contract_identity =
        "ocgforge.test.random_sampling.v1";
    deterministic_with_random_sampling.policy_artifact_id =
        compute_policy_artifact_id(deterministic_with_random_sampling);
    require(!resolver.validate(provenance_with_artifact(deterministic_with_random_sampling),
                               config, spec),
            "non-deterministic sampling contract was admitted for deterministic policy");

    require(!resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                  "ocgforge.unknown_contract.v1"),
            "unknown versioned contract identity was resolved as trusted");
    require(!resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                  "ocgforge.test.v1"),
            "unregistered contract identity crossed into producer authority");
    const auto immutable_content = std::string("content.v1.") + std::string(64, 'c');
    ProvenanceResolver content_resolver({
        trajectory_test::registry_entry(ProvenanceKind::ImmutableContentArtifact,
                                        immutable_content)});
    require(content_resolver.can_resolve(ProvenanceKind::ImmutableContentArtifact,
                                          immutable_content) &&
                !content_resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                              immutable_content),
            "explicit content identity was not kept distinct from typed implementation authority");
    ProvenanceResolver mixed_registry({
        trajectory_test::registry_entry(ProvenanceKind::ImmutableContentArtifact,
                                        immutable_content)});
    require(mixed_registry.can_resolve(ProvenanceKind::ImmutableContentArtifact,
                                       immutable_content) &&
                !mixed_registry.can_resolve(ProvenanceKind::ProducerImplementation,
                                            immutable_content),
            "content identity crossed into typed implementation authority");
    ProvenanceResolver unknown_contract_registry({
        trajectory_test::registry_entry(ProvenanceKind::ImmutableContentArtifact,
                                        "content.v1." + std::string(64, 'd'))});
    require(unknown_contract_registry.can_resolve(ProvenanceKind::ImmutableContentArtifact,
                                                  "content.v1." + std::string(64, 'd')),
            "explicit immutable content registration was not preserved");
    require(!unknown_contract_registry.can_resolve(ProvenanceKind::SamplingContract,
                                                   "content.v1." + std::string(64, 'd')),
            "immutable content identity crossed into sampling authority");
    auto unknown_identity = envelope;
    unknown_identity.policy_artifacts.front().producer_implementation_identity =
        "ocgforge.unknown_producer.v1";
    unknown_identity.policy_artifacts.front().policy_artifact_id =
        compute_policy_artifact_id(unknown_identity.policy_artifacts.front());
    for (auto& assignment_value : unknown_identity.participant_assignments) {
        assignment_value.policy_artifact_id =
            unknown_identity.policy_artifacts.front().policy_artifact_id;
        assignment_value.participant_policy_assignment_id =
            compute_participant_policy_assignment_id(assignment_value);
    }
    std::sort(unknown_identity.participant_assignments.begin(),
              unknown_identity.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    require(!resolver.validate(unknown_identity, config, spec),
            "unknown policy producer identity was admitted");

    auto missing_checkpoint = envelope;
    missing_checkpoint.policy_artifacts.front().policy_kind = PolicyKind::NeuralCheckpoint;
    missing_checkpoint.policy_artifacts.front().policy_artifact_id =
        compute_policy_artifact_id(missing_checkpoint.policy_artifacts.front());
    for (auto& assignment_value : missing_checkpoint.participant_assignments) {
        assignment_value.policy_artifact_id =
            missing_checkpoint.policy_artifacts.front().policy_artifact_id;
        assignment_value.participant_policy_assignment_id =
            compute_participant_policy_assignment_id(assignment_value);
    }
    std::sort(missing_checkpoint.participant_assignments.begin(),
              missing_checkpoint.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    require(!resolver.validate(missing_checkpoint, config, spec),
            "neural policy without a checkpoint identity was admitted");

    auto contract_in_content_field = envelope;
    contract_in_content_field.policy_artifacts.front().artifact_metadata_identity =
        "ocgforge.test.v1";
    contract_in_content_field.policy_artifacts.front().policy_artifact_id =
        compute_policy_artifact_id(contract_in_content_field.policy_artifacts.front());
    for (auto& assignment_value : contract_in_content_field.participant_assignments) {
        assignment_value.policy_artifact_id =
            contract_in_content_field.policy_artifacts.front().policy_artifact_id;
        assignment_value.participant_policy_assignment_id =
            compute_participant_policy_assignment_id(assignment_value);
    }
    std::sort(contract_in_content_field.participant_assignments.begin(),
              contract_in_content_field.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    require(!resolver.validate(contract_in_content_field, config, spec),
            "contract identity was accepted in a content-only metadata field");

    auto content_in_contract_field = envelope;
    content_in_contract_field.policy_artifacts.front().sampling_contract_identity =
        immutable_content;
    content_in_contract_field.policy_artifacts.front().policy_artifact_id =
        compute_policy_artifact_id(content_in_contract_field.policy_artifacts.front());
    for (auto& assignment_value : content_in_contract_field.participant_assignments) {
        assignment_value.policy_artifact_id =
            content_in_contract_field.policy_artifacts.front().policy_artifact_id;
        assignment_value.participant_policy_assignment_id =
            compute_participant_policy_assignment_id(assignment_value);
    }
    std::sort(content_in_contract_field.participant_assignments.begin(),
              content_in_contract_field.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    require(!content_resolver.validate(content_in_contract_field, config, spec),
            "content identity was accepted in a contract-only sampling field");
}

}  // namespace

int main() {
    try {
        test_primitive_strictness();
        test_policy_codecs();
        test_provenance_codec_lexical_strictness();
        test_public_codecs();
        test_continuation_operation_validation();
        test_envelope_codec_and_identity();
        test_identity_input_codecs();
        test_closure_variants();
        test_draw_terminal_codec_and_identity();
        test_typed_provenance_authority();
        test_policy_kind_sampling_rng_consistency();
        test_provenance_resolution();
        std::cout << "trajectory_codec_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
