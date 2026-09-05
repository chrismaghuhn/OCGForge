#include "ygo/phase6/task7_dataset_authority_provisioning.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ygo::phase6::task7;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_exact_frozen_schedule() {
    const auto schedule = make_task7_collection_schedule(
        "0123456789abcdef0123456789abcdef01234567");
    require(schedule.jobs.size() == 16, "Task7 collection schedule is not 16 jobs");
    require(schedule.seeds == std::vector<std::uint64_t>{4, 6, 8, 9},
            "Task7 collection seed vector changed");
    require(schedule.placements == std::vector<std::string>{"NORMAL", "MIRROR"},
            "Task7 collection placement vector changed");
    require(schedule.starting_players == std::vector<std::uint8_t>{0, 1},
            "Task7 collection starting-player vector changed");
    require(schedule.jobs.front().root_seed == 4 &&
                schedule.jobs.front().placement == "NORMAL" &&
                schedule.jobs.front().starting_player == 0,
            "Task7 collection first job is not canonical");
    require(schedule.jobs[1].root_seed == 4 &&
                schedule.jobs[1].placement == "NORMAL" &&
                schedule.jobs[1].starting_player == 1,
            "Task7 collection second job is not canonical");
    require(schedule.jobs[2].root_seed == 4 &&
                schedule.jobs[2].placement == "MIRROR" &&
                schedule.jobs[2].starting_player == 0,
            "Task7 collection third job is not canonical");
    require(schedule.jobs.back().root_seed == 9 &&
                schedule.jobs.back().placement == "MIRROR" &&
                schedule.jobs.back().starting_player == 1,
            "Task7 collection last job is not canonical");
    require(std::all_of(schedule.jobs.begin(), schedule.jobs.end(), [](const auto& job) {
                return job.engine_process_budget == 20000 &&
                       job.semantic_action_budget == 20000;
            }),
            "Task7 collection run-control bounds changed");
}

void test_schedule_identity_is_canonical_and_source_bound() {
    const auto first = make_task7_collection_schedule(
        "0123456789abcdef0123456789abcdef01234567");
    const auto second = make_task7_collection_schedule(
        "0123456789abcdef0123456789abcdef01234567");
    require(canonical_task7_collection_schedule_bytes(first) ==
                canonical_task7_collection_schedule_bytes(second),
            "identical Task7 schedules changed canonical bytes");
    require(task7_collection_schedule_identity(first) ==
                task7_collection_schedule_identity(second),
            "identical Task7 schedules changed identity");

    const auto other = make_task7_collection_schedule(
        "fedcba9876543210fedcba9876543210fedcba98");
    require(task7_collection_schedule_identity(first) !=
                task7_collection_schedule_identity(other),
            "collector source commit was not bound to schedule identity");
}

void test_schedule_rejects_mutation_and_invalid_source() {
    auto schedule = make_task7_collection_schedule(
        "0123456789abcdef0123456789abcdef01234567");
    std::string error;
    require(validate_task7_collection_schedule(schedule, &error),
            "canonical Task7 schedule failed validation: " + error);
    std::reverse(schedule.jobs.begin(), schedule.jobs.end());
    require(!validate_task7_collection_schedule(schedule, &error),
            "reordered Task7 schedule was accepted");

    auto run_control = make_task7_collection_schedule(
        "0123456789abcdef0123456789abcdef01234567");
    run_control.jobs.front().semantic_action_budget = 1;
    require(!validate_task7_collection_schedule(run_control, &error),
            "one-decision Task4-style run control was accepted");

    bool rejected = false;
    try {
        (void)make_task7_collection_schedule("not-a-git-commit");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "malformed Task7 source commit was accepted");
}

}  // namespace

int main() {
    try {
        test_exact_frozen_schedule();
        test_schedule_identity_is_canonical_and_source_bound();
        test_schedule_rejects_mutation_and_invalid_source();
    } catch (const std::exception& error) {
        return (void)error.what(), 1;
    }
    return 0;
}
