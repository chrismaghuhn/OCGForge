#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/card_vocabulary.hpp"
#include "ygo/phase6/supervision_dataset.hpp"
#include "ygo/phase6/task7_input_materialization.hpp"
#include "ygo/policy/teacher_runner.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"
#include "ygo/trajectory/shard.hpp"

namespace ygo::phase6::task7 {

inline constexpr std::string_view kTask7CollectionScheduleSchemaId =
    "ocgforge.phase6.task7.dataset_collection_schedule.v1";
inline constexpr std::string_view kTask7CollectionJobSchemaId =
    "ocgforge.phase6.task7.dataset_collection_job.v1";
inline constexpr std::string_view kTask7CollectionScheduleIdentityDomain =
    "ocgforge.phase6.task7.dataset_collection_schedule_identity.v1";
inline constexpr std::string_view kTask7CollectionJobIdentityDomain =
    "ocgforge.phase6.task7.dataset_collection_job_identity.v1";
inline constexpr std::string_view kTask7CollectionScheduleIdentityPrefix =
    "phase6_task7_dataset_collection_schedule.v1.";
inline constexpr std::string_view kTask7CollectionJobIdentityPrefix =
    "phase6_task7_dataset_collection_job.v1.";
inline constexpr std::string_view kTask7CollectionProfileIdentity =
    "ocgforge.phase6.task7.dataset_collection.reference.v1";
inline constexpr std::string_view kTask7CollectionEnvironmentContractId =
    "ocgforge.episodic_environment.v2";
inline constexpr std::string_view kTask7CollectionSemanticVersion =
    "ocgforge.phase6.task7.dataset_authority_provisioning.v1";
inline constexpr std::string_view kTask7CollectionCancellationSource =
    "phase6-task7-dataset-authority-provisioning";
inline constexpr std::uint64_t kTask7CollectionEngineProcessBudget = 20000;
inline constexpr std::uint64_t kTask7CollectionSemanticActionBudget = 20000;

struct Task7CollectionJobV1 final {
    std::string identity_domain = std::string(kTask7CollectionJobIdentityDomain);
    std::string identity_schema = std::string(kTask7CollectionJobSchemaId);
    std::string collection_profile = std::string(kTask7CollectionProfileIdentity);
    std::string environment_contract =
        std::string(kTask7CollectionEnvironmentContractId);
    std::string matchup = "ocgforge.matchup.swordsoul_salamangreat.v1";
    std::string rules_bundle =
        "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
    std::string format = "TCG_ADVANCED_2026_05_18";
    std::string duel_mode = "DUEL_MODE_MR5";
    std::uint64_t duel_flags = 190464;
    std::uint64_t root_seed = 4;
    std::string placement = "NORMAL";
    std::uint8_t starting_player = 0;
    std::string seat_0_deck_role = "ocgforge.swordsoul_tenyi.ml_v1";
    std::string seat_0_deck_sha256 =
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
    std::string seat_1_deck_role = "ocgforge.salamangreat.ml_v1";
    std::string seat_1_deck_sha256 =
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
    std::string seat_0_teacher_artifact =
        "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d";
    std::string seat_0_teacher_binding =
        "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c";
    std::string seat_1_teacher_artifact =
        "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527";
    std::string seat_1_teacher_binding =
        "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56";
    std::string teacher_producer = "ocgforge.policy.teacher_core.v1";
    std::string teacher_selection =
        "ocgforge.policy.deterministic_lexicographic_argmax.v1";
    std::string teacher_rng = "ocgforge.no_policy_rng.v1";
    std::uint64_t engine_process_budget = kTask7CollectionEngineProcessBudget;
    std::uint64_t semantic_action_budget = kTask7CollectionSemanticActionBudget;
    std::string cancellation_reason = "ADMINISTRATIVE_CANCEL";
    std::string cancellation_source = std::string(kTask7CollectionCancellationSource);
    std::string collector_semantic_version =
        std::string(kTask7CollectionSemanticVersion);
    std::string collector_semantic_source_commit;
};

struct Task7CollectionScheduleV1 final {
    std::string schema_id = std::string(kTask7CollectionScheduleSchemaId);
    std::string identity_domain =
        std::string(kTask7CollectionScheduleIdentityDomain);
    std::string collection_profile = std::string(kTask7CollectionProfileIdentity);
    std::string collector_semantic_source_commit;
    std::vector<std::uint64_t> seeds;
    std::vector<std::string> placements;
    std::vector<std::uint8_t> starting_players;
    std::vector<Task7CollectionJobV1> jobs;
};

enum class Task7DatasetAuthorityErrorCode : std::uint8_t {
    InvalidSourceCommit,
    InvalidSchedule,
    CollectionJobFailure,
    IneligibleEpisode,
    ArtifactValidationFailure,
    ManifestFailure,
    SplitFailure,
    EmptyPartition,
    VocabularyFailure,
    MaterializationFailure,
    OutputFailure,
    InternalFailure,
};

struct Task7DatasetAuthorityError final {
    Task7DatasetAuthorityErrorCode code =
        Task7DatasetAuthorityErrorCode::InternalFailure;
    std::string diagnostic;
};

struct Task7CollectionJobArtifactsV1 final {
    Task7CollectionJobV1 job;
    trajectory::EpisodeEnvelope episode_envelope;
    trajectory::CandidateTrajectoryShard candidate_shard;
    trajectory::RestrictedCollectionEvidenceBundle restricted_evidence;
    std::optional<trajectory::VerifiedAdmissionReceipt> admission_receipt;
};

struct Task7DatasetAuthorityV1 final {
    Task7CollectionScheduleV1 schedule;
    std::vector<Task7CollectionJobArtifactsV1> jobs;
    trajectory::DatasetManifest dataset_manifest;
    TrainingDatasetSplitV1 split;
    model::CardVocabularyV1 card_vocabulary;
    Phase6MaterializedDatasetV1 materialized_dataset;
    Task7MaterializedBatchV1 task7_materialization;
};

struct Task7DatasetAuthorityResult final {
    std::optional<Task7DatasetAuthorityV1> value;
    std::optional<Task7DatasetAuthorityError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

Task7CollectionScheduleV1 make_task7_collection_schedule(
    std::string collector_semantic_source_commit);

std::vector<std::uint8_t> canonical_task7_collection_job_bytes(
    const Task7CollectionJobV1& job);
std::string task7_collection_job_identity(const Task7CollectionJobV1& job);
bool validate_task7_collection_job(const Task7CollectionJobV1& job,
                                   std::string* error = nullptr) noexcept;

std::vector<std::uint8_t> canonical_task7_collection_schedule_bytes(
    const Task7CollectionScheduleV1& schedule);
std::string task7_collection_schedule_identity(
    const Task7CollectionScheduleV1& schedule);
bool validate_task7_collection_schedule(
    const Task7CollectionScheduleV1& schedule,
    std::string* error = nullptr) noexcept;

Task7DatasetAuthorityResult provision_task7_dataset_authority(
    std::string collector_semantic_source_commit) noexcept;

bool write_task7_dataset_authority(
    const Task7DatasetAuthorityV1& authority,
    const std::filesystem::path& output_directory,
    std::string* error = nullptr) noexcept;

std::string_view task7_dataset_authority_error_code_name(
    Task7DatasetAuthorityErrorCode code) noexcept;

}  // namespace ygo::phase6::task7
