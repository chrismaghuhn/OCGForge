#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"

#include <fstream>
#include <iostream>
#include <string>

namespace {

void usage() {
    std::cerr << "usage: phase6_task7_v3_provisioner --source-commit <sha> "
                 "--output <authority-file>\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string source_commit;
    std::string output_path;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--source-commit" && index + 1 < argc) {
            source_commit = argv[++index];
        } else if (argument == "--output" && index + 1 < argc) {
            output_path = argv[++index];
        } else {
            usage();
            return 2;
        }
    }
    if (source_commit.empty() || output_path.empty()) {
        usage();
        return 2;
    }
    try {
        const auto schedule = ygo::phase6::make_task7_collection_schedule_v3(source_commit);
        const auto result = ygo::phase6::provision_task7_dataset_authority_v3(schedule);
        if (!result) {
            std::cerr << "Task7 V3 provisioning failed: "
                      << (result.error.has_value() ? *result.error : "unknown failure") << '\n';
            return 1;
        }
        const auto bytes =
            ygo::phase6::canonical_task7_v3_authority_bytes(*result.value);
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "Task7 V3 authority output could not be opened\n";
            return 1;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            std::cerr << "Task7 V3 authority output could not be written\n";
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
