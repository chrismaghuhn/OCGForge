#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ygo/trajectory/codec.hpp"

namespace ygo::trajectory {

std::vector<std::uint8_t> canonical_candidate_trajectory_shard_bytes_v2(
    const CandidateTrajectoryShardV2& value);
DecodeResult<CandidateTrajectoryShardV2> decode_candidate_trajectory_shard_v2(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string candidate_shard_artifact_sha256_v2(const CandidateTrajectoryShardV2& value);

}  // namespace ygo::trajectory
