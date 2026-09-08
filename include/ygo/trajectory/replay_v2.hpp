#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "ygo/trajectory/types.hpp"

namespace ygo::trajectory::replay_v2 {

struct ReplayOptions final {
    // Terminal replay has no persisted run-control values in the public
    // trajectory.  Admission supplies the exact control used for replay.
    std::optional<environment::RunControl> terminal_run_control;
    // Administrative interruption is an explicit replay input and is never
    // taken from canonical public trajectory bytes.
    std::optional<std::string> cancellation_source;
};

struct ReplayResult final {
    bool accepted = false;
    std::uint64_t final_engine_step_index = 0;
    std::string error;

    explicit operator bool() const noexcept { return accepted; }
};

ReplayResult replay_episode_v2(
    const EpisodeEnvelopeV2& envelope,
    const std::optional<RestrictedReplayEvidenceV2>& evidence,
    const ReplayOptions& options);

}  // namespace ygo::trajectory::replay_v2
