#include "ygo/phase6/task7_v2_forensic_diagnostics.hpp"

#include <filesystem>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_empty_run_is_fully_classified() {
    const auto schedule = ygo::phase6::make_task7_collection_schedule_v2(
        "2ce354d26402fc9283137be3e74774810dc56986");
    const auto inspection = ygo::phase6::inspect_task7_v2_job_run(
        schedule.jobs.front(), ygo::policy::TeacherRunnerV3TrajectoryRunResult{});
    require(!inspection.eligible, "empty run was eligible");
    require(!inspection.envelope_present && !inspection.candidate_shard_present &&
                !inspection.admission_receipt_present,
            "empty run presence flags were incomplete");
    require(inspection.failed_conditions.size() >= 6,
            "empty run did not expose all missing A5 outputs");
    require(inspection.first_failed_condition == "ENVELOPE_ABSENT" ||
                inspection.first_failed_condition == "CANDIDATE_SHARD_ABSENT",
            "empty run first failure was not a missing output");
}

void compare_public_boundaries(const ygo::environment::ResetResult& left,
                               const ygo::environment::ResetResult& right) {
    require(left.index() == right.index(), "diagnostic observer changed reset boundary kind");
    const auto* left_frame = std::get_if<ygo::environment::ResetAccepted>(&left);
    const auto* right_frame = std::get_if<ygo::environment::ResetAccepted>(&right);
    if (left_frame == nullptr || right_frame == nullptr) return;
    require(left_frame->next.index() == right_frame->next.index(),
            "diagnostic observer changed accepted boundary kind");
    const auto* left_decision =
        std::get_if<ygo::environment::DecisionFrame>(&left_frame->next);
    const auto* right_decision =
        std::get_if<ygo::environment::DecisionFrame>(&right_frame->next);
    if (left_decision == nullptr || right_decision == nullptr) return;
    require(left_decision->episode_semantic_id == right_decision->episode_semantic_id &&
                left_decision->public_semantic_decision_id ==
                    right_decision->public_semantic_decision_id &&
                left_decision->public_observation_digest ==
                    right_decision->public_observation_digest &&
                left_decision->public_candidate_domain_digest ==
                    right_decision->public_candidate_domain_digest &&
                left_decision->request.candidates.size() ==
                    right_decision->request.candidates.size(),
            "diagnostic observer changed public reset semantics");
    for (std::size_t index = 0; index < left_decision->request.candidates.size(); ++index) {
        require(left_decision->request.candidates[index].public_action_key ==
                    right_decision->request.candidates[index].public_action_key,
                "diagnostic observer changed candidate order or identity");
    }
}

void test_observer_is_semantically_non_interfering() {
    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical_v3();
    auto left_factory = ygo::environment::EpisodicEnvironment::create(config);
    auto right_factory = ygo::environment::EpisodicEnvironment::create(config);
    auto* left = std::get_if<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(
        &left_factory);
    auto* right = std::get_if<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(
        &right_factory);
    require(left != nullptr && right != nullptr && *left != nullptr && *right != nullptr,
            "V3 environment fixture could not be created");
    std::size_t event_count = 0;
    (*right)->set_diagnostic_observer(
        [&event_count](const ygo::diagnostics::Task7DiagnosticEvent&) { ++event_count; });
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = std::string(ygo::environment::kEpisodicEnvironmentV3ContractId);
    spec.root_seed = 2;
    spec.seat_assignment = ygo::environment::SeatAssignment::Normal;
    spec.starting_player = 0;
    ygo::environment::RunControl control;
    control.engine_process_budget = 64;
    control.semantic_action_budget = 64;
    control.cancellation.source = "diagnostic-test";
    const auto left_start = std::chrono::steady_clock::now();
    const auto left_reset = (*left)->reset(spec, control);
    const auto left_end = std::chrono::steady_clock::now();
    const auto right_start = std::chrono::steady_clock::now();
    const auto right_reset = (*right)->reset(spec, control);
    const auto right_end = std::chrono::steady_clock::now();
    compare_public_boundaries(left_reset, right_reset);
    require(event_count != 0, "diagnostic observer did not receive a public-safe event");
    const auto off_us = std::chrono::duration_cast<std::chrono::microseconds>(
        left_end - left_start).count();
    const auto on_us = std::chrono::duration_cast<std::chrono::microseconds>(
        right_end - right_start).count();
    const auto overhead = off_us == 0 ? 0.0
                                      : (static_cast<double>(on_us) - static_cast<double>(off_us)) *
                                            100.0 / static_cast<double>(off_us);
    std::cout << "DIAGNOSTICS_OFF_WALL_US=" << off_us << '\n'
              << "DIAGNOSTICS_ON_WALL_US=" << on_us << '\n'
              << "DIAGNOSTIC_OVERHEAD_PERCENT=" << overhead << '\n';
}

void test_invalid_job_index_is_rejected_without_execution() {
    const auto output = std::filesystem::temp_directory_path() /
                        "ocgforge-task7-diagnostic-invalid-index-test";
    if (std::filesystem::exists(output)) std::filesystem::remove_all(output);
    const auto result = ygo::phase6::run_task7_v2_forensic_diagnostic({
        "2ce354d26402fc9283137be3e74774810dc56986", output,
        ygo::phase6::Task7V2DiagnosticMode::SingleJob, 16, 30});
    require(!result.completed && result.error.find("outside the frozen schedule") !=
                std::string::npos,
            "invalid diagnostic job index was not rejected");
    require(!std::filesystem::exists(output),
            "invalid diagnostic job index created forensic output");
}

}  // namespace

int main() {
    try {
        test_empty_run_is_fully_classified();
        test_observer_is_semantically_non_interfering();
        test_invalid_job_index_is_rejected_without_execution();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task7_v2_forensic_diagnostics_test: " << error.what() << '\n';
        return 1;
    }
}
