#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "ygo/trajectory/restricted_evidence_v3.hpp"

namespace ygo::trajectory::replay_v3 {

struct ReplayOptions final {
    std::optional<environment::RunControl> terminal_run_control;
    std::optional<std::string> cancellation_source;
};

struct ReplayResult final {
    bool accepted = false;
    std::uint64_t final_engine_step_index = 0;
    std::string error;

    explicit operator bool() const noexcept { return accepted; }
};

ReplayResult replay_episode_v3(
    const EpisodeEnvelopeV3& envelope,
    const std::optional<RestrictedReplayEvidenceV3>& evidence,
    const ReplayOptions& options);

}  // namespace ygo::trajectory::replay_v3
