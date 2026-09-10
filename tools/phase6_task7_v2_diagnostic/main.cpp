#include "ygo/phase6/task7_v2_forensic_diagnostics.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Arguments final {
    std::string source_commit;
    std::string output;
    ygo::phase6::Task7V2DiagnosticMode mode =
        ygo::phase6::Task7V2DiagnosticMode::UntilFirstIneligible;
    std::size_t job_index = 0;
};

void usage() {
    std::cerr << "usage: phase6_task7_v2_diagnostic --source-commit <sha> "
                 "--output <directory> (--until-first-ineligible | --job-index <0..15>)\n";
}

std::size_t parse_index(const std::string_view value) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size() || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("job index is invalid");
    }
    return static_cast<std::size_t>(parsed);
}

Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    bool mode_selected = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--source-commit" && index + 1 < argc) {
            result.source_commit = argv[++index];
        } else if (argument == "--output" && index + 1 < argc) {
            result.output = argv[++index];
        } else if (argument == "--job-index" && index + 1 < argc) {
            if (mode_selected) throw std::invalid_argument("diagnostic mode was selected twice");
            result.mode = ygo::phase6::Task7V2DiagnosticMode::SingleJob;
            result.job_index = parse_index(argv[++index]);
            mode_selected = true;
        } else if (argument == "--until-first-ineligible") {
            if (mode_selected) throw std::invalid_argument("diagnostic mode was selected twice");
            result.mode = ygo::phase6::Task7V2DiagnosticMode::UntilFirstIneligible;
            mode_selected = true;
        } else if (argument == "--help") {
            usage();
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown or incomplete argument");
        }
    }
    if (result.source_commit.empty() || result.output.empty() || !mode_selected) {
        throw std::invalid_argument("source commit, output, and exactly one diagnostic mode are required");
    }
    return result;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        const ygo::phase6::Task7V2DiagnosticOptions options{
            arguments.source_commit,
            arguments.output,
            arguments.mode,
            arguments.job_index,
            30};
        const auto result = ygo::phase6::run_task7_v2_forensic_diagnostic(options);
        if (!result.completed) {
            std::cerr << "Task7 V2 forensic diagnostic failed: " << result.error << '\n';
            return 1;
        }
        std::cout << "FORENSIC_DIAGNOSTIC_COMPLETE=YES\n"
                  << "JOBS_EXECUTED=" << result.jobs_executed << '\n'
                  << "ELIGIBLE_JOBS=" << result.eligible_jobs << '\n'
                  << "INELIGIBLE_JOBS=" << result.ineligible_jobs << '\n';
        if (result.first_failed_job_index.has_value()) {
            std::cout << "FIRST_FAILED_JOB_INDEX=" << *result.first_failed_job_index << '\n'
                      << "FIRST_FAILED_JOB_ID=" << result.first_failed_job_id << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        usage();
        std::cerr << "Task7 V2 forensic diagnostic: " << error.what() << '\n';
        return 2;
    }
}
