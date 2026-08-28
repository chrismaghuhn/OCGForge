#include "ygo/trajectory/receipt.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ygo/trace/sha256.hpp"

namespace {

using namespace ygo;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

AdmissionEntryCommitment commitment(const char* suffix, const std::uint8_t closure_kind) {
    AdmissionEntryCommitment value;
    value.trajectory_record_id = std::string("trajectory_record.v1.") + suffix;
    value.public_gameplay_trajectory_id = std::string("public_gameplay_trajectory.v1.") + suffix;
    value.environment_semantic_id = std::string(64, suffix[0]);
    value.episode_semantic_id = std::string(64, suffix[0] == 'a' ? 'b' : 'a');
    value.episode_envelope_sha256 = std::string(64, suffix[0]);
    value.closure_kind = closure_kind;
    return value;
}

AdmissionReceipt fixture() {
    AdmissionReceipt value;
    value.candidate_shard_artifact_sha256 = std::string(64, '1');
    value.restricted_evidence_artifact_sha256 = std::string(64, '2');
    value.entries = {commitment(
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 0),
                     commitment(
                         "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 1)};
    return value;
}

void test_golden_and_strict_codec() {
    auto receipt = fixture();
    const auto bytes = canonical_admission_receipt_bytes(receipt);
    require(trace::sha256_bytes(bytes) ==
                "43f257c7d6ac3dcbb30d3adbc7398af98364ce483b8fe7d53ea1f2f661e5a084",
            "admission receipt golden mismatch");
    require(admission_receipt_id(receipt) ==
                "admission_receipt.v1.43f257c7d6ac3dcbb30d3adbc7398af98364ce483b8fe7d53ea1f2f661e5a084",
            "admission receipt identity golden mismatch");
    const auto decoded = decode_admission_receipt(bytes);
    require(decoded && canonical_admission_receipt_bytes(*decoded.value) == bytes,
            "admission receipt did not strictly round-trip");

    auto truncated = bytes;
    truncated.pop_back();
    require(!decode_admission_receipt(truncated), "truncated admission receipt was accepted");
    auto trailing = bytes;
    trailing.push_back(0);
    require(!decode_admission_receipt(trailing), "admission receipt trailing bytes were accepted");
    auto corrupt = bytes;
    ByteReader corrupt_reader(corrupt);
    std::string corrupt_ignored;
    std::uint32_t corrupt_count = 0;
    require(corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.u32be(corrupt_count) && corrupt_count == 2 &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored) &&
                corrupt_reader.string(corrupt_ignored),
            "could not locate nested receipt digest for corruption test");
    corrupt[corrupt_reader.position() - 1] = 'z';
    require(!decode_admission_receipt(corrupt), "corrupt admission receipt was accepted");

    ByteReader count_reader(bytes);
    std::string ignored;
    require(count_reader.string(ignored) && count_reader.string(ignored) &&
                count_reader.string(ignored) && count_reader.string(ignored) &&
                count_reader.string(ignored) && count_reader.position() + 4 <= bytes.size(),
            "could not locate admission receipt count");
    auto bad_count = bytes;
    const auto count_position = count_reader.position();
    bad_count[count_position] = 0xff;
    bad_count[count_position + 1] = 0xff;
    bad_count[count_position + 2] = 0xff;
    bad_count[count_position + 3] = 0xff;
    require(!decode_admission_receipt(bad_count),
            "admission receipt accepted a count beyond its input");

    for (const auto cut : {std::size_t{0}, std::size_t{1}, bytes.size() / 2,
                           bytes.size() - 1}) {
        if (cut >= bytes.size()) {
            continue;
        }
        const std::vector<std::uint8_t> prefix(bytes.begin(), bytes.begin() +
                                                            static_cast<std::ptrdiff_t>(cut));
        require(!decode_admission_receipt(prefix),
                "admission receipt accepted a boundary truncation");
    }

    auto unsorted = receipt;
    std::swap(unsorted.entries[0], unsorted.entries[1]);
    bool unsorted_rejected = false;
    try {
        (void)canonical_admission_receipt_bytes(unsorted);
    } catch (const std::exception&) {
        unsorted_rejected = true;
    }
    require(unsorted_rejected, "unsorted receipt commitments were silently sorted");
    auto duplicate = receipt;
    duplicate.entries[1] = duplicate.entries[0];
    bool duplicate_rejected = false;
    try {
        (void)canonical_admission_receipt_bytes(duplicate);
    } catch (const std::exception&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected, "duplicate receipt commitments were silently deduplicated");
}

}  // namespace

int main() {
    try {
        test_golden_and_strict_codec();
        std::cout << "admission receipt tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "admission receipt tests failed: " << error.what() << '\n';
        return 1;
    }
}
