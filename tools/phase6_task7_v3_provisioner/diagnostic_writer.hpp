#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "ygo/diagnostics/task7_observer.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"

namespace ygo::phase6::tooling {

inline constexpr std::string_view kTask7V3ProvisionerDiagnosticsSchema =
    "ocgforge.phase6.task7.v3.provisioner_diagnostics.v1";

class Task7V3ProvisionerDiagnosticWriter final {
public:
    explicit Task7V3ProvisionerDiagnosticWriter(std::filesystem::path path);

    Task7V3ProvisionerDiagnosticWriter(
        const Task7V3ProvisionerDiagnosticWriter&) = delete;
    Task7V3ProvisionerDiagnosticWriter& operator=(
        const Task7V3ProvisionerDiagnosticWriter&) = delete;

    bool failed() const noexcept;
    const std::string& error() const noexcept;

    bool write_run_start(std::string_view source_commit,
                         std::size_t job_count) noexcept;
    bool write_job_start(std::size_t job_index,
                         const Task7CollectionJobV3& job) noexcept;
    bool write_event(std::size_t job_index, std::string_view job_identity,
                     const diagnostics::Task7DiagnosticEvent& event) noexcept;
    bool write_job_end(std::size_t job_index, const Task7CollectionJobV3& job,
                       const policy::TeacherRunnerV4TrajectoryRunResult& run) noexcept;
    bool write_run_end(std::size_t job_count, bool provisioning_success,
                       bool authority_generated) noexcept;

    diagnostics::Task7DiagnosticObserver observer(
        std::size_t job_index, std::string job_identity);

private:
    bool write_line(const std::string& line) noexcept;

    std::ofstream output_;
    bool failed_ = false;
    std::string error_;
};

}  // namespace ygo::phase6::tooling
