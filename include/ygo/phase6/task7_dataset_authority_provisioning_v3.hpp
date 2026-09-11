#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/diagnostics/task7_observer.hpp"
#include "ygo/model/card_vocabulary.hpp"
#include "ygo/phase6/supervision_dataset_v2.hpp"
#include "ygo/policy/teacher_runner_v4_trajectory.hpp"
#include "ygo/trajectory/admission_v3.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/dataset_manifest_v3.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"
#include "ygo/trajectory/shard_v3.hpp"

namespace ygo::phase6 {

inline constexpr std::string_view kTask7V3CollectionProfile =
    "ocgforge.phase6.task7.dataset_collection.reference.v3";
inline constexpr std::string_view kTask7V3JobSchemaId =
    "ocgforge.phase6.task7.dataset_collection_job.v3";
inline constexpr std::string_view kTask7V3ScheduleSchemaId =
    "ocgforge.phase6.task7.dataset_collection_schedule.v3";
inline constexpr std::string_view kTask7V3ReferenceSchemaId =
    "ocgforge.phase6.task7.dataset_collection.reference.v3";
inline constexpr std::string_view kTask7V3JobIdentityPrefix =
    "phase6_task7_dataset_collection_job.v3.";
inline constexpr std::string_view kTask7V3ScheduleIdentityPrefix =
    "phase6_task7_dataset_collection_schedule.v3.";
inline constexpr std::string_view kTask7V3AuthoritySchemaId =
    "ocgforge.phase6.task7.dataset_authority.v3";
inline constexpr std::string_view kTask7V3CollectorSemanticVersion =
    "ocgforge.phase6.task7.v3.provisioner.1";

struct Task7CollectionJobV3 final {
    std::string collection_profile = std::string(kTask7V3ReferenceSchemaId);
    std::string environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV4ContractId);
    std::string matchup_id = "ocgforge.matchup.swordsoul_salamangreat.v1";
    std::string rules_bundle_id;
    std::string format_id;
    std::string duel_mode;
    std::uint64_t duel_flags = 0;
    std::uint64_t root_seed = 0;
    environment::SeatAssignment seat_assignment = environment::SeatAssignment::Normal;
    std::uint8_t starting_player = 0;
    std::array<trajectory::DeckRole, 2> deck_roles = {
        trajectory::DeckRole::FirstLockedDeck, trajectory::DeckRole::SecondLockedDeck};
    std::array<std::string, 2> deck_ids;
    std::array<std::string, 2> deck_sha256;
    std::array<std::string, 2> teacher_policy_artifact_ids;
    std::array<std::string, 2> teacher_policy_binding_ids;
    std::string teacher_producer_identity = "ocgforge.policy.teacher_core.v3";
    std::string teacher_action_adapter_identity =
        "ocgforge.policy.public_action_key.v3";
    std::string teacher_sampling_identity =
        "ocgforge.policy.deterministic_lexicographic_argmax.v1";
    std::string policy_rng_identity = "ocgforge.no_policy_rng.v1";
    std::uint64_t engine_process_budget = 20000;
    std::uint64_t semantic_action_budget = 20000;
    std::string cancellation_reason = "ADMINISTRATIVE_CANCEL";
    std::string cancellation_source =
        "phase6-task7-v2-dataset-authority-provisioning";
    std::string collector_semantic_version =
        std::string(kTask7V3CollectorSemanticVersion);
    std::string collector_source_commit;
};

struct Task7CollectionScheduleV3 final {
    std::string collection_profile = std::string(kTask7V3ReferenceSchemaId);
    std::string environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV4ContractId);
    std::vector<std::uint64_t> seeds;
    std::vector<environment::SeatAssignment> placements;
    std::vector<std::uint8_t> starting_players;
    std::vector<Task7CollectionJobV3> jobs;
};

struct Task7V3JobOutcome final {
    Task7CollectionJobV3 job;
    policy::TeacherRunnerV4TrajectoryRunResult run;
    std::optional<trajectory::RestrictedReplayEvidenceV3> replay_evidence;
    std::optional<trajectory::CandidateTrajectoryShardV3> candidate_shard;
    std::optional<trajectory::RestrictedCollectionEvidenceBundleV3>
        restricted_collection_evidence;
    std::optional<trajectory::admission_v3::AdmissionVerification>
        admission_verification;
    std::optional<trajectory::VerifiedAdmissionReceiptV3> admission_receipt;
    std::optional<trajectory::dataset_v3::DatasetManifestV3> dataset_manifest;
};

struct Task7V3DatasetAuthority final {
    Task7CollectionScheduleV3 schedule;
    std::vector<Task7V3JobOutcome> outcomes;
    trajectory::dataset_v3::DatasetManifestV3 dataset_manifest;
    TrainingDatasetSplitV1 split;
    model::CardVocabularyV1 vocabulary;
};

