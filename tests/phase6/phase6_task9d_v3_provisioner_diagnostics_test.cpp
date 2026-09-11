#include "tools/phase6_task7_v3_provisioner/diagnostic_writer.hpp"

#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ygo::phase6;
using namespace ygo::phase6::tooling;

constexpr std::string_view kSourceCommit =
    "a2ae754d56b0668f2ad7ba5ee70aa2c65762e093";
constexpr std::uint64_t kDecisionLimit = 236;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("diagnostic test output could not be opened");
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void test_jsonl_content_and_bounded_job0() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kSourceCommit));
    require(schedule.jobs.size() == 16, "Task7 V3 schedule is not canonical");
    const auto& job = schedule.jobs.front();
    require(job.root_seed == 4 &&
                job.seat_assignment == ygo::environment::SeatAssignment::Normal &&
                job.starting_player == 0,
            "Task9D Job0 mapping changed");

    const auto path = std::filesystem::temp_directory_path() /
                      "ocgforge_task9d_v3_diagnostics_test.jsonl";
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);

    std::vector<ygo::diagnostics::Task7DiagnosticEvent> events;
    {
        Task7V3ProvisionerDiagnosticWriter writer(path);
        require(!writer.failed(), "diagnostic writer failed to open test output");
        require(writer.write_run_start(kSourceCommit, schedule.jobs.size()),
                "RUN_START could not be written");
        const auto job_id = task7_collection_job_identity_v3(job);
        require(writer.write_job_start(0, job), "JOB_START could not be written");
        const auto writer_observer = writer.observer(0, job_id);
        const ygo::diagnostics::Task7DiagnosticObserver observer =
            [&events, writer_observer](const auto& event) {
                events.push_back(event);
                writer_observer(event);
            };
        const auto result = run_task7_collection_job_v3_bounded_with_replay_evidence(
            job, kDecisionLimit, observer);
        require(!result.run.error.has_value() && result.run.envelope.has_value() &&
                    result.restricted_replay_evidence.has_value(),
                "bounded Job0 diagnostic execution failed");
        require(writer.write_job_end(0, job, result.run),
                "JOB_END could not be written");
        require(writer.write_run_end(1, false, false),
                "RUN_END could not be written");
        require(!writer.failed(), "diagnostic writer failed during bounded run");
    }

    const auto content = read_all(path);
    std::filesystem::remove(path, cleanup_error);
    require(content.find("\"record_type\":\"RUN_START\"") != std::string::npos,
            "JSONL lacks RUN_START");
    require(content.find("\"record_type\":\"JOB_START\"") != std::string::npos,
            "JSONL lacks JOB_START");
    require(content.find("\"record_type\":\"EVENT\"") != std::string::npos,
            "JSONL lacks EVENT");
    require(content.find("\"record_type\":\"JOB_END\"") != std::string::npos,
            "JSONL lacks JOB_END");
    require(content.find("\"record_type\":\"RUN_END\"") != std::string::npos,
            "JSONL lacks RUN_END");
    require(content.find("\"job_count\":16") != std::string::npos,
            "JSONL lacks canonical job count");

    std::size_t line_count = 0;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        require(!line.empty() && line.front() == '{' && line.back() == '}',
                "JSONL contains a malformed line boundary");
        ++line_count;
    }
    require(line_count > 5, "JSONL lacks event records");

    const auto event_at = [&events](const std::uint64_t decision_index) {
        const auto found = std::find_if(
            events.begin(), events.end(), [decision_index](const auto& event) {
                return event.phase == "TEACHER" &&
                       event.decision_index == decision_index;
            });
        return found == events.end() ? nullptr : &*found;
    };
    const auto* hiita = event_at(234);
    const auto* material = event_at(235);
    require(hiita != nullptr && material != nullptr,
            "JSONL source events lack Decisions 234/235");
    for (const auto* event : {hiita, material}) {
        require(!event->public_observation_digest.empty() &&
                    !event->public_current_state_fingerprint.empty() &&
                    !event->public_candidate_domain_digest.empty() &&
                    !event->public_semantic_decision_id.empty() &&
                    !event->selected_public_action_key.empty(),
                "bounded decision event lacks public diagnostic fields");
    }
    require(material->request_kind ==
                ygo::environment::environment_decision_kind_name(
                    ygo::environment::EnvironmentDecisionKind::UnselectCard) &&
                material->candidate_count == 3,
            "Decision 235 diagnostic shape changed");
    require(content.find("\"decision_index\":234") != std::string::npos &&
                content.find("\"decision_index\":235") != std::string::npos,
            "JSONL lacks decision indices 234/235");
    require(content.find("\"public_semantic_decision_id\":\"" +
                         hiita->public_semantic_decision_id + "\"") !=
                std::string::npos &&
                content.find("\"public_semantic_decision_id\":\"" +
                         material->public_semantic_decision_id + "\"") !=
                    std::string::npos,
            "JSONL lacks public semantic decision identities");
    require(content.find("\"selected_public_action_key\":\"" +
                         material->selected_public_action_key + "\"") !=
                std::string::npos,
            "JSONL lacks selected public action key");
}

}  // namespace

int main() {
    try {
        test_jsonl_content_and_bounded_job0();
        std::cout << "phase6_task9d_v3_provisioner_diagnostics_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task9d_v3_provisioner_diagnostics_test: "
                  << error.what() << '\n';
        return 1;
    }
}
