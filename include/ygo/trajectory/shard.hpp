#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ygo/trajectory/codec.hpp"

namespace ygo::trajectory {

inline constexpr std::string_view kTrajectoryShardContractId =
    "ocgforge.trajectory_shard.v1";

std::vector<std::uint8_t> canonical_candidate_trajectory_shard_bytes(
    const CandidateTrajectoryShard& value);
DecodeResult<CandidateTrajectoryShard> decode_candidate_trajectory_shard(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string candidate_shard_artifact_sha256(const CandidateTrajectoryShard& value);

}  // namespace ygo::trajectory