struct Task7V3ProvisioningResult final {
    std::optional<Task7V3DatasetAuthority> value;
    std::vector<Task7V3JobOutcome> outcomes;
    std::optional<std::string> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

using Task7V3JobExecutor =
    std::function<policy::TeacherRunnerV4TrajectoryRunResult(
        const Task7CollectionJobV3&)>;

struct Task7V3EligibilityInspection final {
    bool eligible = false;
    bool run_error_present = false;
    bool envelope_present = false;
    bool quarantined = false;
    bool replay_evidence_present = false;
    bool candidate_shard_present = false;
    bool restricted_collection_evidence_present = false;
    bool admission_verification_present = false;
    bool admission_receipt_present = false;
    bool dataset_manifest_present = false;
    bool terminal_closure = false;
    bool clean_collection_disposition = false;
    std::size_t candidate_shard_entry_count = 0;
    std::size_t admission_receipt_entry_count = 0;
    std::size_t dataset_manifest_member_count = 0;
    std::vector<std::string> failed_conditions;
    std::string first_failed_condition;
    std::string diagnostic;
};

// The executor overload is a bounded test/controlled-integration seam. The
// production provisioning entry point below always uses
// run_task7_collection_job_v3(), which calls TeacherRunnerV4TrajectoryRunner.

Task7CollectionScheduleV3 make_task7_collection_schedule_v3(
    std::string collector_source_commit);

std::vector<std::uint8_t> canonical_task7_collection_job_bytes_v3(
    const Task7CollectionJobV3& job);
trajectory::DecodeResult<Task7CollectionJobV3> decode_task7_collection_job_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;
std::string task7_collection_job_identity_v3(const Task7CollectionJobV3& job);

std::vector<std::uint8_t> canonical_task7_collection_schedule_bytes_v3(
    const Task7CollectionScheduleV3& schedule);
trajectory::DecodeResult<Task7CollectionScheduleV3>
decode_task7_collection_schedule_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;
std::string task7_collection_schedule_identity_v3(
    const Task7CollectionScheduleV3& schedule);

policy::TeacherRunnerV4TrajectoryRunResult run_task7_collection_job_v3(
    const Task7CollectionJobV3& job) noexcept;

policy::TeacherRunnerV4TrajectoryRunResult run_task7_collection_job_v3(
    const Task7CollectionJobV3& job,
    const diagnostics::Task7DiagnosticObserver& diagnostic_observer) noexcept;

// Diagnostic-only bounded execution. The decision limit is an execution
// control outside Task7CollectionJobV3 and is never part of job/schedule
// canonical bytes or identities. The normal collection overloads above are
// unchanged and always run without a diagnostic decision bound.
policy::TeacherRunnerV4TrajectoryRunResult
run_task7_collection_job_v3_bounded_for_diagnostics(
    const Task7CollectionJobV3& job,
    std::uint64_t decision_limit,
    const diagnostics::Task7DiagnosticObserver& diagnostic_observer = {}) noexcept;

Task7V3EligibilityInspection inspect_task7_v3_job_run(
    const Task7CollectionJobV3& job,
    const Task7V3JobOutcome& outcome) noexcept;

trajectory::PolicyProvenanceEnvelope make_task7_v3_policy_provenance(
    const Task7CollectionJobV3& job);

bool validate_task7_v3_job_episode_binding(
    const Task7CollectionJobV3& job,
    const trajectory::EpisodeEnvelopeV3& envelope,
    std::string* error = nullptr) noexcept;

Task7V3ProvisioningResult provision_task7_dataset_authority_v3(
    const Task7CollectionScheduleV3& schedule,
    const Task7V3JobExecutor& executor);

Task7V3ProvisioningResult provision_task7_dataset_authority_v3(
    const Task7CollectionScheduleV3& schedule);

Phase6SplitResult derive_training_dataset_split_v1_from_v3(
    std::string source_dataset_identity,
    const std::vector<std::string>& episode_semantic_ids) noexcept;

model::CardVocabularyResult derive_card_vocabulary_v1_from_public_observations_v3(
    const std::vector<environment::PublicEnvironmentObservation>& observations) noexcept;

bool validate_task7_v3_authority(
    const Task7V3DatasetAuthority& authority,
    std::string* error = nullptr) noexcept;

std::vector<std::uint8_t> canonical_task7_v3_authority_bytes(
    const Task7V3DatasetAuthority& authority);
std::string task7_v3_authority_identity(
    const Task7V3DatasetAuthority& authority);

}  // namespace ygo::phase6
