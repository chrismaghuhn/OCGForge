#include "ygo/trajectory/shard_v3.hpp"

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
        throw std::length_error(std::string("V3 trajectory shard ") + field +
                                " exceeds u32 length");
    }
}

void validate_shard(const CandidateTrajectoryShardV3& value) {
    require_length(value.entries.size(), "entry count");
    std::string previous;
    for (const auto& entry : value.entries) {
        if (!is_lower_hex_digest(entry.episode_envelope_sha256) ||
            (!previous.empty() && entry.episode_envelope_sha256 <= previous) ||
            !decode_episode_envelope_v3(entry.envelope_bytes) ||
            trace::sha256_bytes(entry.envelope_bytes) != entry.episode_envelope_sha256) {
            throw std::invalid_argument("V3 trajectory shard entry is invalid");
        }
        previous = entry.episode_envelope_sha256;
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_candidate_trajectory_shard_bytes_v3(
    const CandidateTrajectoryShardV3& value) {
    validate_shard(value);
    ByteWriter writer;
    writer.string(kTrajectoryShardV3ContractId);
    writer.string(kTrajectoryShardV3ContractId);
    writer.u32be(static_cast<std::uint32_t>(value.entries.size()));
    for (const auto& entry : value.entries) {
        writer.string(entry.episode_envelope_sha256);
        require_length(entry.envelope_bytes.size(), "envelope");
        writer.u32be(static_cast<std::uint32_t>(entry.envelope_bytes.size()));
        writer.raw(entry.envelope_bytes);
    }
    return std::move(writer).take();
}

DecodeResult<CandidateTrajectoryShardV3> decode_candidate_trajectory_shard_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        CandidateTrajectoryShardV3 value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kTrajectoryShardV3ContractId ||
            !reader.string(schema) || schema != kTrajectoryShardV3ContractId ||
            !reader.u32be(count) || count > reader.remaining() / 72) {
            return failure<CandidateTrajectoryShardV3>("malformed V3 shard header");
        }
        value.entries.reserve(count);
        std::string previous;
        for (std::uint32_t index = 0; index < count; ++index) {
            ShardEntryV3 entry;
            std::uint32_t length = 0;
            if (!reader.string(entry.episode_envelope_sha256) ||
                !reader.u32be(length) || !reader.raw(length, entry.envelope_bytes) ||
                !is_lower_hex_digest(entry.episode_envelope_sha256) ||
                (!previous.empty() && entry.episode_envelope_sha256 <= previous)) {
                return failure<CandidateTrajectoryShardV3>("malformed V3 shard entry");
            }
            const auto decoded = decode_episode_envelope_v3(entry.envelope_bytes);
            if (!decoded || trace::sha256_bytes(entry.envelope_bytes) !=
                                  entry.episode_envelope_sha256) {
                return failure<CandidateTrajectoryShardV3>("V3 shard contains invalid envelope");
            }
            previous = entry.episode_envelope_sha256;
            value.entries.push_back(std::move(entry));
        }
        if (!reader.at_end() || canonical_candidate_trajectory_shard_bytes_v3(value) != bytes) {
            return failure<CandidateTrajectoryShardV3>("noncanonical V3 shard");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<CandidateTrajectoryShardV3>(error.what());
    } catch (...) {
        return failure<CandidateTrajectoryShardV3>("V3 shard decode threw");
    }
}

std::string candidate_shard_artifact_sha256_v3(const CandidateTrajectoryShardV3& value) {
    return trace::sha256_bytes(canonical_candidate_trajectory_shard_bytes_v3(value));
}

}  // namespace ygo::trajectory
