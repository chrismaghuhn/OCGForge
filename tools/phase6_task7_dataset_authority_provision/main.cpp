#include "ygo/phase6/task7_dataset_authority_provisioning.hpp"

#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Arguments final {
    std::filesystem::path output;
    std::string source_commit;
};

void usage() {
    std::cerr << "usage: phase6_task7_dataset_authority_provision"
                 " --output <directory> --source-commit <40-lowercase-hex>\n";
}

Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--output" && index + 1 < argc) {
            result.output = argv[++index];
        } else if (argument == "--source-commit" && index + 1 < argc) {
            result.source_commit = argv[++index];
        } else if (argument == "--help") {
            usage();
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown or incomplete argument");
        }
    }
    if (result.output.empty() || result.source_commit.empty()) {
        throw std::invalid_argument("output and source commit are required");
    }
    return result;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
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
