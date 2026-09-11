#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"
#include "ygo/trajectory/replay_v3.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"
#include "ygo/trace/sha256.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ygo::phase6;
using namespace ygo::trajectory;

constexpr std::string_view kSourceCommit =
    "cffa1c797ab72f8f64e638117773d5a83ef72cfc";
constexpr std::uint64_t kDecisionLimit = 236;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_bounded_interrupted_replay_evidence() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kSourceCommit));
    require(schedule.jobs.size() == 16, "Task7 V3 schedule count changed");
    const auto& job = schedule.jobs.front();
    require(job.root_seed == 4 && job.seat_assignment == ygo::environment::SeatAssignment::Normal &&
                job.starting_player == 0,
            "Task9B Job0 mapping changed");

    const auto job_bytes = canonical_task7_collection_job_bytes_v3(job);
    const auto job_id = task7_collection_job_identity_v3(job);
    const auto schedule_bytes = canonical_task7_collection_schedule_bytes_v3(schedule);
    const auto schedule_id = task7_collection_schedule_identity_v3(schedule);

    const auto result = run_task7_collection_job_v3_bounded_with_replay_evidence(
        job, kDecisionLimit);
    require(!result.run.error.has_value() && result.run.envelope.has_value() &&
                result.restricted_replay_evidence.has_value(),
            "bounded diagnostic did not return replay evidence: " + result.run.diagnostic);
    const auto& envelope = *result.run.envelope;
    const auto& evidence = *result.restricted_replay_evidence;
    const auto* interrupted = std::get_if<InterruptedClosureV3>(&envelope.closure);
    require(interrupted != nullptr && interrupted->record_count == kDecisionLimit,
            "bounded diagnostic did not produce the expected interrupted closure");
    require(evidence.episode_semantic_id == envelope.manifest.episode_semantic_id &&
                evidence.interruption_reason ==
                    ygo::environment::InterruptionReason::AdministrativeCancel &&
                evidence.engine_process_budget == 20000 &&
                evidence.semantic_action_budget == 20000 &&
                evidence.observed_semantic_action_count == kDecisionLimit,
            "restricted replay evidence is not bound to the authoritative interruption");
    (void)canonical_restricted_replay_evidence_bytes_v3(evidence);

    replay_v3::ReplayOptions options;
    options.cancellation_source = job.cancellation_source;
    const auto replay = replay_v3::replay_episode_v3(envelope, evidence, options);
    require(replay.accepted,
            "bounded interrupted envelope failed replay_v3: " + replay.error);

    require(canonical_task7_collection_job_bytes_v3(job) == job_bytes &&
                task7_collection_job_identity_v3(job) == job_id &&
                canonical_task7_collection_schedule_bytes_v3(schedule) == schedule_bytes &&
                task7_collection_schedule_identity_v3(schedule) == schedule_id,
            "Task9B diagnostic execution changed canonical Task7 identities");

    std::cout << "RESTRICTED_REPLAY_EVIDENCE_PRESENT=YES\n"
              << "EVIDENCE_EPISODE_ID_MATCH=YES\n"
              << "EVIDENCE_REASON=AdministrativeCancel\n"
              << "EVIDENCE_SEMANTIC_ACTION_COUNT="
              << evidence.observed_semantic_action_count << '\n'
              << "REPLAY_PREFIX_VALIDATION=PASS\n"
              << "REPLAY_FINAL_ENGINE_STEP_INDEX=" << replay.final_engine_step_index << '\n'
              << "RESTRICTED_REPLAY_EVIDENCE_SHA256="
              << restricted_replay_evidence_artifact_sha256_v3(evidence) << '\n'
              << "REPLAY_RESULT=PASS\n";
}

}  // namespace

int main() {
    try {
        test_bounded_interrupted_replay_evidence();
        std::cout << "phase6_task9b_bounded_interrupted_replay_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task9b_bounded_interrupted_replay_test: "
                  << error.what() << '\n';
        return 1;
    }
}
