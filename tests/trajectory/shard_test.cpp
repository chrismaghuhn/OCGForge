#include "ygo/trajectory/shard.hpp"
#include "ygo/trajectory/storage.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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

std::filesystem::path unique_publication_root() {
#ifdef _WIN32
    const auto process_id = static_cast<unsigned long long>(_getpid());
#else
    const auto process_id = static_cast<unsigned long long>(getpid());
#endif
    const auto base = std::filesystem::temp_directory_path();
    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        const auto candidate = base / (std::string("ocgforge_phase3b_publication_test_") +
                                       std::to_string(process_id) + "_" +
                                       std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            return candidate;
        }
        if (!error && std::filesystem::exists(candidate)) {
            continue;
        }
        if (error == std::errc::file_exists) {
            continue;
        }
        throw std::runtime_error("could not create unique publication test root: " +
                                 error.message());
    }
    throw std::runtime_error("could not find a unique publication test root");
}

struct PublicationTreeCleanup final {
    std::filesystem::path root;
    std::filesystem::path link_like_target;

    ~PublicationTreeCleanup() {
        std::error_code ignored;
        if (!link_like_target.empty()) {
            std::filesystem::remove(link_like_target, ignored);
        }
        ignored.clear();
        std::filesystem::remove_all(root, ignored);
    }
};

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

    auto bad_count = bytes;
    ByteReader count_reader(bad_count);
    require(count_reader.string(ignored) && count_reader.string(ignored) &&
                count_reader.position() + 4 <= bad_count.size(),
            "could not locate shard entry count");
    const auto count_position = count_reader.position();
    bad_count[count_position] = 0xff;
    bad_count[count_position + 1] = 0xff;
    bad_count[count_position + 2] = 0xff;
    bad_count[count_position + 3] = 0xff;
    require(!decode_candidate_trajectory_shard(bad_count),
            "shard accepted a count beyond its input");

    auto truncated = bytes;
    truncated.pop_back();
    require(!decode_candidate_trajectory_shard(truncated), "truncated shard was accepted");
    auto trailing = bytes;
    trailing.push_back(0);
    require(!decode_candidate_trajectory_shard(trailing), "shard trailing bytes were accepted");
    auto corrupt = bytes;
    corrupt.back() ^= 1;
    require(!decode_candidate_trajectory_shard(corrupt), "corrupt shard was accepted");

    for (const auto cut : {std::size_t{0}, std::size_t{1}, bytes.size() / 2,
                           bytes.size() - 1}) {
        if (cut >= bytes.size()) {
            continue;
        }
        const std::vector<std::uint8_t> prefix(bytes.begin(), bytes.begin() +
                                                            static_cast<std::ptrdiff_t>(cut));
        require(!decode_candidate_trajectory_shard(prefix),
                "shard accepted a boundary truncation");
    }
}

void test_atomic_publication() {
    const auto root = unique_publication_root();
    const std::vector<std::uint8_t> bytes = {1, 2, 3, 4, 5};
    const auto digest = trace::sha256_bytes(bytes);
    const auto symlink_target = root / "symlink" /
                               (std::string("trajectory_shard.") + digest + ".bin");
    PublicationTreeCleanup cleanup{root, symlink_target};
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

    const auto non_regular_root = root / "non_regular";
    std::filesystem::create_directories(non_regular_root);
    const auto directory_target = non_regular_root /
                                  (std::string("trajectory_shard.") + digest + ".bin");
    std::filesystem::create_directory(directory_target);
    require(!storage::publish_content_addressed_artifact(
                 non_regular_root, "trajectory_shard", digest, bytes, &error),
            "directory final artifact target was accepted");

    const auto symlink_root = root / "symlink";
    std::filesystem::create_directories(symlink_root);
    const auto symlink_source = symlink_root / "source_dir";
    std::filesystem::create_directory(symlink_source);
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink(symlink_source, symlink_target, symlink_error);
#ifdef _WIN32
    if (symlink_error) {
        const auto command = std::string("cmd /c mklink /J \"") + symlink_target.string() +
                             "\" \"" + symlink_source.string() + "\" >NUL";
        if (std::system(command.c_str()) == 0) {
            symlink_error.clear();
        }
    }
#endif
    require(!symlink_error, "could not create symlink final artifact for negative coverage: " +
                                symlink_error.message());
    require(!storage::publish_content_addressed_artifact(
                 symlink_root, "trajectory_shard", digest, bytes, &error),
            "symbolic-link final artifact target was accepted");

    for (const auto& item : std::filesystem::directory_iterator(root)) {
        require(item.path() == first->path || item.path() == competing_root ||
                    item.path() == non_regular_root || item.path() == symlink_root,
                "temporary publication artifact was left behind");
    }
    for (const auto& item : std::filesystem::directory_iterator(competing_root)) {
        require(item.path() == competing_path,
                "competing publication left a temporary partial artifact behind");
    }
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
