#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"
#include "ygo/trajectory/replay_v3.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"
#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ygo::phase6;
using namespace ygo::trajectory;

constexpr std::string_view kSourceCommit =
    "a2ae754d56b0668f2ad7ba5ee70aa2c65762e093";
constexpr std::uint64_t kDecisionLimit = 236;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct CapturedRun final {
    Task7V3BoundedDiagnosticResult result;
    std::vector<ygo::diagnostics::Task7DiagnosticEvent> events;
};

CapturedRun execute(const Task7CollectionJobV3& job, const bool observe) {
    CapturedRun captured;
    const ygo::diagnostics::Task7DiagnosticObserver observer =
        observe ? [&captured](const auto& event) { captured.events.push_back(event); } :
                  ygo::diagnostics::Task7DiagnosticObserver{};
    captured.result = run_task7_collection_job_v3_bounded_with_replay_evidence(
        job, kDecisionLimit, observer);
    return captured;
}

const ygo::diagnostics::Task7DiagnosticEvent* find_teacher_event(
    const std::vector<ygo::diagnostics::Task7DiagnosticEvent>& events,
    const std::uint64_t decision_index) {
    const auto found = std::find_if(
        events.begin(), events.end(), [decision_index](const auto& event) {
            return event.phase == "TEACHER" &&
                   event.decision_index == decision_index;
        });
    return found == events.end() ? nullptr : &*found;
}

void require_public_event(const ygo::diagnostics::Task7DiagnosticEvent& event,
                          const std::uint64_t decision_index,
                          const std::string_view request_kind,
                          const std::uint64_t candidate_count) {
    require(event.decision_index == decision_index,
            "V4 diagnostic event has the wrong decision index");
    require(event.request_kind == request_kind,
            "V4 diagnostic event has the wrong request kind");
    require(event.candidate_count == candidate_count,
            "V4 diagnostic event has the wrong candidate count");
    require(!event.public_observation_digest.empty(),
            "V4 diagnostic event lacks public observation digest");
    require(!event.public_current_state_fingerprint.empty(),
            "V4 diagnostic event lacks current-state fingerprint");
    require(!event.public_candidate_domain_digest.empty(),
            "V4 diagnostic event lacks candidate-domain digest");
    require(!event.public_semantic_decision_id.empty(),
            "V4 diagnostic event lacks semantic decision identity");
    require(!event.selected_public_action_key.empty(),
            "V4 diagnostic event lacks selected public action key");
}

void require_equivalent_runs(const Task7CollectionJobV3& job,
                             const CapturedRun& left, const CapturedRun& right) {
    require(left.result.run.envelope.has_value() && right.result.run.envelope.has_value(),
            "bounded V4 diagnostic run lacks an envelope");
    require(left.result.restricted_replay_evidence.has_value() &&
                right.result.restricted_replay_evidence.has_value(),
            "bounded V4 diagnostic run lacks restricted replay evidence");
    require(canonical_episode_envelope_bytes_v3(*left.result.run.envelope) ==
                canonical_episode_envelope_bytes_v3(*right.result.run.envelope),
            "diagnostics changed V3 episode envelope bytes");
    require(canonical_restricted_replay_evidence_bytes_v3(
                *left.result.restricted_replay_evidence) ==
                canonical_restricted_replay_evidence_bytes_v3(
                    *right.result.restricted_replay_evidence),
            "diagnostics changed restricted replay evidence bytes");

    replay_v3::ReplayOptions options;
    options.cancellation_source = job.cancellation_source;
    const auto left_replay = replay_v3::replay_episode_v3(
        *left.result.run.envelope, *left.result.restricted_replay_evidence, options);
    const auto right_replay = replay_v3::replay_episode_v3(
        *right.result.run.envelope, *right.result.restricted_replay_evidence, options);
    require(left_replay.accepted && right_replay.accepted,
            "bounded V4 diagnostic replay was not accepted");
    require(left_replay.final_engine_step_index == right_replay.final_engine_step_index,
            "diagnostic replay final engine step differs");
}

void test_v4_diagnostic_projection_and_parity() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kSourceCommit));
    require(schedule.jobs.size() == 16, "Task7 V3 schedule is not canonical");
    const auto& job = schedule.jobs.front();
    require(job.root_seed == 4 &&
                job.seat_assignment == ygo::environment::SeatAssignment::Normal &&
                job.starting_player == 0,
            "Task9E Job0 mapping changed");

    const auto off = execute(job, false);
    const auto on = execute(job, true);
    const auto fresh_on = execute(job, true);
    require_equivalent_runs(job, off, on);
    require_equivalent_runs(job, off, fresh_on);

    const auto* hiita = find_teacher_event(on.events, 234);
    const auto* material = find_teacher_event(on.events, 235);
    require(hiita != nullptr, "V4 diagnostics lack accepted Decision 234 event");
    require(material != nullptr, "V4 diagnostics lack accepted Decision 235 event");
    require_public_event(
        *hiita, 234,
        ygo::environment::environment_decision_kind_name(
            ygo::environment::EnvironmentDecisionKind::IdleCommand),
        hiita->candidate_count);
    require_public_event(
        *material, 235,
        ygo::environment::environment_decision_kind_name(
            ygo::environment::EnvironmentDecisionKind::UnselectCard),
        3);

    std::cout << "EPISODE_ENVELOPE_BYTES_A_B_EQUAL=YES\n"
              << "EPISODE_ENVELOPE_BYTES_A_C_EQUAL=YES\n"
              << "RESTRICTED_REPLAY_EVIDENCE_A_B_EQUAL=YES\n"
              << "RESTRICTED_REPLAY_EVIDENCE_A_C_EQUAL=YES\n"
              << "REPLAY_RESULT_A_B_C=PASS\n"
              << "DECISION_234_EVENT_PRESENT=YES\n"
              << "DECISION_235_EVENT_PRESENT=YES\n"
              << "DECISION_234_PUBLIC_SEMANTIC_DECISION_ID="
              << hiita->public_semantic_decision_id << '\n'
              << "DECISION_235_PUBLIC_SEMANTIC_DECISION_ID="
              << material->public_semantic_decision_id << '\n'
              << "DECISION_235_SELECTED_PUBLIC_ACTION_KEY="
              << material->selected_public_action_key << '\n'
              << "BOUNDED_ENVELOPE_SHA256="
              << ygo::trace::sha256_bytes(
                     canonical_episode_envelope_bytes_v3(*on.result.run.envelope))
              << '\n';
}

}  // namespace

int main() {
    try {
        test_v4_diagnostic_projection_and_parity();
        std::cout << "phase6_task9e_v4_trajectory_diagnostic_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task9e_v4_trajectory_diagnostic_test: "
                  << error.what() << '\n';
        return 1;
    }
}
