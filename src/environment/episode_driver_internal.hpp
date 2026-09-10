#pragma once

#include <memory>

#include "ygo/environment/episode_driver.hpp"

namespace ygo::environment::detail {

struct EpisodicEnvironmentDriverAccess final {
    static std::unique_ptr<EpisodeDriver> make_v4(EpisodeDriverConfig config);
};

}  // namespace ygo::environment::detail
