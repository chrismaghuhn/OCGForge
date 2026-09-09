#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/card_vocabulary.hpp"
#include "ygo/phase6/supervision_dataset.hpp"
#include "ygo/policy/teacher_runner_v3_trajectory.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::phase6 {

inline constexpr std::string_view kTask7V2CollectionProfile =
    "ocgforge.phase6.task7.dataset_collection.reference.v2";
inline constexpr std::string_view kTask7V2JobSchemaId =
    "ocgforge.phase6.task7.dataset_collection_job.v2";
inline constexpr std::string_view kTask7V2ScheduleSchemaId =
    "ocgforge.phase6.task7.dataset_collection_schedule.v2";
inline constexpr std::string_view kTask7V2ReferenceSchemaId =
    "ocgforge.phase6.task7.dataset_collection.reference.v2";
inline constexpr std::string_view kTask7V2JobIdentityPrefix =
    "phase6_task7_dataset_collection_job.v2.";
inline constexpr std::string_view kTask7V2ScheduleIdentityPrefix =
    "phase6_task7_dataset_collection_schedule.v2.";
inline constexpr std::string_view kTask7V2AuthoritySchemaId =
    "ocgforge.phase6.task7.dataset_authority.v2";
inline constexpr std::string_view kTask7V2CollectorSemanticVersion =
    "ocgforge.phase6.task7.v2.provisioner.1";

struct Task7CollectionJobV2 final {
    std::string collection_profile = std::string(kTask7V2ReferenceSchemaId);
    std::string environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV3ContractId);
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
    std::string teacher_producer_identity = "ocgforge.policy.teacher_core.v2";
    std::string teacher_action_adapter_identity =
        "ocgforge.policy.public_action_key.v2";
    std::string teacher_sampling_identity =
        "ocgforge.policy.deterministic_lexicographic_argmax.v1";
    std::string policy_rng_identity = "ocgforge.no_policy_rng.v1";
    std::uint64_t engine_process_budget = 20000;
    std::uint64_t semantic_action_budget = 20000;
    std::string cancellation_reason = "ADMINISTRATIVE_CANCEL";
    std::string cancellation_source =
        "phase6-task7-v2-dataset-authority-provisioning";
    std::string collector_semantic_version =
        std::string(kTask7V2CollectorSemanticVersion);
    std::string collector_source_commit;
};

struct Task7CollectionScheduleV2 final {
    std::string collection_profile = std::string(kTask7V2ReferenceSchemaId);
    std::string environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV3ContractId);
    std::vector<std::uint64_t> seeds;
    std::vector<environment::SeatAssignment> placements;
    std::vector<std::uint8_t> starting_players;
    std::vector<Task7CollectionJobV2> jobs;
};

struct Task7V2JobOutcome final {
    Task7CollectionJobV2 job;
    policy::TeacherRunnerV3TrajectoryRunResult run;
};

struct Task7V2DatasetAuthority final {
    Task7CollectionScheduleV2 schedule;
    std::vector<Task7V2JobOutcome> outcomes;
    trajectory::DatasetManifestV2 dataset_manifest;
    TrainingDatasetSplitV1 split;
    model::CardVocabularyV1 vocabulary;
};

struct Task7V2ProvisioningResult final {
    std::optional<Task7V2DatasetAuthority> value;
    std::vector<Task7V2JobOutcome> outcomes;
    std::optional<std::string> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

using Task7V2JobExecutor =
    std::function<policy::TeacherRunnerV3TrajectoryRunResult(
        const Task7CollectionJobV2&)>;

// The executor overload is a bounded test/controlled-integration seam. The
// production provisioning entry point below always uses
// run_task7_collection_job_v2(), which calls TeacherRunnerV3TrajectoryRunner.

Task7CollectionScheduleV2 make_task7_collection_schedule_v2(
    std::string collector_source_commit);

std::vector<std::uint8_t> canonical_task7_collection_job_bytes_v2(
    const Task7CollectionJobV2& job);
trajectory::DecodeResult<Task7CollectionJobV2> decode_task7_collection_job_v2(
    const std::vector<std::uint8_t>& bytes) noexcept;
std::string task7_collection_job_identity_v2(const Task7CollectionJobV2& job);

std::vector<std::uint8_t> canonical_task7_collection_schedule_bytes_v2(
    const Task7CollectionScheduleV2& schedule);
trajectory::DecodeResult<Task7CollectionScheduleV2>
decode_task7_collection_schedule_v2(
    const std::vector<std::uint8_t>& bytes) noexcept;
std::string task7_collection_schedule_identity_v2(
    const Task7CollectionScheduleV2& schedule);

policy::TeacherRunnerV3TrajectoryRunResult run_task7_collection_job_v2(
    const Task7CollectionJobV2& job) noexcept;

Task7V2ProvisioningResult provision_task7_dataset_authority_v2(
    const Task7CollectionScheduleV2& schedule,
    const Task7V2JobExecutor& executor);

Task7V2ProvisioningResult provision_task7_dataset_authority_v2(
    const Task7CollectionScheduleV2& schedule);

Phase6SplitResult derive_training_dataset_split_v1_from_v2(
    std::string source_dataset_identity,
    const std::vector<std::string>& episode_semantic_ids) noexcept;

model::CardVocabularyResult derive_card_vocabulary_v1_from_public_observations(
    const std::vector<environment::PublicEnvironmentObservation>& observations) noexcept;

std::vector<std::uint8_t> canonical_task7_v2_authority_bytes(
    const Task7V2DatasetAuthority& authority);
std::string task7_v2_authority_identity(
    const Task7V2DatasetAuthority& authority);

}  // namespace ygo::phase6
