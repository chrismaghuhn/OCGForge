#include "ygo/trajectory/shard_v2.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory {
namespace {

template <typename T>
DecodeResult<T> failure(std::string message) noexcept {
    DecodeResult<T> result;
    result.error = DecodeError{std::move(message)};
    return result;
}

template <typename T>
DecodeResult<T> success(T value) noexcept {
    DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

void require_length(const std::size_t size, const char* field) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(std::string("V2 trajectory shard ") + field +
                                " exceeds u32 length");
    }
}

void validate_shard(const CandidateTrajectoryShardV2& value) {
    require_length(value.entries.size(), "entry count");
    std::string previous;
    for (const auto& entry : value.entries) {
        if (!is_lower_hex_digest(entry.episode_envelope_sha256) ||
            (!previous.empty() && entry.episode_envelope_sha256 <= previous) ||
            !decode_episode_envelope_v2(entry.envelope_bytes) ||
            trace::sha256_bytes(entry.envelope_bytes) != entry.episode_envelope_sha256) {
            throw std::invalid_argument("V2 trajectory shard entry is invalid");
        }
        previous = entry.episode_envelope_sha256;
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_candidate_trajectory_shard_bytes_v2(
    const CandidateTrajectoryShardV2& value) {
    validate_shard(value);
    ByteWriter writer;
    writer.string(kTrajectoryShardV2ContractId);
    writer.string(kTrajectoryShardV2ContractId);
    writer.u32be(static_cast<std::uint32_t>(value.entries.size()));
    for (const auto& entry : value.entries) {
        writer.string(entry.episode_envelope_sha256);
        require_length(entry.envelope_bytes.size(), "envelope");
        writer.u32be(static_cast<std::uint32_t>(entry.envelope_bytes.size()));
        writer.raw(entry.envelope_bytes);
    }
    return std::move(writer).take();
}

DecodeResult<CandidateTrajectoryShardV2> decode_candidate_trajectory_shard_v2(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        CandidateTrajectoryShardV2 value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kTrajectoryShardV2ContractId ||
            !reader.string(schema) || schema != kTrajectoryShardV2ContractId ||
            !reader.u32be(count) || count > reader.remaining() / 72) {
            return failure<CandidateTrajectoryShardV2>("malformed V2 shard header");
        }
        value.entries.reserve(count);
        std::string previous;
        for (std::uint32_t index = 0; index < count; ++index) {
            ShardEntryV2 entry;
            std::uint32_t length = 0;
            if (!reader.string(entry.episode_envelope_sha256) ||
                !reader.u32be(length) || !reader.raw(length, entry.envelope_bytes) ||
                !is_lower_hex_digest(entry.episode_envelope_sha256) ||
                (!previous.empty() && entry.episode_envelope_sha256 <= previous)) {
                return failure<CandidateTrajectoryShardV2>("malformed V2 shard entry");
            }
            const auto decoded = decode_episode_envelope_v2(entry.envelope_bytes);
            if (!decoded || trace::sha256_bytes(entry.envelope_bytes) !=
                                  entry.episode_envelope_sha256) {
                return failure<CandidateTrajectoryShardV2>("V2 shard contains invalid envelope");
            }
            previous = entry.episode_envelope_sha256;
            value.entries.push_back(std::move(entry));
        }
        if (!reader.at_end() || canonical_candidate_trajectory_shard_bytes_v2(value) != bytes) {
            return failure<CandidateTrajectoryShardV2>("noncanonical V2 shard");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<CandidateTrajectoryShardV2>(error.what());
    } catch (...) {
        return failure<CandidateTrajectoryShardV2>("V2 shard decode threw");
    }
}

std::string candidate_shard_artifact_sha256_v2(const CandidateTrajectoryShardV2& value) {
    return trace::sha256_bytes(canonical_candidate_trajectory_shard_bytes_v2(value));
}

}  // namespace ygo::trajectory
