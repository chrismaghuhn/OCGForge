#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "provenance_test_support.hpp"
#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_golden(const std::vector<std::uint8_t>& bytes,
                    const std::string& expected,
                    const std::string& message) {
    require(trace::sha256_bytes(bytes) == expected, message + " (SHA-256 mismatch)");
}

template <typename T>
void require(const DecodeResult<T>& result, const std::string& message) {
    require(static_cast<bool>(result), message);
}

template <typename Function>
void require_reject(Function&& function, const std::string& message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}

EnvironmentActionCandidate v2_candidate(
    const EnvironmentActionKind action_kind,
    const PublicCardSelectionOperation operation) {
    PublicActionKeyInput key;
    key.action_kind = std::string(environment_action_kind_name(action_kind));
    key.card_selection_operation = operation;
    EnvironmentActionCandidate value;
    value.action_kind = action_kind;
    value.card_selection_operation = operation;
    value.public_action_key = public_action_key_v2(key);
    value.submits_engine_response = true;
    return value;
}

PublicFrameSnapshotV2 v2_frame() {
    const auto config = CertifiedEnvironmentConfig::canonical_v3();
    const auto spec = trajectory_test::episode_spec(19);
    PublicFrameSnapshotV2 value;
    value.episodic_environment_contract_id =
        std::string(kEpisodicEnvironmentV3ContractId);
    value.episode_semantic_id = episode_semantic_id(config, spec);
    value.decision_index = 0;
    value.acting_player = 0;
    value.public_observation = trajectory_test::observation(0, 0, true);
    value.public_observation_digest = public_observation_digest(value.public_observation);
    value.request.kind = EnvironmentDecisionKind::UnselectCard;
    value.request.player = 0;
    value.request.candidates = {
        v2_candidate(EnvironmentActionKind::CardSelection,
                     PublicCardSelectionOperation::Select),
        v2_candidate(EnvironmentActionKind::CardSelection,
                     PublicCardSelectionOperation::Unselect),
        v2_candidate(EnvironmentActionKind::Cancel,
                     PublicCardSelectionOperation::None),
    };
    std::vector<std::string> keys;
    for (const auto& candidate : value.request.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    value.public_candidate_domain_digest =
        public_candidate_domain_digest_v2("unselect_card", keys);
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = value.episode_semantic_id;
    identity.decision_index = value.decision_index;
    identity.acting_player = value.acting_player;
    identity.request_kind = "unselect_card";
    identity.public_observation_digest = value.public_observation_digest;
    identity.public_candidate_domain_digest = value.public_candidate_domain_digest;
    value.public_semantic_decision_id = public_semantic_decision_id_v2(identity);
    return value;
}

EpisodeManifestV2 v2_manifest() {
    const auto config = CertifiedEnvironmentConfig::canonical_v3();
    const auto spec = trajectory_test::episode_spec(19);
    EpisodeManifestV2 value;
    value.environment_semantic_id = environment_semantic_id(config);
    value.environment_identity_input = canonical_environment_identity_bytes(config);
    value.episode_semantic_id = episode_semantic_id(config, spec);
    value.episode_identity_input = canonical_episode_identity_bytes(config, spec);
    value.policy_provenance = trajectory_test::provenance();
    return value;
}

DecisionRecordV2 v2_record() {
    const auto manifest = v2_manifest();
    const auto frame = v2_frame();
    const auto p0 = std::find_if(
        manifest.policy_provenance.participant_assignments.begin(),
        manifest.policy_provenance.participant_assignments.end(),
        [](const auto& assignment) { return assignment.player == 0; });
    require(p0 != manifest.policy_provenance.participant_assignments.end(),
            "fixture lacks player-zero assignment");
    DecisionRecordV2 value;
    value.frame = frame;
    value.selected_public_action_key = frame.request.candidates.front().public_action_key;
    value.transition_class = TransitionClass::AtomicEngineResponse;
    value.successor.kind = SuccessorKind::Terminal;
    value.acting_policy_assignment_id = p0->participant_policy_assignment_id;
    value.policy_rng_decision_provenance =
        trajectory_test::no_rng(value.acting_policy_assignment_id, 0);
    return value;
}

EpisodeEnvelopeV2 v2_envelope() {
    EpisodeEnvelopeV2 value;
    value.manifest = v2_manifest();
    value.records.push_back(v2_record());
    TerminalClosureV2 terminal;
    terminal.winner = 0;
    terminal.win_reason = 1;
    terminal.semantic_action_count = 1;
    terminal.last_decision_index = 0;
    terminal.terminal_view_player_0 = trajectory_test::observation(0, 1);
    terminal.terminal_view_player_0_digest =
        public_observation_digest(terminal.terminal_view_player_0);
    terminal.terminal_view_player_1 = trajectory_test::observation(1, 1);
    terminal.terminal_view_player_1_digest =
        public_observation_digest(terminal.terminal_view_player_1);
    value.closure = std::move(terminal);
    return value;
}

void test_v3_identity_resolver_is_generation_pure() {
    const auto v3_config = CertifiedEnvironmentConfig::canonical_v3();
    const auto v2_config = CertifiedEnvironmentConfig::canonical();
    const auto v3_environment_bytes = canonical_environment_identity_bytes(v3_config);
    const auto v2_environment_bytes = canonical_environment_identity_bytes(v2_config);
    const auto decoded_v3_environment =
        decode_environment_identity_input_v3(v3_environment_bytes);
    require(decoded_v3_environment,
            "V3 environment identity did not decode");
    require(decoded_v3_environment.value->contract_id == kEpisodicEnvironmentV3ContractId,
            "V3 environment resolver returned the wrong contract");
    require(!static_cast<bool>(decode_environment_identity_input(v3_environment_bytes)),
            "V1 resolver accepted V3 environment identity");
    require(decode_environment_identity_input(v2_environment_bytes),
            "V1 environment identity regressed");
    require(!static_cast<bool>(decode_environment_identity_input_v3(v2_environment_bytes)),
            "V3 resolver accepted V2 environment identity");

    const auto spec = trajectory_test::episode_spec(19);
    const auto v3_episode_bytes = canonical_episode_identity_bytes(v3_config, spec);
    const auto v2_episode_bytes = canonical_episode_identity_bytes(v2_config, spec);
    const auto decoded_v3_episode =
        decode_episode_identity_input_v3(v3_episode_bytes, v3_config);
    require(decoded_v3_episode,
            "V3 episode identity did not decode");
    require(decoded_v3_episode.value->contract_id == kEpisodicEnvironmentV3ContractId,
            "V3 episode resolver returned the wrong contract");
    require(!static_cast<bool>(decode_episode_identity_input(v3_episode_bytes, v3_config)),
            "V1 episode resolver accepted V3 generation");
    require(decode_episode_identity_input(v2_episode_bytes, v2_config),
            "V1 episode identity regressed");
    require(!static_cast<bool>(decode_episode_identity_input_v3(v2_episode_bytes, v3_config)),
            "V3 episode resolver accepted V2 generation");
    require(is_current_certified_environment_v3(v3_config),
            "V3 current-environment check failed");
}

void test_v2_candidate_operation_and_key_validation() {
    const auto select = v2_candidate(EnvironmentActionKind::CardSelection,
                                      PublicCardSelectionOperation::Select);
    auto unselect = v2_candidate(EnvironmentActionKind::CardSelection,
                                  PublicCardSelectionOperation::Unselect);
    require(select.public_action_key != unselect.public_action_key,
            "Select and Unselect public keys collided");
    const auto select_bytes = canonical_public_environment_action_candidate_bytes_v2(select);
    const auto unselect_bytes = canonical_public_environment_action_candidate_bytes_v2(unselect);
    require(select_bytes != unselect_bytes,
            "Select and Unselect candidate bytes collided");
    const auto decoded_select = decode_public_environment_action_candidate_v2(select_bytes);
    require(decoded_select,
            "Select candidate did not decode");
    require(decoded_select.value->card_selection_operation ==
                PublicCardSelectionOperation::Select,
            "Select operation code was not preserved");
    const auto decoded_unselect =
        decode_public_environment_action_candidate_v2(unselect_bytes);
    require(decoded_unselect,
            "Unselect candidate did not decode");
    require(decoded_unselect.value->card_selection_operation ==
                PublicCardSelectionOperation::Unselect,
            "Unselect operation code was not preserved");

    auto unknown_operation = select_bytes;
    ByteReader operation_reader(unknown_operation);
    std::string ignored;
    require(operation_reader.string(ignored) && operation_reader.string(ignored),
            "V2 candidate header could not be read");
    unknown_operation[operation_reader.position()] = 0xff;
    require(!static_cast<bool>(
                decode_public_environment_action_candidate_v2(unknown_operation)),
            "unknown V2 operation code was accepted");

    unselect.card_selection_operation = PublicCardSelectionOperation::None;
    require_reject(
        [&] { (void)canonical_public_environment_action_candidate_bytes_v2(unselect); },
        "invalid native operation was not rejected");

    auto v1_key = select;
    PublicActionKeyInput v1_input;
    v1_input.action_kind = "card_selection";
    v1_key.public_action_key = public_action_key(v1_input);
    require_reject(
        [&] { (void)canonical_public_environment_action_candidate_bytes_v2(v1_key); },
        "V1 action key was accepted by V2 candidate codec");
    require_reject(
        [&] { (void)canonical_public_environment_action_candidate_bytes(v2_candidate(
                   EnvironmentActionKind::CardSelection,
                   PublicCardSelectionOperation::Select)); },
        "V2 action key was accepted by V1 candidate codec");
}

void test_v2_frame_identity_and_domain_validation() {
    const auto frame = v2_frame();
    const auto request_bytes =
        canonical_public_environment_decision_request_bytes_v2(frame.request);
    const auto decoded_request =
        decode_public_environment_decision_request_v2(request_bytes);
    require(decoded_request,
            "V2 request did not decode");
    require(decoded_request.value->candidates.size() == frame.request.candidates.size() &&
                decoded_request.value->candidates[0].public_action_key ==
                    frame.request.candidates[0].public_action_key &&
                decoded_request.value->candidates[1].public_action_key ==
                    frame.request.candidates[1].public_action_key &&
                decoded_request.value->candidates[2].public_action_key ==
                    frame.request.candidates[2].public_action_key,
            "V2 request candidate order changed");
    const auto bytes = canonical_public_frame_snapshot_bytes_v2(frame);
    const auto decoded = decode_public_frame_snapshot_v2(bytes);
    require(decoded, "V2 frame did not decode");
    require(decoded.value->public_semantic_decision_id == frame.public_semantic_decision_id,
            "V2 decision identity changed across frame round-trip");

    auto invalid = frame;
    invalid.public_candidate_domain_digest = std::string(64, '0');
    require_reject(
        [&] { (void)canonical_public_frame_snapshot_bytes_v2(invalid); },
        "V2 frame accepted mismatched candidate-domain digest");

    invalid = frame;
    invalid.request.candidates[1].card_selection_operation =
        PublicCardSelectionOperation::None;
    require_reject(
        [&] { (void)canonical_public_frame_snapshot_bytes_v2(invalid); },
        "V2 frame accepted invalid UnselectCard operation");

    invalid = frame;
    invalid.request.candidates[0].public_action_key = public_action_key(
        PublicActionKeyInput{"card_selection", std::nullopt, std::nullopt, std::nullopt,
                             std::nullopt, std::nullopt, std::nullopt, std::nullopt, ""});
    require_reject(
        [&] { (void)canonical_public_frame_snapshot_bytes_v2(invalid); },
        "V2 frame accepted V1 candidate key");

    invalid = frame;
    invalid.episodic_environment_contract_id =
        std::string(kEpisodicEnvironmentV2ContractId);
    require_reject(
        [&] { (void)canonical_public_frame_snapshot_bytes_v2(invalid); },
        "V2 frame accepted historical environment contract");

    invalid = frame;
    invalid.request.candidates[1] = invalid.request.candidates[0];
    require_reject(
        [&] { (void)canonical_public_frame_snapshot_bytes_v2(invalid); },
        "V2 frame accepted duplicate candidate domain");
}

void test_v2_record_manifest_and_envelope_round_trip() {
    const auto record = v2_record();
    const auto public_record_bytes = canonical_public_decision_record_bytes_v2(record);
    require(decode_public_decision_record_v2(public_record_bytes),
            "V2 public decision record did not decode");
    const auto collection_bytes = canonical_collection_decision_record_bytes_v2(record);
    require(decode_collection_decision_record_v2(collection_bytes),
            "V2 collection decision record did not decode");
    auto collection_trailing = collection_bytes;
    collection_trailing.push_back(0);
    require(!static_cast<bool>(decode_collection_decision_record_v2(collection_trailing)),
            "V2 collection record accepted trailing bytes");

    auto invalid_record = record;
    PublicActionKeyInput absent_key;
    absent_key.action_kind = "card_selection";
    invalid_record.selected_public_action_key = public_action_key_v2(absent_key);
    require_reject(
        [&] { (void)canonical_public_decision_record_bytes_v2(invalid_record); },
        "V2 record accepted a selected key absent from its domain");

    const auto manifest = v2_manifest();
    const auto manifest_bytes = canonical_episode_manifest_bytes_v2(manifest);
    require(decode_episode_manifest_v2(manifest_bytes),
            "V2 manifest did not decode");

    const auto envelope = v2_envelope();
    const auto envelope_bytes = canonical_episode_envelope_bytes_v2(envelope);
    const auto decoded = decode_episode_envelope_v2(envelope_bytes);
    require(decoded, "V2 envelope did not decode");
    require(canonical_episode_envelope_bytes_v2(*decoded.value) == envelope_bytes,
            "V2 envelope decode was not canonically stable");

    auto trailing = envelope_bytes;
    trailing.push_back(0);
    require(!static_cast<bool>(decode_episode_envelope_v2(trailing)),
            "V2 envelope accepted trailing bytes");

    InterruptedClosureV2 interrupted;
    FailedClosureV2 failed;
    require(decode_episode_closure_v2(canonical_episode_closure_bytes_v2(interrupted)),
            "V2 interrupted closure did not decode");
    require(decode_episode_closure_v2(canonical_episode_closure_bytes_v2(failed)),
            "V2 failed closure did not decode");
}

void test_v2_restricted_evidence_round_trip() {
    RestrictedReplayEvidenceV2 value;
    value.episode_semantic_id = v2_manifest().episode_semantic_id;
    value.engine_process_budget = 10;
    value.semantic_action_budget = 20;
    value.observed_engine_process_count = 3;
    value.observed_semantic_action_count = 4;
    value.final_engine_step_index = 5;
    const auto bytes = canonical_restricted_replay_evidence_bytes_v2(value);
    require(decode_restricted_replay_evidence_v2(bytes),
            "V2 restricted evidence did not decode");
}

void test_v2_goldens() {
    const auto none =
        canonical_public_environment_action_candidate_bytes_v2(
            v2_candidate(EnvironmentActionKind::CardSelection,
                         PublicCardSelectionOperation::None));
    const auto select =
        canonical_public_environment_action_candidate_bytes_v2(
            v2_candidate(EnvironmentActionKind::CardSelection,
                         PublicCardSelectionOperation::Select));
    const auto unselect =
        canonical_public_environment_action_candidate_bytes_v2(
            v2_candidate(EnvironmentActionKind::CardSelection,
                         PublicCardSelectionOperation::Unselect));
    const auto frame = canonical_public_frame_snapshot_bytes_v2(v2_frame());
    const auto record = canonical_public_decision_record_bytes_v2(v2_record());
    const auto collection = canonical_collection_decision_record_bytes_v2(v2_record());
    const auto manifest = canonical_episode_manifest_bytes_v2(v2_manifest());
    auto terminal = v2_envelope().closure;
    InterruptedClosureV2 interrupted;
    FailedClosureV2 failed;
    const auto terminal_bytes = canonical_episode_closure_bytes_v2(terminal);
    const auto interrupted_bytes = canonical_episode_closure_bytes_v2(interrupted);
    const auto failed_bytes = canonical_episode_closure_bytes_v2(failed);
    const auto envelope = canonical_episode_envelope_bytes_v2(v2_envelope());
    RestrictedReplayEvidenceV2 evidence;
    evidence.episode_semantic_id = v2_manifest().episode_semantic_id;
    evidence.engine_process_budget = 10;
    evidence.semantic_action_budget = 20;
    evidence.observed_engine_process_count = 3;
    evidence.observed_semantic_action_count = 4;
    evidence.final_engine_step_index = 5;
    const auto restricted = canonical_restricted_replay_evidence_bytes_v2(evidence);
    require_golden(none,
                   "8db01fb2db00bfb0a32dedb4f4c07617147aa993f4318e6aae97144e280bdcfa",
                   "V2 None candidate golden");
    require_golden(select,
                   "bcee9596227aa541970b8eb4ce7b4525041bd5a8ea6c2679c00c89ecb7dd4678",
                   "V2 Select candidate golden");
    require_golden(unselect,
                   "f524181c0d83a3727564f3c19d777c82ec8c98260811799ca7d63a5dea6911f8",
                   "V2 Unselect candidate golden");
    require_golden(frame,
                   "73f2fc074c6c6a3e38c572e01599d82815343f2dfe936dd4fe0db2d53d3a1fb1",
                   "V2 frame golden");
    require_golden(record,
                   "376e30f0daa017f6f43cef807faf24101e93900836c07135ad63fc293adc57b3",
                   "V2 public record golden");
    require_golden(collection,
                   "433614916ff88576c59a22593138239c07e1abaa8d3eb2cf38ee4276cb0572c7",
                   "V2 collection record golden");
    require_golden(manifest,
                   "50a10aa10d0c94af3d75dab54059a5ce580001c18d8a7b3e3d08f4fa7cb785d3",
                   "V2 manifest golden");
    require_golden(terminal_bytes,
                   "9681f79396c15810f5337b72f5fc9cd819a18258013ea570db281b415e53a1c0",
                   "V2 terminal closure golden");
    require_golden(interrupted_bytes,
                   "fa2a05a93677cf34842b6e36529a6321966a57be9d7255a9812d78092d82a19f",
                   "V2 interrupted closure golden");
    require_golden(failed_bytes,
                   "8a57f5979bcbc3fb020c00518d199edd4b88096989e4267a1bd9736f4d29e3e3",
                   "V2 failed closure golden");
    require_golden(envelope,
                   "01d97f35d81c8a0bc160eb0dc0e4007aeae97ae35d8d53263be0ed5b6fb6ad7e",
                   "V2 envelope golden");
    require_golden(restricted,
                   "e5e00d15745ad110ef80c636c5ea022da5d7a6fd122e4c2638d2bd37824734b0",
                   "V2 restricted evidence golden");
}

}  // namespace

int main() {
    try {
        test_v3_identity_resolver_is_generation_pure();
        test_v2_candidate_operation_and_key_validation();
        test_v2_frame_identity_and_domain_validation();
        test_v2_record_manifest_and_envelope_round_trip();
        test_v2_restricted_evidence_round_trip();
        test_v2_goldens();
        std::cout << "trusted trajectory V2 codec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
