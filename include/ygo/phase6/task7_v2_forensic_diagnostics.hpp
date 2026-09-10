#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "ygo/phase6/task7_dataset_authority_provisioning_v2.hpp"

namespace ygo::phase6 {

enum class Task7V2DiagnosticMode : std::uint8_t {
    SingleJob,
    UntilFirstIneligible,
};

struct Task7V2DiagnosticOptions final {
    std::string source_commit;
    std::filesystem::path output_directory;
    Task7V2DiagnosticMode mode = Task7V2DiagnosticMode::UntilFirstIneligible;
    std::size_t job_index = 0;
    std::uint64_t heartbeat_seconds = 30;
};

struct Task7V2DiagnosticRunResult final {
    bool completed = false;
    std::size_t jobs_executed = 0;
    std::size_t eligible_jobs = 0;
    std::size_t ineligible_jobs = 0;
    std::optional<std::size_t> first_failed_job_index;
    std::string first_failed_job_id;
    std::string error;
};

Task7V2DiagnosticRunResult run_task7_v2_forensic_diagnostic(
    const Task7V2DiagnosticOptions& options) noexcept;

}  // namespace ygo::phase6
