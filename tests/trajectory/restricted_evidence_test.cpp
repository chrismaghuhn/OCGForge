#include "ygo/trajectory/restricted_evidence.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::trajectory;
using namespace trajectory_test;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

RestrictedReplayEvidence evidence_for(const EpisodeEnvelope& envelope,
                                      const InterruptionReason reason) {
    RestrictedReplayEvidence value;
    value.episode_semantic_id = envelope.manifest.episode_semantic_id;
    value.interruption_reason = reason;
    value.engine_process_budget = 20;
    value.semantic_action_budget = 10;
    value.observed_engine_process_count = 4;
    value.observed_semantic_action_count = envelope.records.size();
    value.final_engine_step_index = 9;
    return value;
}

void test_bundle_codec_and_cross_references() {
    const auto interrupted = interrupted_envelope(31, true);
    const auto interrupted_entry = shard_entry(interrupted);
    CandidateTrajectoryShard shard;
    shard.entries = {interrupted_entry};
    const auto shard_artifact = candidate_shard_artifact_sha256(shard);

    RestrictedCollectionEvidenceBundle bundle;
    bundle.candidate_shard_artifact_sha256 = shard_artifact;
    bundle.interrupted_episodes.push_back(
        InterruptedEvidenceEntry{interrupted_entry.episode_envelope_sha256,
                                  evidence_for(interrupted, InterruptionReason::AdministrativeCancel)});
    const auto bytes = canonical_restricted_collection_evidence_bundle_bytes(bundle);
    require(trace::sha256_bytes(bytes) ==
                "a780a368282fb61400d3c8556e11d233c89764d3985bf7ecffe239ae4c8aa7a6",
            "restricted evidence bundle golden mismatch");
    require(static_cast<bool>(decode_restricted_collection_evidence_bundle(bytes)),
            "restricted evidence bundle did not decode");
    require(restricted_collection_evidence_artifact_sha256(bundle) == trace::sha256_bytes(bytes),
            "restricted evidence artifact hash is not the canonical byte hash");
    std::string error;
    require(validate_restricted_collection_evidence_bundle(bundle, shard, shard_artifact, &error),
            "valid restricted evidence bundle was rejected: " + error);

    auto missing = bundle;
    missing.interrupted_episodes.clear();
    require(!validate_restricted_collection_evidence_bundle(missing, shard, shard_artifact, &error),
            "missing interrupted evidence was accepted");
    auto extra = bundle;
    extra.interrupted_episodes.push_back(bundle.interrupted_episodes.front());
    // The duplicate is rejected by the strict writer before admission.
    bool duplicate_rejected = false;
    try {
        (void)canonical_restricted_collection_evidence_bundle_bytes(extra);
    } catch (const std::exception&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected, "duplicate interrupted evidence was accepted");

    CandidateTrajectoryShard terminal_shard;
    terminal_shard.entries = {shard_entry(terminal_envelope(32))};
    auto non_interrupted = bundle;
    non_interrupted.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256(terminal_shard);
    non_interrupted.interrupted_episodes.front().episode_envelope_sha256 =
        terminal_shard.entries.front().episode_envelope_sha256;
    require(!validate_restricted_collection_evidence_bundle(
                non_interrupted, terminal_shard,
                non_interrupted.candidate_shard_artifact_sha256, &error),
            "evidence for a non-interrupted episode was accepted");

    auto wrong_binding = bundle;
    wrong_binding.candidate_shard_artifact_sha256 = std::string(64, '0');
    require(!validate_restricted_collection_evidence_bundle(
                wrong_binding, shard, shard_artifact, &error),
            "evidence with a wrong shard binding was accepted");

    auto truncated = bytes;
    truncated.pop_back();
    require(!decode_restricted_collection_evidence_bundle(truncated),
            "truncated restricted evidence was accepted");
    auto trailing = bytes;
    trailing.push_back(0);
    require(!decode_restricted_collection_evidence_bundle(trailing),
            "restricted evidence trailing bytes were accepted");
    auto corrupt = bytes;
    corrupt.back() ^= 1;
    require(!decode_restricted_collection_evidence_bundle(corrupt),
            "corrupt restricted evidence was accepted");
}

void test_rng_material_binding() {
    auto envelope = terminal_envelope(33);
    const auto policy = deterministic_artifact();
    const auto assignments = provenance().participant_assignments;
    const auto assignment = std::find_if(assignments.begin(), assignments.end(),
                                         [](const auto& value) { return value.player == 0; });
    PolicyRngInitializationIdentity initialization;
    initialization.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    initialization.policy_rng_stream_id = "main";
    initialization.initialization_material = {0x11, 0x22, 0x33};
    initialization.policy_rng_initialization_identity =
        compute_policy_rng_initialization_id(initialization);
    PolicyRngStreamIdentity stream;
    stream.policy_artifact_id = policy.policy_artifact_id;
    stream.participant_policy_assignment_id = assignment->participant_policy_assignment_id;
    stream.policy_rng_contract_identity = initialization.policy_rng_contract_identity;
    stream.policy_rng_stream_id = initialization.policy_rng_stream_id;
    stream.policy_rng_initialization_identity = initialization.policy_rng_initialization_identity;
    stream.policy_rng_identity = compute_policy_rng_stream_id(stream);
    auto& attribution = envelope.records.front().policy_rng_decision_provenance;
    attribution.policy_rng_identity = stream.policy_rng_identity;
    attribution.policy_rng_contract_identity = stream.policy_rng_contract_identity;
    attribution.policy_rng_stream_id = stream.policy_rng_stream_id;
    attribution.policy_rng_initialization_identity = stream.policy_rng_initialization_identity;
    attribution.mode = PolicyRngMode::State;
    attribution.pre_state = std::vector<std::uint8_t>{1, 2};
    attribution.post_state = std::vector<std::uint8_t>{3, 4};
    const auto entry = shard_entry(envelope);
    CandidateTrajectoryShard shard;
    shard.entries = {entry};
    RestrictedCollectionEvidenceBundle bundle;
    bundle.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256(shard);
    bundle.rng_initializations.push_back(
        RngInitializationEvidenceEntry{initialization.policy_rng_initialization_identity,
                                       initialization.initialization_material});
    std::string error;
    require(!validate_restricted_collection_evidence_bundle(
                bundle, shard, bundle.candidate_shard_artifact_sha256, &error),
            "unregistered RNG state codec was accepted as trusted evidence");
    require(error.find("registered canonical state codec") != std::string::npos,
            "unregistered RNG state rejection was not explicit");
    auto wrong_material = bundle;
    wrong_material.rng_initializations.front().initialization_material = {9, 9};
    require(!validate_restricted_collection_evidence_bundle(
                wrong_material, shard, bundle.candidate_shard_artifact_sha256, &error),
            "conflicting RNG initialization material was accepted");
    auto missing = bundle;
    missing.rng_initializations.clear();
    require(!validate_restricted_collection_evidence_bundle(
                missing, shard, bundle.candidate_shard_artifact_sha256, &error),
            "missing RNG initialization material was accepted");
}

}  // namespace

int main() {
    try {
        test_bundle_codec_and_cross_references();
        test_rng_material_binding();
        std::cout << "restricted evidence tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "restricted evidence tests failed: " << error.what() << '\n';
        return 1;
    }
}
