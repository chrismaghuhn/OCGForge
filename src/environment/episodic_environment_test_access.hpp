#pragma once

#include <cstdint>
#include <string>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/protocol/continuation.hpp"

namespace ygo::environment::detail {

// Test-only access. This header is intentionally kept outside the installed
// public include tree and has no production caller path.
struct EpisodicEnvironmentTestAccess final {
    static void force_next_reset_failure(EpisodicEnvironment& environment);

    // Runs the exact facade projection used by live Driver boundaries against
    // controlled test-only internal values. It does not mutate the supplied
    // environment or expose the internal binding.
    static DecisionFrame project_frame_for_test(
        EpisodicEnvironment& environment, const ygo::protocol::DecisionRequest& request,
        const ygo::observation::PlayerObservation& observation,
        std::string episode_semantic_id, std::uint64_t decision_index);

    // Populates the same cached public terminal views used by a true Driver
    // terminal, from controlled test-only perspective observations.
    static void install_terminal_views_for_test(
        EpisodicEnvironment& environment,
        const ygo::observation::PlayerObservation& player_zero_observation,
        const ygo::observation::PlayerObservation& player_one_observation,
        std::uint64_t decision_index);
};

}  // namespace ygo::environment::detail
