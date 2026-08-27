#pragma once

#include "ygo/environment/episodic_environment.hpp"

namespace ygo::environment::detail {

// Test-only fault injection. It is intentionally outside the certified public
// configuration and has no production caller path.
struct EpisodicEnvironmentTestAccess final {
    static void force_next_reset_failure(EpisodicEnvironment& environment);
};

}  // namespace ygo::environment::detail
