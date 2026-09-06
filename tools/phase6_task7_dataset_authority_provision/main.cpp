#include "ygo/phase6/task7_dataset_authority_provisioning.hpp"

#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Arguments final {
    std::filesystem::path output;
    std::string source_commit;
    std::optional<std::size_t> diagnostic_job_index;
};

void usage() {
    std::cerr << "usage: phase6_task7_dataset_authority_provision"
                 " --output <directory> --source-commit <40-lowercase-hex>\n"
                 "   or: phase6_task7_dataset_authority_provision"
                 " --source-commit <40-lowercase-hex>"
                 " --diagnostic-job-index <0..15>\n";
}

std::size_t parse_job_index(const std::string_view value) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size() || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("diagnostic job index is invalid");
    }
    return static_cast<std::size_t>(parsed);
}

Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--output" && index + 1 < argc) {
            result.output = argv[++index];
        } else if (argument == "--source-commit" && index + 1 < argc) {
            result.source_commit = argv[++index];
        } else if (argument == "--diagnostic-job-index" && index + 1 < argc) {
            result.diagnostic_job_index = parse_job_index(argv[++index]);
        } else if (argument == "--help") {
            usage();
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown or incomplete argument");
        }
    }
    if (result.source_commit.empty()) {
        throw std::invalid_argument("source commit is required");
    }
    if (result.diagnostic_job_index.has_value()) {
        if (!result.output.empty()) {
            throw std::invalid_argument("diagnostic mode does not accept an output directory");
        }
    } else if (result.output.empty()) {
        throw std::invalid_argument("output and source commit are required");
    }
    return result;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        if (arguments.diagnostic_job_index.has_value()) {
            const auto diagnostic = ygo::phase6::task7::diagnose_task7_collection_job(
                arguments.source_commit, *arguments.diagnostic_job_index);
            std::cout << "TASK7_COLLECTION_DIAGNOSTIC=YES\n"
                      << "CLEAN_TERMINAL=" << (diagnostic.clean_terminal ? "YES" : "NO")
                      << "\n"
                      << diagnostic.diagnostic << '\n';
            return diagnostic.clean_terminal ? 0 : 2;
        }
        const auto result = ygo::phase6::task7::provision_task7_dataset_authority(
            arguments.source_commit);
        if (!result || !result.value.has_value()) {
            if (result.error.has_value()) {
                std::cerr << "ERROR="
                          << ygo::phase6::task7::task7_dataset_authority_error_code_name(
                                 result.error->code)
                          << "\nDETAIL=" << result.error->diagnostic << '\n';
            } else {
                std::cerr << "ERROR=internal_failure\n";
            }
            return 2;
        }
        std::string error;
        if (!ygo::phase6::task7::write_task7_dataset_authority(
                *result.value, arguments.output, &error)) {
            std::cerr << "ERROR=output_failure\nDETAIL=" << error << '\n';
            return 3;
        }
        std::cout << "TASK7_DATASET_AUTHORITY_READY=YES\n"
                  << "COLLECTION_JOB_COUNT=" << result.value->jobs.size() << '\n'
                  << "DATASET_SEMANTIC_ID="
                  << result.value->dataset_manifest.dataset_semantic_id << '\n'
                  << "SPLIT_IDENTITY=" << result.value->split.split_identity << '\n'
                  << "CARD_VOCABULARY_IDENTITY="
                  << result.value->card_vocabulary.identity() << '\n';
        return 0;
    } catch (const std::exception& error) {
        usage();
        std::cerr << "ERROR=invalid_arguments\nDETAIL=" << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "ERROR=internal_failure\n";
        return 1;
    }
}
