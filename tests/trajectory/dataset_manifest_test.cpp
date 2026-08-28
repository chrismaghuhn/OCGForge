#include "ygo/trajectory/dataset_manifest.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/trace/sha256.hpp"

namespace {

using namespace ygo;
using namespace ygo::trajectory;
using namespace ygo::trajectory::dataset;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

AdmissionEntryCommitment commitment(const char prefix, const std::uint8_t closure_kind) {
    const std::string suffix(64, prefix);
    AdmissionEntryCommitment value;
    value.trajectory_record_id = "trajectory_record.v1." + suffix;
    value.public_gameplay_trajectory_id = "public_gameplay_trajectory.v1." + suffix;
    value.environment_semantic_id = std::string(64, prefix == 'a' ? '1' : '2');
    value.episode_semantic_id = std::string(64, prefix == 'a' ? '3' : '4');
    value.episode_envelope_sha256 = std::string(64, prefix == 'a' ? '5' : '6');
    value.closure_kind = closure_kind;
    return value;
}

AdmissionReceipt receipt(const std::vector<AdmissionEntryCommitment>& entries,
                         const char shard_prefix) {
    AdmissionReceipt value;
    value.candidate_shard_artifact_sha256 = std::string(64, shard_prefix);
    value.restricted_evidence_artifact_sha256 = std::string(64, shard_prefix == 'a' ? 'b' : 'c');
    value.entries = entries;
    std::sort(value.entries.begin(), value.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.trajectory_record_id < right.trajectory_record_id;
              });
    (void)canonical_admission_receipt_bytes(value);
    return value;
}

DatasetManifest manifest_for(const std::vector<AdmissionReceipt>& receipts) {
    DatasetManifest value;
    for (const auto& current_receipt : receipts) {
        const auto id = admission_receipt_id(current_receipt);
        for (const auto& entry : current_receipt.entries) {
            value.members.push_back(DatasetManifestMember{
                entry.trajectory_record_id,
                entry.public_gameplay_trajectory_id,
                id,
                current_receipt.candidate_shard_artifact_sha256,
                entry.episode_envelope_sha256});
        }
    }
    std::sort(value.members.begin(), value.members.end(),
              [](const auto& left, const auto& right) {
                  return left.trajectory_record_id < right.trajectory_record_id;
              });
    std::vector<std::string> record_ids;
    for (const auto& member : value.members) {
        record_ids.push_back(member.trajectory_record_id);
    }
    value.dataset_semantic_id = dataset::dataset_semantic_id(record_ids);
    return value;
}

void test_identity_and_resharding() {
    const auto first = commitment('a', 0);
    const auto second = commitment('b', 1);
    const auto all_receipt = receipt({first, second}, 'a');
    const auto split_receipt_a = receipt({first}, 'c');
    const auto split_receipt_b = receipt({second}, 'd');
    const auto packing_a = manifest_for({all_receipt});
    const auto packing_b = manifest_for({split_receipt_a, split_receipt_b});
    require(packing_a.dataset_semantic_id == packing_b.dataset_semantic_id,
            "re-sharding changed logical dataset identity");
    require(canonical_dataset_manifest_bytes(packing_a) !=
                canonical_dataset_manifest_bytes(packing_b),
            "different physical provenance produced identical manifest bytes");
    require(validate_dataset_manifest(packing_a, {all_receipt}),
            "single-shard dataset manifest was rejected");
    require(validate_dataset_manifest(packing_b, {split_receipt_a, split_receipt_b}),
            "re-sharded dataset manifest was rejected");
    std::string duplicate_error;
    require(!validate_dataset_manifest(packing_a, {all_receipt, all_receipt}, &duplicate_error),
            "duplicate verified receipts were silently accepted");
    require(trace::sha256_bytes(canonical_dataset_identity_bytes(
                {first.trajectory_record_id, second.trajectory_record_id})) ==
                "245e3d8290609fc2b91378bf857326ce45a16ab70a182ba130539cb4e2dcafb7",
            "dataset identity golden mismatch");
    require(trace::sha256_bytes(canonical_dataset_manifest_bytes(packing_a)) ==
                "58d02c703752ff37e723f98c6f27dee95c905089eb84876e27f5dc7bacb0bb2e",
            "dataset manifest golden mismatch");
}

void test_conflicts_and_strict_codec() {
    const auto first = commitment('a', 0);
    const auto second = commitment('b', 1);
    const auto current_receipt = receipt({first, second}, 'a');
    auto manifest = manifest_for({current_receipt});
    const auto bytes = canonical_dataset_manifest_bytes(manifest);
    const auto decoded = decode_dataset_manifest(bytes);
    require(decoded && canonical_dataset_manifest_bytes(*decoded.value) == bytes,
            "dataset manifest did not strictly round-trip");
    auto truncated = bytes;
    truncated.pop_back();
    require(!decode_dataset_manifest(truncated), "truncated dataset manifest was accepted");
    auto trailing = bytes;
    trailing.push_back(0);
    require(!decode_dataset_manifest(trailing), "dataset manifest trailing bytes were accepted");
    auto corrupt = bytes;
    corrupt[0] = 0xff;
    require(!decode_dataset_manifest(corrupt), "corrupt dataset manifest was accepted");

    auto unknown_receipt = manifest;
    unknown_receipt.members.front().admission_receipt_id =
        "admission_receipt.v1." + std::string(64, 'f');
    std::string error;
    require(!validate_dataset_manifest(unknown_receipt, {current_receipt}, &error),
            "unknown admission receipt was accepted");

    auto conflict = manifest;
    conflict.members.front().candidate_shard_artifact_sha256 = std::string(64, 'f');
    require(!validate_dataset_manifest(conflict, {current_receipt}, &error),
            "conflicting physical provenance was accepted");

    auto invalid_commitment = current_receipt;
    invalid_commitment.entries.front().environment_semantic_id[0] = 'z';
    require(!validate_dataset_manifest(manifest, {invalid_commitment}, &error),
            "invalid receipt commitment digest was accepted as dataset provenance");

    auto duplicate = manifest;
    duplicate.members.push_back(duplicate.members.front());
    bool duplicate_rejected = false;
    try {
        (void)canonical_dataset_manifest_bytes(duplicate);
    } catch (const std::exception&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected, "duplicate dataset membership was accepted");
}

}  // namespace

int main() {
    try {
        test_identity_and_resharding();
        test_conflicts_and_strict_codec();
        std::cout << "dataset manifest tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dataset manifest tests failed: " << error.what() << '\n';
        return 1;
    }
}
