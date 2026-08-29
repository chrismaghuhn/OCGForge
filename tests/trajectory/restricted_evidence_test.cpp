#include "ygo/trajectory/restricted_evidence.hpp"

#include <algorithm>
#include <cstddef>
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

    ByteReader count_reader(bytes);
    require(count_reader.string(error) && count_reader.string(error) &&
                count_reader.string(error) && count_reader.position() + 4 <= bytes.size(),
            "could not locate restricted interrupted count");
    auto bad_interrupted_count = bytes;
    const auto interrupted_count_position = count_reader.position();
    bad_interrupted_count[interrupted_count_position] = 0xff;
    bad_interrupted_count[interrupted_count_position + 1] = 0xff;
    bad_interrupted_count[interrupted_count_position + 2] = 0xff;
    bad_interrupted_count[interrupted_count_position + 3] = 0xff;
    require(!decode_restricted_collection_evidence_bundle(bad_interrupted_count),
            "restricted evidence accepted a count beyond its input");

    ByteReader nested_reader(bytes);
    std::string ignored;
    std::uint32_t interrupted_count = 0;
    require(nested_reader.string(ignored) && nested_reader.string(ignored) &&
                nested_reader.string(ignored) && nested_reader.u32be(interrupted_count) &&
                nested_reader.string(ignored) && nested_reader.position() > 0,
            "could not locate nested restricted evidence hash");
    auto corrupted_nested_hash = bytes;
    corrupted_nested_hash[nested_reader.position() - 1] = 'z';
    require(!decode_restricted_collection_evidence_bundle(corrupted_nested_hash),
            "restricted evidence accepted a corrupted nested envelope hash");

    for (const auto cut : {std::size_t{0}, std::size_t{1}, bytes.size() / 2,
                           bytes.size() - 1}) {
        if (cut >= bytes.size()) {
            continue;
        }
        const std::vector<std::uint8_t> prefix(bytes.begin(), bytes.begin() +
                                                            static_cast<std::ptrdiff_t>(cut));
        require(!decode_restricted_collection_evidence_bundle(prefix),
                "restricted evidence accepted a boundary truncation");
    }
}

void test_rng_material_binding() {
    const auto envelope = stochastic_terminal_envelope(33);
    const auto& attribution = envelope.records.front().policy_rng_decision_provenance;
    const auto entry = shard_entry(envelope);
    CandidateTrajectoryShard shard;
    shard.entries = {entry};
    RestrictedCollectionEvidenceBundle bundle;
    bundle.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256(shard);
    bundle.rng_initializations.push_back(
        RngInitializationEvidenceEntry{attribution.policy_rng_initialization_identity,
                                       {0x11, 0x22, 0x33}});
    const auto resolver = test_provenance_resolver();
    std::string error;
    require(validate_restricted_collection_evidence_bundle(
                bundle, shard, bundle.candidate_shard_artifact_sha256, resolver, &error),
            "registered RNG state codec rejected valid evidence: " + error);
    require(!validate_restricted_collection_evidence_bundle(
                bundle, shard, bundle.candidate_shard_artifact_sha256, &error),
            "default resolver trusted an unregistered test RNG contract");
    auto wrong_material = bundle;
    wrong_material.rng_initializations.front().initialization_material = {9, 9};
    require(!validate_restricted_collection_evidence_bundle(
                wrong_material, shard, bundle.candidate_shard_artifact_sha256, resolver, &error),
            "conflicting RNG initialization material was accepted");
    auto missing = bundle;
    missing.rng_initializations.clear();
    require(!validate_restricted_collection_evidence_bundle(
                missing, shard, bundle.candidate_shard_artifact_sha256, resolver, &error),
            "missing RNG initialization material was accepted");

    auto extra = bundle;
    extra.rng_initializations.push_back(
        RngInitializationEvidenceEntry{"policy_rng_initialization.v1." +
                                           std::string(64, 'f'),
                                       {0x11, 0x22, 0x33}});
    std::sort(extra.rng_initializations.begin(), extra.rng_initializations.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_rng_initialization_identity <
                         right.policy_rng_initialization_identity;
              });
    require(!validate_restricted_collection_evidence_bundle(
                extra, shard, bundle.candidate_shard_artifact_sha256, resolver, &error),
            "extra RNG initialization evidence was accepted");

    auto wrong_identity = bundle;
    wrong_identity.rng_initializations.front().policy_rng_initialization_identity.back() = '0';
    require(!validate_restricted_collection_evidence_bundle(
                wrong_identity, shard, bundle.candidate_shard_artifact_sha256, resolver, &error),
            "wrong RNG initialization identity was accepted");

    const auto cursor = stochastic_terminal_envelope(34, "cursor-unique", PolicyRngMode::Cursor);
    const auto cursor_entry = shard_entry(cursor);
    CandidateTrajectoryShard cursor_shard;
    cursor_shard.entries = {cursor_entry};
    RestrictedCollectionEvidenceBundle cursor_bundle;
    cursor_bundle.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256(cursor_shard);
    cursor_bundle.rng_initializations.push_back(
        RngInitializationEvidenceEntry{
            cursor.records.front().policy_rng_decision_provenance.policy_rng_initialization_identity,
            {0x11, 0x22, 0x33}});
    require(validate_restricted_collection_evidence_bundle(
                cursor_bundle, cursor_shard, cursor_bundle.candidate_shard_artifact_sha256,
                resolver, &error),
            "cursor-capable RNG contract rejected valid initialization evidence: " + error);
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
