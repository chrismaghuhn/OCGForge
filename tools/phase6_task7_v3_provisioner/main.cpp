#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"
#include "diagnostic_writer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

void usage() {
    std::cerr << "usage: phase6_task7_v3_provisioner --source-commit <sha> "
                 "--output <authority-file> [--diagnostics <jsonl-file>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string source_commit;
    std::string output_path;
    std::string diagnostics_path;
    bool source_commit_seen = false;
    bool output_path_seen = false;
    bool diagnostics_path_seen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--source-commit" && index + 1 < argc &&
            !source_commit_seen) {
            source_commit = argv[++index];
            source_commit_seen = true;
        } else if (argument == "--output" && index + 1 < argc &&
                   !output_path_seen) {
            output_path = argv[++index];
            output_path_seen = true;
        } else if (argument == "--diagnostics" && index + 1 < argc &&
                   !diagnostics_path_seen) {
            diagnostics_path = argv[++index];
            diagnostics_path_seen = true;
        } else {
            usage();
            return 2;
        }
    }
    if (source_commit.empty() || output_path.empty() ||
        (diagnostics_path_seen && diagnostics_path.empty())) {
        usage();
        return 2;
    }
    try {
        const auto schedule = ygo::phase6::make_task7_collection_schedule_v3(source_commit);
        std::unique_ptr<ygo::phase6::tooling::Task7V3ProvisionerDiagnosticWriter>
            diagnostics;
        if (diagnostics_path_seen) {
            diagnostics = std::make_unique<
                ygo::phase6::tooling::Task7V3ProvisionerDiagnosticWriter>(
                std::filesystem::path(diagnostics_path));
            if (!diagnostics->write_run_start(source_commit, schedule.jobs.size())) {
                std::cerr << "Task7 V3 diagnostics output failed: "
                          << diagnostics->error() << '\n';
                return 1;
            }
        }

        ygo::phase6::Task7V3ProvisioningResult result;
        if (diagnostics) {
            std::size_t next_job_index = 0;
            const ygo::phase6::Task7V3JobExecutor executor =
                [&diagnostics, &next_job_index](
                    const ygo::phase6::Task7CollectionJobV3& job) {
                    const auto job_index = next_job_index++;
                    const auto job_identity =
                        ygo::phase6::task7_collection_job_identity_v3(job);
                    diagnostics->write_job_start(job_index, job);
                    const auto run = ygo::phase6::run_task7_collection_job_v3(
                        job, diagnostics->observer(job_index, job_identity));
                    diagnostics->write_job_end(job_index, job, run);
                    return run;
                };
            result = ygo::phase6::provision_task7_dataset_authority_v3(
                schedule, executor);
        } else {
            result = ygo::phase6::provision_task7_dataset_authority_v3(schedule);
        }
        if (!result) {
            if (diagnostics) {
                diagnostics->write_run_end(schedule.jobs.size(), false, false);
            }
            std::cerr << "Task7 V3 provisioning failed: "
                      << (result.error.has_value() ? *result.error : "unknown failure") << '\n';
            return 1;
        }
        if (diagnostics && diagnostics->failed()) {
            diagnostics->write_run_end(schedule.jobs.size(), false, false);
            std::cerr << "Task7 V3 diagnostics output failed: "
                      << diagnostics->error() << '\n';
            return 1;
        }
        const auto bytes =
            ygo::phase6::canonical_task7_v3_authority_bytes(*result.value);
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (diagnostics) {
                diagnostics->write_run_end(schedule.jobs.size(), false, false);
            }
            std::cerr << "Task7 V3 authority output could not be opened\n";
            return 1;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            if (diagnostics) {
                diagnostics->write_run_end(schedule.jobs.size(), false, false);
            }
            std::cerr << "Task7 V3 authority output could not be written\n";
            return 1;
        }
        if (diagnostics &&
            !diagnostics->write_run_end(schedule.jobs.size(), true, true)) {
            std::cerr << "Task7 V3 diagnostics output failed: "
                      << diagnostics->error() << '\n';
            return 1;
        }
        std::cout << "TASK7_V3_AUTHORITY_ID="
                  << ygo::phase6::task7_v3_authority_identity(*result.value) << '\n'
                  << "TASK7_V3_AUTHORITY_BYTES=" << bytes.size() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Task7 V3 provisioner: " << error.what() << '\n';
        return 1;
    }
}
