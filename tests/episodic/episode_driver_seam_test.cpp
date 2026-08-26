#include "ygo/environment/episode_driver.hpp"

#include <cassert>
#include <type_traits>

int main() {
    static_assert(std::is_copy_assignable_v<ygo::environment::DriverDecisionBoundary>);
    static_assert(std::is_copy_assignable_v<ygo::environment::DriverBoundary>);

    ygo::environment::EpisodeDriverConfig config;
    config.seed = 17;
    config.starting_player = 1;
    config.engine_process_budget = 2200;
    assert(config.seed == 17);
    assert(config.starting_player == 1);
    assert(config.engine_process_budget == 2200);

    ygo::environment::DriverDecisionBoundary boundary;
    assert(boundary.request == nullptr);
    assert(boundary.observation == nullptr);

    ygo::environment::DriverMetrics metrics;
    assert(metrics.process_call_count == 0);
    assert(metrics.response_submission_count == 0);
    return 0;
}
