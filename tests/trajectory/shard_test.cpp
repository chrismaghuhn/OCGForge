#include "ygo/trajectory/shard.hpp"
#include "ygo/trajectory/storage.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_fixtures.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

using namespace ygo;
using namespace ygo::trajectory;
using namespace trajectory_test;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

void test_shard_codec() {
    CandidateTrajectoryShard empty;
    const auto empty_bytes = canonical_candidate_trajectory_shard_bytes(empty);
    require(static_cast<bool>(decode_candidate_trajectory_shard(empty_bytes)),
            "empty shard did not decode");

    CandidateTrajectoryShard shard;
    shard.entries = {shard_entry(terminal_envelope(7)), shard_entry(terminal_envelope(8))};
    std::sort(shard.entries.begin(), shard.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });
    const auto bytes = canonical_candidate_trajectory_shard_bytes(shard);
    require(trace::sha256_bytes(bytes) ==
                "e33e286d2386a0f2b40b1c187ca37645d3fd76d505d575b625af0e728dda960f",
            "candidate shard golden mismatch");
    require(!bytes.empty(), "shard codec produced no bytes");
    require(candidate_shard_artifact_sha256(shard) == trace::sha256_bytes(bytes),
            "shard artifact digest is not the canonical byte digest");
    const auto decoded = decode_candidate_trajectory_shard(bytes);
    require(decoded && decoded.value->entries.size() == 2,
            "multi-entry shard did not strictly decode");
    require(canonical_candidate_trajectory_shard_bytes(*decoded.value) == bytes,
            "multi-entry shard did not strictly round-trip");

    auto unsorted = shard;
    std::swap(unsorted.entries[0], unsorted.entries[1]);
    require_throw([&] { (void)canonical_candidate_trajectory_shard_bytes(unsorted); },
                  "unsorted shard was silently canonicalized");
    auto duplicate = shard;
    duplicate.entries[1] = duplicate.entries[0];
    require_throw([&] { (void)canonical_candidate_trajectory_shard_bytes(duplicate); },
                  "duplicate shard entry was silently deduplicated");

    auto wrong_digest = bytes;
    ByteReader reader(wrong_digest);
    std::string ignored;
    std::uint32_t count = 0;
    require(reader.string(ignored) && reader.string(ignored) && reader.u32be(count) && count == 2,
            "could not locate shard digest for corruption test");
    const auto digest_position = reader.position();
    require(!wrong_digest.empty() && digest_position + 4 < wrong_digest.size(),
            "shard digest position is invalid");
    wrong_digest[digest_position + 4] = wrong_digest[digest_position + 4] == '0' ? '1' : '0';
    require(!decode_candidate_trajectory_shard(wrong_digest),
            "shard with a wrong entry digest was accepted");

    auto bad_length = bytes;
    ByteReader length_reader(bad_length);
    require(length_reader.string(ignored) && length_reader.string(ignored) &&
                length_reader.u32be(count) && length_reader.string(ignored),
            "could not locate shard length for corruption test");
    const auto length_position = length_reader.position();
    bad_length[length_position] = 0xff;
    bad_length[length_position + 1] = 0xff;
    bad_length[length_position + 2] = 0xff;
    bad_length[length_position + 3] = 0xff;
    require(!decode_candidate_trajectory_shard(bad_length),
            "shard with a bad entry length was accepted");

    auto truncated = bytes;
    truncated.pop_back();
    require(!decode_candidate_trajectory_shard(truncated), "truncated shard was accepted");
    auto trailing = bytes;
    trailing.push_back(0);
    require(!decode_candidate_trajectory_shard(trailing), "shard trailing bytes were accepted");
    auto corrupt = bytes;
    corrupt.back() ^= 1;
    require(!decode_candidate_trajectory_shard(corrupt), "corrupt shard was accepted");
}

void test_atomic_publication() {
    const auto root = std::filesystem::temp_directory_path() / "ocgforge_phase3b_publication_test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::vector<std::uint8_t> bytes = {1, 2, 3, 4, 5};
    const auto digest = trace::sha256_bytes(bytes);
    std::string error;
    const auto first = storage::publish_content_addressed_artifact(
        root, "trajectory_shard", digest, bytes, &error);
    require(first.has_value(), "first artifact publication failed: " + error);
    require(std::filesystem::exists(first->path), "published artifact is missing");
    const auto second = storage::publish_content_addressed_artifact(
        root, "trajectory_shard", digest, bytes, &error);
    require(second.has_value() && second->path == first->path,
            "identical artifact publication was not idempotent");

    const auto competing_root = root / "competing";
    std::filesystem::create_directories(competing_root);
    const auto competing_path = competing_root /
                                (std::string("trajectory_shard.") + digest + ".bin");
    const std::vector<std::uint8_t> competing_bytes = {0x7f};
    {
        std::ofstream competing(competing_path, std::ios::binary | std::ios::trunc);
        competing.write(reinterpret_cast<const char*>(competing_bytes.data()),
                        static_cast<std::streamsize>(competing_bytes.size()));
    }
    const auto conflict = storage::publish_content_addressed_artifact(
        competing_root, "trajectory_shard", digest, bytes, &error);
    require(!conflict.has_value(), "nonidentical final artifact was overwritten");
    std::ifstream preserved(competing_path, std::ios::binary);
    std::vector<std::uint8_t> preserved_bytes(competing_bytes.size());
    preserved.read(reinterpret_cast<char*>(preserved_bytes.data()),
                   static_cast<std::streamsize>(preserved_bytes.size()));
    require(preserved_bytes == competing_bytes,
            "nonidentical competing artifact was modified during publication");

    for (const auto& item : std::filesystem::directory_iterator(root)) {
        require(item.path() == first->path || item.path() == competing_root,
                "temporary publication artifact was left behind");
    }
    std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main() {
    try {
        test_shard_codec();
        test_atomic_publication();
        std::cout << "trajectory shard tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "trajectory shard tests failed: " << error.what() << '\n';
        return 1;
    }
}
