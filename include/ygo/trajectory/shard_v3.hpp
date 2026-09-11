#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ygo/trajectory/codec_v3.hpp"

namespace ygo::trajectory {

inline constexpr char kTrajectoryShardV3ContractId[] =
    "ocgforge.trajectory_shard.v3";

struct ShardEntryV3 final {
    std::string episode_envelope_sha256;
    std::vector<std::uint8_t> envelope_bytes;
};

struct CandidateTrajectoryShardV3 final {
    std::vector<ShardEntryV3> entries;
};

std::vector<std::uint8_t> canonical_candidate_trajectory_shard_bytes_v3(
    const CandidateTrajectoryShardV3& value);
DecodeResult<CandidateTrajectoryShardV3> decode_candidate_trajectory_shard_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string candidate_shard_artifact_sha256_v3(
    const CandidateTrajectoryShardV3& value);

}  // namespace ygo::trajectory
