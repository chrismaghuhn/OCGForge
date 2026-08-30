#include "ygo/trajectory/shard.hpp"

#include <cstddef>
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
        throw std::length_error(std::string("trajectory shard ") + field + " exceeds u32 length");
    }
}

void validate_shard(const CandidateTrajectoryShard& value) {
    require_length(value.entries.size(), "entry count");
    std::string previous_digest;
    for (const auto& entry : value.entries) {
        if (!is_lower_hex_digest(entry.episode_envelope_sha256)) {
            throw std::invalid_argument("trajectory shard entry has an invalid envelope digest");
        }
        if (!previous_digest.empty() && entry.episode_envelope_sha256 <= previous_digest) {
            throw std::invalid_argument("trajectory shard entries are not strictly digest sorted");
        }
        previous_digest = entry.episode_envelope_sha256;
        const auto decoded = decode_episode_envelope(entry.envelope_bytes);
        if (!decoded) {
            throw std::invalid_argument("trajectory shard entry has an invalid envelope");
        }
        if (trace::sha256_bytes(entry.envelope_bytes) != entry.episode_envelope_sha256) {
            throw std::invalid_argument("trajectory shard entry digest mismatch");
        }
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_candidate_trajectory_shard_bytes(
    const CandidateTrajectoryShard& value) {
    validate_shard(value);
    ByteWriter writer;
    writer.string(kTrajectoryShardContractId);
    writer.string(kTrajectoryShardContractId);
    writer.u32be(static_cast<std::uint32_t>(value.entries.size()));
    for (const auto& entry : value.entries) {
        writer.string(entry.episode_envelope_sha256);
        require_length(entry.envelope_bytes.size(), "envelope");
        writer.u32be(static_cast<std::uint32_t>(entry.envelope_bytes.size()));
        writer.raw(entry.envelope_bytes);
    }
    return std::move(writer).take();
}

DecodeResult<CandidateTrajectoryShard> decode_candidate_trajectory_shard(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        CandidateTrajectoryShard value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kTrajectoryShardContractId ||
            !reader.string(schema) || schema != kTrajectoryShardContractId ||
            !reader.u32be(count)) {
            return failure<CandidateTrajectoryShard>("malformed trajectory shard header");
        }
        // Every entry has at least a 64-byte digest string and its u32 length.
        if (count > reader.remaining() / 72) {
            return failure<CandidateTrajectoryShard>("trajectory shard entry count exceeds input");
        }
        value.entries.reserve(count);
        std::string previous_digest;
        for (std::uint32_t index = 0; index < count; ++index) {
            ShardEntry entry;
            std::uint32_t envelope_length = 0;
            if (!reader.string(entry.episode_envelope_sha256) ||
                !reader.u32be(envelope_length) || !reader.raw(envelope_length, entry.envelope_bytes)) {
                return failure<CandidateTrajectoryShard>("truncated trajectory shard entry");
            }
            if (!is_lower_hex_digest(entry.episode_envelope_sha256) ||
                (!previous_digest.empty() && entry.episode_envelope_sha256 <= previous_digest)) {
                return failure<CandidateTrajectoryShard>("unsorted or duplicate trajectory shard entry");
            }
            previous_digest = entry.episode_envelope_sha256;
            const auto decoded = decode_episode_envelope(entry.envelope_bytes);
            if (!decoded) {
                return failure<CandidateTrajectoryShard>("trajectory shard contains invalid envelope");
            }
            if (trace::sha256_bytes(entry.envelope_bytes) != entry.episode_envelope_sha256) {
                return failure<CandidateTrajectoryShard>("trajectory shard entry digest mismatch");
            }
            value.entries.push_back(std::move(entry));
        }
        if (!reader.at_end()) {
            return failure<CandidateTrajectoryShard>("trajectory shard has trailing bytes");
        }
        if (canonical_candidate_trajectory_shard_bytes(value) != bytes) {
            return failure<CandidateTrajectoryShard>("noncanonical trajectory shard");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<CandidateTrajectoryShard>(error.what());
    } catch (...) {
        return failure<CandidateTrajectoryShard>("trajectory shard decode threw");
    }
}

std::string candidate_shard_artifact_sha256(const CandidateTrajectoryShard& value) {
    return trace::sha256_bytes(canonical_candidate_trajectory_shard_bytes(value));
}

}  // namespace ygo::trajectory
