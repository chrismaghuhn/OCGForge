#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/phase6/task7_input_materialization_v2.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/dataset_manifest_v3.hpp"
#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trajectory/receipt_v3.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"
#include "ygo/trajectory/shard_v3.hpp"

namespace ygo::phase6 {
namespace {

struct Task7MaterializationSourceSampleV2 final {
    const Phase6BcSampleV2* sample = nullptr;
    std::string source_task7_authority_identity;
    std::string source_dataset_manifest_identity;
    std::string source_dataset_semantic_identity;
    std::string source_training_dataset_split_identity;
    std::string source_card_vocabulary_identity;
};

struct Task7MaterializationSourceBatchV2 final {
    const model::RaggedModelBatchV2* ragged = nullptr;
    const model::CardVocabularyV1* vocabulary = nullptr;
    std::string source_task7_authority_identity;
    std::string source_dataset_manifest_identity;
    std::string source_dataset_semantic_identity;
    std::string source_training_dataset_split_identity;
    std::string source_card_vocabulary_identity;
    std::vector<Task7MaterializationSourceSampleV2> samples;
};

class MaterializationFailure final {
public:
    explicit MaterializationFailure(const Task7MaterializationErrorCodeV2 code)
        : code_(code) {}

    Task7MaterializationErrorCodeV2 code() const noexcept { return code_; }

private:
    Task7MaterializationErrorCodeV2 code_;
};

[[noreturn]] void fail_materialization(const Task7MaterializationErrorCodeV2 code) {
    throw MaterializationFailure(code);
}

using DiagnosticClock = std::chrono::steady_clock;

void emit_diagnostic(const diagnostics::Task7DiagnosticObserver& observer,
                     std::string_view phase, const std::uint64_t duration_us) noexcept {
    if (!observer) return;
    try {
        diagnostics::Task7DiagnosticEvent event;
        event.phase = std::string(phase);
        event.duration_us = duration_us;
        observer(event);
    } catch (...) {
    }
}

std::uint64_t diagnostic_elapsed_us(const DiagnosticClock::time_point start,
                                    const DiagnosticClock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

constexpr std::string_view kRulesBundleId =
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
constexpr std::string_view kFormatId = "TCG_ADVANCED_2026_05_18";
constexpr std::string_view kDuelMode = "DUEL_MODE_MR5";
constexpr std::uint64_t kDuelFlags = 190464;
constexpr std::string_view kSwordsoulDeckId = "ocgforge.swordsoul_tenyi.ml_v1";
constexpr std::string_view kSwordsoulDeckSha256 =
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
constexpr std::string_view kSalamangreatDeckId = "ocgforge.salamangreat.ml_v1";
constexpr std::string_view kSalamangreatDeckSha256 =
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";

template <typename T>
trajectory::DecodeResult<T> decode_failure(std::string message) noexcept {
    trajectory::DecodeResult<T> result;
    result.error = trajectory::DecodeError{std::move(message)};
    return result;
}

template <typename T>
trajectory::DecodeResult<T> decode_success(T value) noexcept {
    trajectory::DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

bool valid_commit(std::string_view value) noexcept {
    if (value.size() != 40 && value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

bool same_commitments(
    const std::vector<trajectory::AdmissionEntryCommitmentV3>& left,
    const std::vector<trajectory::AdmissionEntryCommitmentV3>& right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].trajectory_record_id != right[index].trajectory_record_id ||
            left[index].public_gameplay_trajectory_id !=
                right[index].public_gameplay_trajectory_id ||
            left[index].environment_semantic_id != right[index].environment_semantic_id ||
            left[index].episode_semantic_id != right[index].episode_semantic_id ||
            left[index].episode_envelope_sha256 != right[index].episode_envelope_sha256 ||
            left[index].closure_kind != right[index].closure_kind) {
            return false;
        }
    }
    return true;
}

void require_u32_count(const std::size_t size, const char* field) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(std::string("Task7 V3 ") + field + " exceeds u32");
    }
}

std::size_t deck_index_for(const environment::SeatAssignment placement,
                           const std::uint8_t player) {
    if (player > 1 || (placement != environment::SeatAssignment::Normal &&
                       placement != environment::SeatAssignment::Mirror)) {
        throw std::invalid_argument("Task7 V3 placement/player is invalid");
    }
    return placement == environment::SeatAssignment::Mirror ? 1u - player : player;
}

void validate_job(const Task7CollectionJobV3& job) {
    if (job.collection_profile != kTask7V3ReferenceSchemaId ||
        job.environment_contract_id != environment::kEpisodicEnvironmentV4ContractId ||
        job.matchup_id != "ocgforge.matchup.swordsoul_salamangreat.v1" ||
        job.rules_bundle_id != kRulesBundleId || job.format_id != kFormatId ||
        job.duel_mode != kDuelMode || job.duel_flags != kDuelFlags ||
        job.starting_player > 1 || job.engine_process_budget != 20000 ||
        job.semantic_action_budget != 20000 ||
        job.cancellation_reason != "ADMINISTRATIVE_CANCEL" ||
        job.cancellation_source != "phase6-task7-v2-dataset-authority-provisioning" ||
        job.teacher_producer_identity != "ocgforge.policy.teacher_core.v3" ||
        job.teacher_action_adapter_identity != "ocgforge.policy.public_action_key.v3" ||
        job.teacher_sampling_identity !=
            "ocgforge.policy.deterministic_lexicographic_argmax.v1" ||
        job.policy_rng_identity != "ocgforge.no_policy_rng.v1" ||
        job.collector_semantic_version != kTask7V3CollectorSemanticVersion ||
        !valid_commit(job.collector_source_commit)) {
        throw std::invalid_argument("Task7 V3 job contract is invalid");
    }
    const auto config = environment::CertifiedEnvironmentConfig::canonical_v4();
    if (config.rules_bundle_id != job.rules_bundle_id || config.format_id != job.format_id ||
        config.duel_mode != job.duel_mode || config.duel_flags != job.duel_flags ||
        config.locked_decks.size() != 2) {
        throw std::invalid_argument("Task7 V3 job is not bound to the canonical V3 curriculum");
    }
    const auto swordsoul = policy::make_teacher_policy_artifact_v3(
        teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat = policy::make_teacher_policy_artifact_v3(
        teacher::make_salamangreat_profile());
    const auto swordsoul_binding = policy::make_teacher_policy_binding_v3(
        teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat_binding = policy::make_teacher_policy_binding_v3(
        teacher::make_salamangreat_profile());
    const std::array<std::string_view, 2> expected_artifacts = {
        swordsoul.policy_artifact_id, salamangreat.policy_artifact_id};
    const std::array<std::string_view, 2> expected_bindings = {
        swordsoul_binding.teacher_policy_binding_id,
        salamangreat_binding.teacher_policy_binding_id};
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto index = deck_index_for(job.seat_assignment, player);
        const auto expected_role = index == 0 ? trajectory::DeckRole::FirstLockedDeck
                                               : trajectory::DeckRole::SecondLockedDeck;
        const auto expected_deck_id = index == 0 ? kSwordsoulDeckId : kSalamangreatDeckId;
        const auto expected_deck_sha = index == 0 ? kSwordsoulDeckSha256 : kSalamangreatDeckSha256;
        const auto expected_artifact = expected_artifacts[index];
        const auto expected_binding = expected_bindings[index];
        if (job.deck_roles[player] != expected_role || job.deck_ids[player] != expected_deck_id ||
            job.deck_sha256[player] != expected_deck_sha ||
            job.teacher_policy_artifact_ids[player] != expected_artifact ||
            job.teacher_policy_binding_ids[player] != expected_binding) {
            throw std::invalid_argument("Task7 V3 job seat provenance is invalid");
        }
    }
}

void validate_schedule(const Task7CollectionScheduleV3& schedule) {
    if (schedule.collection_profile != kTask7V3ReferenceSchemaId ||
        schedule.environment_contract_id != environment::kEpisodicEnvironmentV4ContractId ||
        schedule.seeds != std::vector<std::uint64_t>{4, 6, 8, 9} ||
        schedule.placements.size() != 2 || schedule.starting_players != std::vector<std::uint8_t>{0, 1} ||
        schedule.jobs.size() != 16) {
        throw std::invalid_argument("Task7 V3 schedule shape is invalid");
    }
    if (schedule.placements[0] != environment::SeatAssignment::Normal ||
        schedule.placements[1] != environment::SeatAssignment::Mirror) {
        throw std::invalid_argument("Task7 V3 schedule placement order is invalid");
    }
    std::set<std::string> identities;
    std::size_t index = 0;
    for (const auto seed : schedule.seeds) {
        for (const auto placement : schedule.placements) {
            for (const auto starting_player : schedule.starting_players) {
                const auto& job = schedule.jobs[index++];
                validate_job(job);
                if (job.root_seed != seed || job.seat_assignment != placement ||
                    job.starting_player != starting_player ||
                    !identities.insert(task7_collection_job_identity_v3(job)).second) {
                    throw std::invalid_argument("Task7 V3 schedule order or duplicate job");
                }
            }
        }
    }
}

struct Task7TeacherSetup final {
    teacher::StrategyProfileV1 swordsoul_profile;
    teacher::StrategyProfileV1 salamangreat_profile;
    trajectory::PolicyArtifact swordsoul_artifact;
    trajectory::PolicyArtifact salamangreat_artifact;
    std::vector<trajectory::ParticipantPolicyAssignment> assignments;
    trajectory::PolicyProvenanceEnvelope provenance;
};

Task7TeacherSetup make_task7_teacher_setup(const Task7CollectionJobV3& job) {
    validate_job(job);
    const auto config = environment::CertifiedEnvironmentConfig::canonical_v4();
    Task7TeacherSetup setup;
    setup.swordsoul_profile = teacher::make_swordsoul_tenyi_profile();
    setup.salamangreat_profile = teacher::make_salamangreat_profile();
    setup.swordsoul_artifact = policy::make_teacher_policy_artifact_v3(
        setup.swordsoul_profile);
    setup.salamangreat_artifact = policy::make_teacher_policy_artifact_v3(
        setup.salamangreat_profile);
    const std::array<trajectory::PolicyRole, 2> roles = {
        trajectory::PolicyRole::Behavior, trajectory::PolicyRole::Opponent};
    setup.assignments = policy::make_teacher_participant_assignments(
        setup.swordsoul_artifact, setup.salamangreat_artifact, config,
        job.seat_assignment, job.starting_player, roles);
    setup.provenance.policy_artifacts = {
        setup.swordsoul_artifact, setup.salamangreat_artifact};
    std::sort(setup.provenance.policy_artifacts.begin(),
              setup.provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    setup.provenance.participant_assignments = setup.assignments;
    return setup;
}

bool finalize_task7_v3_collection(
    const Task7CollectionJobV3& job,
    Task7V3JobOutcome& outcome,
    std::string& error) noexcept {
    try {
        if (outcome.run.error.has_value() || !outcome.run.envelope.has_value() ||
            outcome.run.quarantined) {
            error = outcome.run.diagnostic;
            return false;
        }
        const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes_v3(
            *outcome.run.envelope);
        trajectory::CandidateTrajectoryShardV3 shard;
        shard.entries.push_back({ygo::trace::sha256_bytes(envelope_bytes), envelope_bytes});
        trajectory::RestrictedCollectionEvidenceBundleV3 evidence;
        evidence.candidate_shard_artifact_sha256 =
            trajectory::candidate_shard_artifact_sha256_v3(shard);
        if (std::holds_alternative<trajectory::InterruptedClosureV3>(
                outcome.run.envelope->closure)) {
            error = "Task7 V3 interrupted job lacks restricted replay evidence";
            return false;
        }
        trajectory::replay_v3::ReplayOptions replay_options;
        environment::RunControl run_control;
        run_control.engine_process_budget = job.engine_process_budget;
        run_control.semantic_action_budget = job.semantic_action_budget;
        run_control.cancellation.reason = job.cancellation_reason;
        run_control.cancellation.source = job.cancellation_source;
        replay_options.terminal_run_control = run_control;
        const auto shard_id = trajectory::candidate_shard_artifact_sha256_v3(shard);
        const auto evidence_id =
            trajectory::restricted_collection_evidence_artifact_sha256_v3(evidence);
        std::string admission_error;
        const auto verification = trajectory::admission_v3::verify_collection_for_admission_v3(
            shard, evidence, shard_id, evidence_id, replay_options,
            policy::make_production_policy_provenance_resolver(), &admission_error);
        if (!verification.has_value()) {
            error = admission_error;
            return false;
        }
        const auto receipt = trajectory::issue_admission_receipt_v3(*verification, &admission_error);
        if (!receipt.has_value()) {
            error = admission_error;
            return false;
        }
        trajectory::dataset_v3::DatasetManifestV3 manifest;
        std::vector<std::string> record_ids;
        for (const auto& entry : verification->entries()) {
            record_ids.push_back(entry.trajectory_record_id);
            manifest.members.push_back({
                entry.trajectory_record_id,
                entry.public_gameplay_trajectory_id,
                trajectory::admission_receipt_id_v3(receipt->receipt()),
                entry.trajectory_record_id.empty() ? std::string{} : shard_id,
                entry.episode_envelope_sha256});
        }
        manifest.dataset_semantic_id = trajectory::dataset_v3::dataset_semantic_id_v3(record_ids);
        if (!trajectory::dataset_v3::validate_dataset_manifest_v3(
                manifest, std::vector<trajectory::VerifiedAdmissionReceiptV3>{*receipt},
                &admission_error)) {
            error = admission_error;
            return false;
        }
        outcome.candidate_shard = std::move(shard);
        outcome.restricted_collection_evidence = std::move(evidence);
        outcome.admission_verification = *verification;
        outcome.admission_receipt = *receipt;
        outcome.dataset_manifest = std::move(manifest);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "Task7 V3 collection finalization threw";
        return false;
    }
}

bool eligible_run(const Task7CollectionJobV3& job,
                  const Task7V3JobOutcome& outcome,
                  std::string& error) {
    const auto inspection = inspect_task7_v3_job_run(job, outcome);
    if (inspection.eligible) return true;
    if (!inspection.diagnostic.empty()) {
        error = inspection.diagnostic;
    } else if (inspection.first_failed_condition == "NON_TERMINAL_CLOSURE" ||
               inspection.first_failed_condition == "NON_CLEAN_COLLECTION_DISPOSITION" ||
               inspection.first_failed_condition == "CANDIDATE_SHARD_MEMBER_COUNT_NOT_ONE" ||
               inspection.first_failed_condition == "ADMISSION_RECEIPT_MEMBER_COUNT_NOT_ONE" ||
               inspection.first_failed_condition == "DATASET_MANIFEST_MEMBER_COUNT_NOT_ONE") {
        error = "Task7 V3 job was not a clean terminal single-member admission";
    } else {
        error = outcome.run.diagnostic.empty() ? "Task7 V3 job did not produce complete V3 output"
                                                : outcome.run.diagnostic;
    }
    return false;
}

std::optional<std::vector<std::uint8_t>> extract_safe_state_bytes(
    const environment::PublicEnvironmentObservation& observation) {
    const auto bytes = environment::canonical_public_environment_observation_bytes(observation);
    trajectory::ByteReader reader(bytes);
    std::string schema;
    std::uint8_t perspective = 0;
    std::uint64_t decision_index = 0;
    std::vector<std::uint8_t> safe_state;
    if (!reader.string(schema) || !reader.string(schema) || !reader.u8(perspective) ||
        !reader.u64be(decision_index) || !reader.bytes(safe_state)) {
        return std::nullopt;
    }
    return safe_state;
}

struct DerivedAuthorityValues final {
    trajectory::dataset_v3::DatasetManifestV3 manifest;
    TrainingDatasetSplitV1 split;
    std::optional<model::CardVocabularyV1> vocabulary;
};

bool derive_authority_values(const Task7CollectionScheduleV3& schedule,
                             const std::vector<Task7V3JobOutcome>& outcomes,
                             DerivedAuthorityValues& derived,
                             std::string& error) {
    try {
        validate_schedule(schedule);
        if (outcomes.size() != schedule.jobs.size()) {
            error = "Task7 V3 authority outcome count does not match schedule";
            return false;
        }
        std::vector<trajectory::VerifiedAdmissionReceiptV3> receipts;
        receipts.reserve(outcomes.size());
        std::set<std::string> record_ids;
        std::vector<environment::PublicEnvironmentObservation> observations;
        std::vector<std::string> episode_ids;
        for (std::size_t index = 0; index < outcomes.size(); ++index) {
            const auto& outcome = outcomes[index];
            if (task7_collection_job_identity_v3(outcome.job) !=
                task7_collection_job_identity_v3(schedule.jobs[index])) {
                error = "Task7 V3 authority outcome/job binding mismatch";
                return false;
            }
            std::string job_error;
            if (!eligible_run(outcome.job, outcome, job_error)) {
                error = job_error;
                return false;
            }
            receipts.push_back(*outcome.admission_receipt);
            const auto& receipt = outcome.admission_receipt->receipt();
            const auto receipt_id = trajectory::admission_receipt_id_v3(receipt);
            for (const auto& entry : receipt.entries) {
                if (!record_ids.insert(entry.trajectory_record_id).second) {
                    error = "Task7 V3 authority contains a duplicate trajectory record ID";
                    return false;
                }
                episode_ids.push_back(entry.episode_semantic_id);
                derived.manifest.members.push_back({
                    entry.trajectory_record_id, entry.public_gameplay_trajectory_id, receipt_id,
                    receipt.candidate_shard_artifact_sha256, entry.episode_envelope_sha256});
            }
            for (const auto& record : outcome.run.envelope->records) {
                observations.push_back(record.frame.public_observation);
            }
            if (const auto* terminal = std::get_if<trajectory::TerminalClosureV3>(
                    &outcome.run.envelope->closure)) {
                observations.push_back(terminal->terminal_view_player_0);
                observations.push_back(terminal->terminal_view_player_1);
            }
        }
        if (derived.manifest.members.size() != 16 || episode_ids.size() != 16) {
            error = "Task7 V3 authority does not contain exactly 16 admitted members";
            return false;
        }
        std::sort(derived.manifest.members.begin(), derived.manifest.members.end(),
                  [](const auto& left, const auto& right) {
                      return left.trajectory_record_id < right.trajectory_record_id;
                  });
        std::vector<std::string> record_id_vector;
        record_id_vector.reserve(derived.manifest.members.size());
        for (const auto& member : derived.manifest.members) {
            record_id_vector.push_back(member.trajectory_record_id);
        }
        derived.manifest.dataset_semantic_id =
            trajectory::dataset_v3::dataset_semantic_id_v3(record_id_vector);
        std::string manifest_error;
        if (!trajectory::dataset_v3::validate_dataset_manifest_v3(
                derived.manifest, receipts, &manifest_error)) {
            error = "Task7 V3 DatasetManifest validation failed: " + manifest_error;
            return false;
        }
        std::sort(episode_ids.begin(), episode_ids.end());
        if (std::adjacent_find(episode_ids.begin(), episode_ids.end()) != episode_ids.end()) {
            error = "Task7 V3 authority contains a duplicate episode semantic ID";
            return false;
        }
        const auto split = derive_training_dataset_split_v1_from_v3(
            derived.manifest.dataset_semantic_id, episode_ids);
        if (!split || !split.value.has_value()) {
            error = "Task7 V3 split derivation failed";
            return false;
        }
        derived.split = *split.value;
        const auto vocabulary = derive_card_vocabulary_v1_from_public_observations_v3(observations);
        if (!vocabulary || !vocabulary.value.has_value()) {
            error = "Task7 V3 vocabulary derivation failed";
            return false;
        }
        derived.vocabulary = *vocabulary.value;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "Task7 V3 authority derivation threw";
        return false;
    }
}

trajectory::DecodeResult<TrainingDatasetSplitV1> decode_phase6_split_v1(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        TrainingDatasetSplitV1 split;
        std::string domain;
        std::string schema;
        if (!reader.string(domain) || domain != kPhase6DatasetSplitIdentityDomain ||
            !reader.string(schema) || schema != kPhase6DatasetSplitIdentityDomain ||
            !reader.string(split.source_dataset_identity) ||
            !reader.string(split.split_contract_identity) ||
            !reader.string(split.split_seed_or_partition_identity)) {
            return decode_failure<TrainingDatasetSplitV1>("malformed Task7 V2 split header");
        }
        const auto read_group = [&reader](std::vector<std::string>& output) {
            std::uint32_t count = 0;
            if (!reader.u32be(count) || count > reader.remaining() / 4) return false;
            output.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                std::string value;
                if (!reader.string(value)) return false;
                output.push_back(std::move(value));
            }
            return true;
        };
        if (!read_group(split.train_episode_ids) ||
            !read_group(split.validation_episode_ids) ||
            !read_group(split.test_episode_ids) || !reader.at_end() ||
            canonical_phase6_split_identity_bytes(split) != bytes) {
            return decode_failure<TrainingDatasetSplitV1>("noncanonical Task7 V2 split");
        }
        split.split_identity = phase6_split_identity(split);
        return decode_success(std::move(split));
    } catch (const std::exception& exception) {
        return decode_failure<TrainingDatasetSplitV1>(exception.what());
    } catch (...) {
        return decode_failure<TrainingDatasetSplitV1>("Task7 V2 split decode threw");
    }
}

trajectory::DecodeResult<model::CardVocabularyV1> decode_card_vocabulary_v1(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        std::string domain;
        std::string schema;
        std::string mapping;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != model::kCardVocabularySchemaId ||
            !reader.string(schema) || schema != model::kCardVocabularySchemaId ||
            !reader.string(mapping) || !reader.u32be(count) ||
            count > reader.remaining() / 4) {
            return decode_failure<model::CardVocabularyV1>(
                "malformed Task7 V2 vocabulary header");
        }
        std::vector<std::uint32_t> passcodes;
        passcodes.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            std::uint32_t passcode = 0;
            if (!reader.u32be(passcode)) {
                return decode_failure<model::CardVocabularyV1>(
                    "truncated Task7 V2 vocabulary");
            }
            passcodes.push_back(passcode);
        }
        if (!reader.at_end()) {
            return decode_failure<model::CardVocabularyV1>(
                "Task7 V2 vocabulary has trailing bytes");
        }
        const auto vocabulary = model::CardVocabularyV1::from_ascending_passcodes(
            std::move(passcodes));
        if (!vocabulary || vocabulary.value->canonical_bytes() != bytes) {
            return decode_failure<model::CardVocabularyV1>(
                "noncanonical Task7 V2 vocabulary");
        }
        return decode_success(std::move(*vocabulary.value));
    } catch (const std::exception& exception) {
        return decode_failure<model::CardVocabularyV1>(exception.what());
    } catch (...) {
        return decode_failure<model::CardVocabularyV1>("Task7 V2 vocabulary decode threw");
    }
}

trajectory::replay_v3::ReplayOptions replay_options_for_task7_job(
    const Task7CollectionJobV3& job) {
    trajectory::replay_v3::ReplayOptions options;
    environment::RunControl control;
    control.engine_process_budget = job.engine_process_budget;
    control.semantic_action_budget = job.semantic_action_budget;
    control.cancellation.reason = job.cancellation_reason;
    control.cancellation.source = job.cancellation_source;
    options.terminal_run_control = std::move(control);
    return options;
}

trajectory::dataset_v3::DatasetManifestV3 make_single_member_manifest(
    const trajectory::admission_v3::AdmissionVerification& verification,
    const trajectory::VerifiedAdmissionReceiptV3& verified_receipt,
    const std::string_view shard_id) {
    const auto& receipt = verified_receipt.receipt();
    trajectory::dataset_v3::DatasetManifestV3 manifest;
    std::vector<std::string> record_ids;
    for (const auto& entry : verification.entries()) {
        record_ids.push_back(entry.trajectory_record_id);
        manifest.members.push_back({
            entry.trajectory_record_id,
            entry.public_gameplay_trajectory_id,
            trajectory::admission_receipt_id_v3(receipt),
            std::string(shard_id),
            entry.episode_envelope_sha256});
    }
    manifest.dataset_semantic_id = trajectory::dataset_v3::dataset_semantic_id_v3(record_ids);
    std::string error;
    if (!trajectory::dataset_v3::validate_dataset_manifest_v3(
            manifest,
            std::vector<trajectory::VerifiedAdmissionReceiptV3>{
                verified_receipt},
            &error)) {
        throw std::invalid_argument("Task7 V3 authority member manifest is invalid: " + error);
    }
    return manifest;
}

void validate_materialization_source_identity_strings(
    const Task7MaterializationSourceBatchV2& source) {
    if (!trajectory::is_canonical_identity(
            source.source_task7_authority_identity,
            "phase6_task7_dataset_authority.v3.")) {
        fail_materialization(Task7MaterializationErrorCodeV2::InvalidAuthorityBinding);
    }
    if (!trajectory::is_canonical_identity(
            source.source_dataset_manifest_identity,
            kTask7V2DatasetManifestIdentityPrefix) ||
        !trajectory::is_lower_hex_digest(source.source_dataset_semantic_identity)) {
        fail_materialization(Task7MaterializationErrorCodeV2::InvalidDatasetBinding);
    }
    if (!trajectory::is_canonical_identity(
            source.source_training_dataset_split_identity,
            "phase6_dataset_split.v1.")) {
        fail_materialization(Task7MaterializationErrorCodeV2::InvalidSplitBinding);
    }
}

void validate_materialization_source_binding(
    const Task7MaterializationSourceBatchV2& source) {
    validate_materialization_source_identity_strings(source);
    if (source.vocabulary == nullptr ||
        source.vocabulary->identity() != source.source_card_vocabulary_identity) {
        fail_materialization(Task7MaterializationErrorCodeV2::InvalidVocabularyBinding);
    }
}

void validate_materialization_sample_binding(
    const Task7MaterializationSourceBatchV2& source,
    const Task7MaterializationSourceSampleV2& value,
    const model::EncodedModelInputV2& reconstructed) {
    if (value.sample == nullptr ||
        value.source_task7_authority_identity != source.source_task7_authority_identity ||
        value.source_dataset_manifest_identity != source.source_dataset_manifest_identity ||
        value.source_dataset_semantic_identity != source.source_dataset_semantic_identity ||
        value.source_training_dataset_split_identity !=
            source.source_training_dataset_split_identity ||
        value.source_card_vocabulary_identity != source.source_card_vocabulary_identity) {
        fail_materialization(Task7MaterializationErrorCodeV2::InvalidSource);
    }
    const auto& sample = *value.sample;
    const auto expected_encoded = model::encode_model_input_v2(
        sample.logical_model_input, *source.vocabulary);
    if (sample.schema_id != kPhase6BcSampleIdentityDomainV2 ||
        phase6_sample_identity_v2(sample) != sample.sample_identity ||
        !trajectory::is_canonical_identity(sample.trajectory_record_id,
                                            "trajectory_record.v3.") ||
        !trajectory::is_lower_hex_digest(sample.episode_semantic_id) ||
        sample.encoded_model_input.card_vocabulary_identity !=
            source.source_card_vocabulary_identity ||
        !expected_encoded || !expected_encoded.value.has_value() ||
        model::canonical_encoded_model_input_bytes(*expected_encoded.value) !=
            model::canonical_encoded_model_input_bytes(sample.encoded_model_input) ||
        model::canonical_encoded_model_input_bytes(sample.encoded_model_input) !=
            model::canonical_encoded_model_input_bytes(reconstructed)) {
        fail_materialization(Task7MaterializationErrorCodeV2::ModelInputMismatch);
    }
    (void)model::canonical_logical_model_input_bytes(sample.logical_model_input);
    (void)model::canonical_model_supervision_sample_bytes_v2(sample.supervision);
    if (sample.supervision.schema_id != model::kModelSupervisionSampleV2SchemaId ||
        sample.supervision.model_input_identity !=
            model::model_input_identity_v2(sample.logical_model_input,
                                           sample.encoded_model_input) ||
        sample.supervision.selected_public_action_key.empty() ||
        sample.supervision.candidate_ordinal >= reconstructed.candidate_features.size() ||
        reconstructed.routing_keys[sample.supervision.candidate_ordinal] !=
            sample.supervision.selected_public_action_key) {
        fail_materialization(Task7MaterializationErrorCodeV2::CandidateDomainMismatch);
    }
}

Task7MaterializationErrorV2 make_materialization_error(
    const Task7MaterializationErrorCodeV2 code) {
    return {code, std::string(task7_materialization_error_code_name_v2(code))};
}

Task7MaterializationResultV2 materialization_failure(
    const Task7MaterializationErrorCodeV2 code) noexcept {
    Task7MaterializationResultV2 result;
    result.error = make_materialization_error(code);
    return result;
}

Task7MaterializationResultV2 materialize_task7_input_v2_from_source(
    const Task7MaterializationSourceBatchV2& source) noexcept {
    try {
        if (source.ragged == nullptr || source.samples.empty() ||
            source.samples.size() != source.ragged->batch_size) {
            fail_materialization(Task7MaterializationErrorCodeV2::InvalidSource);
        }
        validate_materialization_source_binding(source);
        const auto& ragged = *source.ragged;
        (void)model::canonical_model_batch_layout_bytes_v2(ragged);

        Task7MaterializedBatchV2 output;
        output.configuration_identity = task7_materialization_config_identity_v2();
        output.source_task7_authority_identity = source.source_task7_authority_identity;
        output.source_dataset_manifest_identity = source.source_dataset_manifest_identity;
        output.source_dataset_semantic_identity = source.source_dataset_semantic_identity;
        output.source_training_dataset_split_identity =
            source.source_training_dataset_split_identity;
        output.source_card_vocabulary_identity = source.source_card_vocabulary_identity;
        output.ragged = ragged;
        output.samples.reserve(source.samples.size());
        for (std::size_t index = 0; index < source.samples.size(); ++index) {
            const auto reconstructed =
                model::reconstruct_model_batch_sample_v2(ragged, index);
            validate_materialization_sample_binding(source, source.samples[index],
                                                    reconstructed);
            const auto& source_sample = *source.samples[index].sample;
            Task7MaterializedSampleV2 sample;
            sample.source_task7_authority_identity =
                source.source_task7_authority_identity;
            sample.source_dataset_manifest_identity = source.source_dataset_manifest_identity;
            sample.source_dataset_semantic_identity = source.source_dataset_semantic_identity;
            sample.source_training_dataset_split_identity =
                source.source_training_dataset_split_identity;
            sample.source_card_vocabulary_identity = source.source_card_vocabulary_identity;
            sample.source_trajectory_record_id = source_sample.trajectory_record_id;
            sample.source_episode_semantic_id = source_sample.episode_semantic_id;
            sample.source_public_semantic_decision_id =
                source_sample.supervision.source_public_semantic_decision_id;
            sample.source_model_input_identity_v2 =
                source_sample.supervision.model_input_identity;
            sample.supervision = source_sample.supervision;
            sample.logical_model_input = source_sample.logical_model_input;
            sample.encoded_model_input = reconstructed;
            sample.routing_keys = reconstructed.routing_keys;
            sample.canonical_bytes = canonical_task7_materialized_sample_bytes_v2(sample);
            sample.sample_identity = materialized_sample_identity_v2(sample);
            output.samples.push_back(std::move(sample));
        }
        output.canonical_bytes = canonical_task7_materialized_batch_bytes_v2(output);
        return {std::optional<Task7MaterializedBatchV2>(std::move(output)), std::nullopt};
    } catch (const MaterializationFailure& error) {
        return materialization_failure(error.code());
    } catch (const std::bad_alloc&) {
        return materialization_failure(Task7MaterializationErrorCodeV2::InternalFailure);
    } catch (...) {
        return materialization_failure(Task7MaterializationErrorCodeV2::InternalFailure);
    }
}

}  // namespace

Task7V3EligibilityInspection inspect_task7_v3_job_run(
    const Task7CollectionJobV3& job,
    const Task7V3JobOutcome& outcome) noexcept {
    Task7V3EligibilityInspection result;
    const auto& run = outcome.run;
    const auto fail = [&result](const std::string_view condition) {
        result.failed_conditions.emplace_back(condition);
        if (result.first_failed_condition.empty()) result.first_failed_condition = std::string(condition);
    };
    result.run_error_present = run.error.has_value();
    result.envelope_present = run.envelope.has_value();
    result.quarantined = run.quarantined;
    result.replay_evidence_present = outcome.replay_evidence.has_value();
    if (result.replay_evidence_present) fail("UNEXPECTED_REPLAY_EVIDENCE");
    result.candidate_shard_present = outcome.candidate_shard.has_value();
    result.restricted_collection_evidence_present = outcome.restricted_collection_evidence.has_value();
    result.admission_verification_present = outcome.admission_verification.has_value();
    result.admission_receipt_present = outcome.admission_receipt.has_value();
    result.dataset_manifest_present = outcome.dataset_manifest.has_value();
    if (result.run_error_present) fail("RUN_ERROR_PRESENT");
    if (!result.envelope_present) fail("ENVELOPE_ABSENT");
    if (result.quarantined) fail("QUARANTINED");
    if (!result.candidate_shard_present) fail("CANDIDATE_SHARD_ABSENT");
    if (!result.restricted_collection_evidence_present) fail("RESTRICTED_EVIDENCE_ABSENT");
    if (!result.admission_verification_present) fail("ADMISSION_VERIFICATION_ABSENT");
    if (!result.admission_receipt_present) fail("ADMISSION_RECEIPT_ABSENT");
    if (!result.dataset_manifest_present) fail("DATASET_MANIFEST_ABSENT");
    if (!result.envelope_present || !result.candidate_shard_present ||
        !result.admission_receipt_present || !result.dataset_manifest_present ||
        !result.restricted_collection_evidence_present ||
        !result.admission_verification_present || result.quarantined ||
        result.run_error_present) {
        result.diagnostic = run.diagnostic;
        return result;
    }
    result.terminal_closure = std::holds_alternative<trajectory::TerminalClosureV3>(run.envelope->closure);
    result.clean_collection_disposition =
        run.envelope->manifest.collection_disposition.kind == trajectory::CollectionDispositionKind::Clean;
    result.candidate_shard_entry_count = outcome.candidate_shard->entries.size();
    result.admission_receipt_entry_count = outcome.admission_receipt->receipt().entries.size();
    result.dataset_manifest_member_count = outcome.dataset_manifest->members.size();
    if (!result.terminal_closure) fail("NON_TERMINAL_CLOSURE");
    if (!result.clean_collection_disposition) fail("NON_CLEAN_COLLECTION_DISPOSITION");
    if (result.candidate_shard_entry_count != 1) fail("CANDIDATE_SHARD_MEMBER_COUNT_NOT_ONE");
    if (result.admission_receipt_entry_count != 1) fail("ADMISSION_RECEIPT_MEMBER_COUNT_NOT_ONE");
    if (result.dataset_manifest_member_count != 1) fail("DATASET_MANIFEST_MEMBER_COUNT_NOT_ONE");
    if (!result.failed_conditions.empty()) return result;
    try {
        std::string binding_error;
        if (!validate_task7_v3_job_episode_binding(job, *run.envelope, &binding_error)) {
            fail("JOB_EPISODE_BINDING_MISMATCH"); result.diagnostic = std::move(binding_error); return result;
        }
        const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes_v3(*run.envelope);
        const auto shard_bytes = trajectory::canonical_candidate_trajectory_shard_bytes_v3(*outcome.candidate_shard);
        const auto evidence_bytes = trajectory::canonical_restricted_collection_evidence_bundle_bytes_v3(*outcome.restricted_collection_evidence);
        const auto receipt_bytes = trajectory::canonical_admission_receipt_bytes_v3(outcome.admission_receipt->receipt());
        const auto shard_artifact_sha256 = ygo::trace::sha256_bytes(shard_bytes);
        const auto evidence_artifact_sha256 = ygo::trace::sha256_bytes(evidence_bytes);
        (void)trajectory::dataset_v3::canonical_dataset_manifest_bytes_v3(*outcome.dataset_manifest);
        const auto decoded_receipt = trajectory::decode_admission_receipt_v3(receipt_bytes);
        if (!decoded_receipt ||
            !same_commitments(decoded_receipt.value->entries, outcome.admission_receipt->receipt().entries) ||
            !same_commitments(outcome.admission_verification->entries(), outcome.admission_receipt->receipt().entries) ||
            ygo::trace::sha256_bytes(envelope_bytes) != outcome.candidate_shard->entries.front().episode_envelope_sha256 ||
            shard_artifact_sha256 != outcome.admission_receipt->receipt().candidate_shard_artifact_sha256 ||
            evidence_artifact_sha256 != outcome.admission_receipt->receipt().restricted_evidence_artifact_sha256 ||
            outcome.restricted_collection_evidence->candidate_shard_artifact_sha256 != shard_artifact_sha256 ||
            outcome.admission_verification->shard_artifact_sha256() != shard_artifact_sha256 ||
            outcome.admission_verification->restricted_evidence_artifact_sha256() != evidence_artifact_sha256) {
            fail("ARTIFACT_COMMITMENT_MISMATCH");
            result.diagnostic = "Task7 V3 job artifact commitments are inconsistent";
            return result;
        }
        std::string dataset_error;
        if (!trajectory::dataset_v3::validate_dataset_manifest_v3(
                *outcome.dataset_manifest,
                std::vector<trajectory::VerifiedAdmissionReceiptV3>{*outcome.admission_receipt},
                &dataset_error)) {
            fail("DATASET_MANIFEST_INVALID");
            result.diagnostic = "Task7 V3 job DatasetManifest validation failed: " + dataset_error;
            return result;
        }
        result.eligible = true;
        return result;
    } catch (const std::exception& exception) {
        fail("ELIGIBILITY_INSPECTION_EXCEPTION"); result.diagnostic = exception.what(); return result;
    } catch (...) {
        fail("ELIGIBILITY_INSPECTION_EXCEPTION"); result.diagnostic = "Task7 V3 eligibility inspection threw"; return result;
    }
}

trajectory::PolicyProvenanceEnvelope make_task7_v3_policy_provenance(
    const Task7CollectionJobV3& job) {
    return make_task7_teacher_setup(job).provenance;
}

bool validate_task7_v3_job_episode_binding(
    const Task7CollectionJobV3& job,
    const trajectory::EpisodeEnvelopeV3& envelope,
    std::string* error) noexcept {
    try {
        const auto environment_result = trajectory::decode_environment_identity_input_v4(
            envelope.manifest.environment_identity_input);
        if (!environment_result ||
            environment_result.value->contract_id != job.environment_contract_id ||
            !trajectory::is_current_certified_environment_v4(*environment_result.value) ||
            environment_result.value->environment_semantic_id !=
                envelope.manifest.environment_semantic_id) {
            throw std::invalid_argument("Task7 V3 job/environment identity mismatch");
        }
        const auto episode_result = trajectory::decode_episode_identity_input_v4(
            envelope.manifest.episode_identity_input, *environment_result.value);
        if (!episode_result || episode_result.value->contract_id != job.environment_contract_id ||
            episode_result.value->root_seed != job.root_seed ||
            episode_result.value->seat_assignment != job.seat_assignment ||
            episode_result.value->starting_player != job.starting_player ||
            environment::episode_semantic_id(*environment_result.value, *episode_result.value) !=
                envelope.manifest.episode_semantic_id) {
            throw std::invalid_argument("Task7 V3 job/episode identity mismatch");
        }
        const auto expected_provenance = make_task7_v3_policy_provenance(job);
        if (trajectory::canonical_policy_provenance_envelope_bytes(
                expected_provenance) !=
            trajectory::canonical_policy_provenance_envelope_bytes(
                envelope.manifest.policy_provenance)) {
            throw std::invalid_argument("Task7 V3 job/Teacher provenance mismatch");
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return false;
    } catch (...) {
        if (error != nullptr) *error = "Task7 V3 job identity validation threw";
        return false;
    }
}

Task7CollectionScheduleV3 make_task7_collection_schedule_v3(
    std::string collector_source_commit) {
    if (!valid_commit(collector_source_commit)) {
        throw std::invalid_argument("Task7 V3 collector source commit is invalid");
    }
    const auto config = environment::CertifiedEnvironmentConfig::canonical_v4();
    const auto swordsoul = policy::make_teacher_policy_artifact_v3(
        teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat = policy::make_teacher_policy_artifact_v3(
        teacher::make_salamangreat_profile());
    const auto swordsoul_binding = policy::make_teacher_policy_binding_v3(
        teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat_binding = policy::make_teacher_policy_binding_v3(
        teacher::make_salamangreat_profile());

    Task7CollectionScheduleV3 schedule;
    schedule.seeds = {4, 6, 8, 9};
    schedule.placements = {environment::SeatAssignment::Normal,
                           environment::SeatAssignment::Mirror};
    schedule.starting_players = {0, 1};
    for (const auto seed : schedule.seeds) {
        for (const auto placement : schedule.placements) {
            for (const auto starting_player : schedule.starting_players) {
                Task7CollectionJobV3 job;
                job.rules_bundle_id = config.rules_bundle_id;
                job.format_id = config.format_id;
                job.duel_mode = config.duel_mode;
                job.duel_flags = config.duel_flags;
                job.root_seed = seed;
                job.seat_assignment = placement;
                job.starting_player = starting_player;
                job.collector_source_commit = collector_source_commit;
                for (std::uint8_t player = 0; player < 2; ++player) {
                    const auto index = deck_index_for(placement, player);
                    job.deck_roles[player] = index == 0 ? trajectory::DeckRole::FirstLockedDeck
                                                         : trajectory::DeckRole::SecondLockedDeck;
                    job.deck_ids[player] = config.locked_decks[index].id;
                    job.deck_sha256[player] = config.locked_decks[index].sha256;
                    job.teacher_policy_artifact_ids[player] =
                        index == 0 ? swordsoul.policy_artifact_id : salamangreat.policy_artifact_id;
                    job.teacher_policy_binding_ids[player] =
                        index == 0 ? swordsoul_binding.teacher_policy_binding_id
                                   : salamangreat_binding.teacher_policy_binding_id;
                }
                validate_job(job);
                schedule.jobs.push_back(std::move(job));
            }
        }
    }
    validate_schedule(schedule);
    return schedule;
}

std::vector<std::uint8_t> canonical_task7_collection_job_bytes_v3(
    const Task7CollectionJobV3& job) {
    validate_job(job);
    trajectory::ByteWriter writer;
    writer.string(kTask7V3JobIdentityPrefix);
    writer.string(kTask7V3JobSchemaId);
    writer.string(job.collection_profile);
    writer.string(job.environment_contract_id);
    writer.string(job.matchup_id);
    writer.string(job.rules_bundle_id);
    writer.string(job.format_id);
    writer.string(job.duel_mode);
    writer.u64be(job.duel_flags);
    writer.u64be(job.root_seed);
    writer.u8(static_cast<std::uint8_t>(job.seat_assignment));
    writer.u8(job.starting_player);
    for (std::uint8_t seat = 0; seat < 2; ++seat) {
        writer.u8(static_cast<std::uint8_t>(job.deck_roles[seat]));
        writer.string(job.deck_ids[seat]);
        writer.string(job.deck_sha256[seat]);
        writer.string(job.teacher_policy_artifact_ids[seat]);
        writer.string(job.teacher_policy_binding_ids[seat]);
    }
    writer.string(job.teacher_producer_identity);
    writer.string(job.teacher_action_adapter_identity);
    writer.string(job.teacher_sampling_identity);
    writer.string(job.policy_rng_identity);
    writer.u64be(job.engine_process_budget);
    writer.u64be(job.semantic_action_budget);
    writer.string(job.cancellation_reason);
    writer.string(job.cancellation_source);
    writer.string(job.collector_semantic_version);
    writer.string(job.collector_source_commit);
    return std::move(writer).take();
}

trajectory::DecodeResult<Task7CollectionJobV3> decode_task7_collection_job_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        Task7CollectionJobV3 job;
        std::string domain;
        std::string schema;
        std::uint8_t placement = 0;
        std::uint8_t role = 0;
        if (!reader.string(domain) || domain != kTask7V3JobIdentityPrefix ||
            !reader.string(schema) || schema != kTask7V3JobSchemaId ||
            !reader.string(job.collection_profile) || !reader.string(job.environment_contract_id) ||
            !reader.string(job.matchup_id) || !reader.string(job.rules_bundle_id) ||
            !reader.string(job.format_id) || !reader.string(job.duel_mode) ||
            !reader.u64be(job.duel_flags) || !reader.u64be(job.root_seed) ||
            !reader.u8(placement) || !reader.u8(job.starting_player)) {
            return decode_failure<Task7CollectionJobV3>("malformed Task7 V3 job header");
        }
        job.seat_assignment = static_cast<environment::SeatAssignment>(placement);
        for (std::uint8_t seat = 0; seat < 2; ++seat) {
            if (!reader.u8(role) || !reader.string(job.deck_ids[seat]) ||
                !reader.string(job.deck_sha256[seat]) ||
                !reader.string(job.teacher_policy_artifact_ids[seat]) ||
                !reader.string(job.teacher_policy_binding_ids[seat])) {
                return decode_failure<Task7CollectionJobV3>("truncated Task7 V3 job seat");
            }
            job.deck_roles[seat] = static_cast<trajectory::DeckRole>(role);
        }
        if (!reader.string(job.teacher_producer_identity) ||
            !reader.string(job.teacher_action_adapter_identity) ||
            !reader.string(job.teacher_sampling_identity) ||
            !reader.string(job.policy_rng_identity) ||
            !reader.u64be(job.engine_process_budget) ||
            !reader.u64be(job.semantic_action_budget) ||
            !reader.string(job.cancellation_reason) ||
            !reader.string(job.cancellation_source) ||
            !reader.string(job.collector_semantic_version) ||
            !reader.string(job.collector_source_commit) || !reader.at_end()) {
            return decode_failure<Task7CollectionJobV3>("malformed Task7 V3 job body");
        }
        validate_job(job);
        if (canonical_task7_collection_job_bytes_v3(job) != bytes) {
            return decode_failure<Task7CollectionJobV3>("noncanonical Task7 V3 job");
        }
        return decode_success(std::move(job));
    } catch (const std::exception& exception) {
        return decode_failure<Task7CollectionJobV3>(exception.what());
    } catch (...) {
        return decode_failure<Task7CollectionJobV3>("Task7 V3 job decode threw");
    }
}

std::string task7_collection_job_identity_v3(const Task7CollectionJobV3& job) {
    return std::string(kTask7V3JobIdentityPrefix) +
           trace::sha256_bytes(canonical_task7_collection_job_bytes_v3(job));
}

std::vector<std::uint8_t> canonical_task7_collection_schedule_bytes_v3(
    const Task7CollectionScheduleV3& schedule) {
    validate_schedule(schedule);
    trajectory::ByteWriter writer;
    writer.string(kTask7V3ScheduleIdentityPrefix);
    writer.string(kTask7V3ScheduleSchemaId);
    writer.string(schedule.collection_profile);
    writer.string(schedule.environment_contract_id);
    writer.u32be(static_cast<std::uint32_t>(schedule.seeds.size()));
    for (const auto seed : schedule.seeds) writer.u64be(seed);
    writer.u32be(static_cast<std::uint32_t>(schedule.placements.size()));
    for (const auto placement : schedule.placements) {
        writer.u8(static_cast<std::uint8_t>(placement));
    }
    writer.u32be(static_cast<std::uint32_t>(schedule.starting_players.size()));
    for (const auto player : schedule.starting_players) writer.u8(player);
    writer.u32be(static_cast<std::uint32_t>(schedule.jobs.size()));
    for (const auto& job : schedule.jobs) {
        writer.string(task7_collection_job_identity_v3(job));
        writer.bytes(canonical_task7_collection_job_bytes_v3(job));
    }
    return std::move(writer).take();
}

trajectory::DecodeResult<Task7CollectionScheduleV3> decode_task7_collection_schedule_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        Task7CollectionScheduleV3 schedule;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kTask7V3ScheduleIdentityPrefix ||
            !reader.string(schema) || schema != kTask7V3ScheduleSchemaId ||
            !reader.string(schedule.collection_profile) ||
            !reader.string(schedule.environment_contract_id) || !reader.u32be(count)) {
            return decode_failure<Task7CollectionScheduleV3>("malformed Task7 V3 schedule header");
        }
        if (count > 100) return decode_failure<Task7CollectionScheduleV3>("Task7 V3 seed count is excessive");
        schedule.seeds.resize(count);
        for (auto& seed : schedule.seeds) {
            if (!reader.u64be(seed)) return decode_failure<Task7CollectionScheduleV3>("truncated Task7 V3 seeds");
        }
        if (!reader.u32be(count) || count > 10) return decode_failure<Task7CollectionScheduleV3>("malformed Task7 V3 placements");
        schedule.placements.resize(count);
        for (auto& placement : schedule.placements) {
            std::uint8_t value = 0;
            if (!reader.u8(value)) return decode_failure<Task7CollectionScheduleV3>("truncated Task7 V3 placements");
            placement = static_cast<environment::SeatAssignment>(value);
        }
        if (!reader.u32be(count) || count > 10) return decode_failure<Task7CollectionScheduleV3>("malformed Task7 V3 starting players");
        schedule.starting_players.resize(count);
        for (auto& player : schedule.starting_players) {
            if (!reader.u8(player)) return decode_failure<Task7CollectionScheduleV3>("truncated Task7 V3 starting players");
        }
        if (!reader.u32be(count) || count > 16) return decode_failure<Task7CollectionScheduleV3>("malformed Task7 V3 job count");
        schedule.jobs.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            std::string identity;
            std::vector<std::uint8_t> job_bytes;
            if (!reader.string(identity) || !reader.bytes(job_bytes)) {
                return decode_failure<Task7CollectionScheduleV3>("truncated Task7 V3 job");
            }
            const auto decoded_job = decode_task7_collection_job_v3(job_bytes);
            if (!decoded_job || task7_collection_job_identity_v3(*decoded_job.value) != identity) {
                return decode_failure<Task7CollectionScheduleV3>("Task7 V3 schedule job identity mismatch");
            }
            schedule.jobs.push_back(std::move(*decoded_job.value));
        }
        if (!reader.at_end()) return decode_failure<Task7CollectionScheduleV3>("Task7 V3 schedule has trailing bytes");
        validate_schedule(schedule);
        if (canonical_task7_collection_schedule_bytes_v3(schedule) != bytes) {
            return decode_failure<Task7CollectionScheduleV3>("noncanonical Task7 V3 schedule");
        }
        return decode_success(std::move(schedule));
    } catch (const std::exception& exception) {
        return decode_failure<Task7CollectionScheduleV3>(exception.what());
    } catch (...) {
        return decode_failure<Task7CollectionScheduleV3>("Task7 V3 schedule decode threw");
    }
}

std::string task7_collection_schedule_identity_v3(
    const Task7CollectionScheduleV3& schedule) {
    return std::string(kTask7V3ScheduleIdentityPrefix) +
           trace::sha256_bytes(canonical_task7_collection_schedule_bytes_v3(schedule));
}

policy::TeacherRunnerV4TrajectoryRunResult run_task7_collection_job_v3_impl(
    const Task7CollectionJobV3& job,
    const diagnostics::Task7DiagnosticObserver& diagnostic_observer,
    const std::optional<std::uint64_t> decision_limit,
    std::optional<trajectory::RestrictedReplayEvidenceV3>*
        restricted_replay_evidence) noexcept {
    policy::TeacherRunnerV4TrajectoryRunResult result;
    try {
        validate_job(job);
        const auto config = environment::CertifiedEnvironmentConfig::canonical_v4();
        const auto setup_start = DiagnosticClock::now();
        const auto setup = make_task7_teacher_setup(job);
        emit_diagnostic(diagnostic_observer, "JOB_SETUP",
                        diagnostic_elapsed_us(setup_start, DiagnosticClock::now()));
        policy::TeacherRunnerV4Config runner_config;
        for (std::uint8_t player = 0; player < 2; ++player) {
            const auto assignment_it = std::find_if(
                setup.assignments.begin(), setup.assignments.end(),
                [player](const auto& assignment) { return assignment.player == player; });
            if (assignment_it == setup.assignments.end()) {
                result.diagnostic = "Task7 V3 assignment is missing a player";
                return result;
            }
            const bool swordsoul = assignment_it->deck_role == trajectory::DeckRole::FirstLockedDeck;
            const auto& profile = swordsoul ? setup.swordsoul_profile : setup.salamangreat_profile;
            const auto& artifact = swordsoul ? setup.swordsoul_artifact : setup.salamangreat_artifact;
            const auto binding = policy::make_teacher_policy_binding_v3(profile);
            auto session = policy::create_teacher_policy_session_v3(
                profile, binding, artifact, *assignment_it);
            if (!session) {
                result.error = session.error;
                result.diagnostic = session.error.has_value() ? session.error->message :
                                                                  "Task7 V3 session creation failed";
                return result;
            }
            runner_config.sessions[player] = std::move(*session.value);
        }
        environment::EpisodeSpec episode_spec;
        episode_spec.contract_id = std::string(environment::kEpisodicEnvironmentV4ContractId);
        episode_spec.root_seed = job.root_seed;
        episode_spec.seat_assignment = job.seat_assignment;
        episode_spec.starting_player = job.starting_player;
        environment::RunControl run_control;
        run_control.engine_process_budget = job.engine_process_budget;
        run_control.semantic_action_budget = job.semantic_action_budget;
        run_control.cancellation.reason = job.cancellation_reason;
        run_control.cancellation.source = job.cancellation_source;
        const auto runner_create_start = DiagnosticClock::now();
        auto created = policy::TeacherRunnerV4TrajectoryRunner::create(
            policy::TeacherRunnerV4TrajectoryConfig{
                config, episode_spec, run_control, setup.provenance,
                std::move(runner_config), diagnostic_observer});
        emit_diagnostic(diagnostic_observer, "RUNNER_CREATE",
                        diagnostic_elapsed_us(runner_create_start,
                                              DiagnosticClock::now()));
        if (!created) {
            result.error = created.error;
            result.diagnostic = created.error.has_value() ? created.error->message :
                                                              "Task7 V3 runner creation failed";
            return result;
        }
        if (decision_limit.has_value()) {
            if (restricted_replay_evidence != nullptr) {
                auto bounded =
                    policy::detail::TeacherRunnerV4TrajectoryTestAccess::
                        run_until_decision_with_replay_evidence(
                            *created.value, *decision_limit);
                *restricted_replay_evidence =
                    std::move(bounded.restricted_replay_evidence);
                return std::move(bounded.run);
            }
            return policy::detail::TeacherRunnerV4TrajectoryTestAccess::run_until_decision(
                *created.value, *decision_limit);
        }
        return created.value->run();
    } catch (const std::exception& exception) {
        result.diagnostic = exception.what();
        return result;
    } catch (...) {
        result.diagnostic = "Task7 V3 job execution threw";
        return result;
    }
}

policy::TeacherRunnerV4TrajectoryRunResult run_task7_collection_job_v3(
    const Task7CollectionJobV3& job,
    const diagnostics::Task7DiagnosticObserver& diagnostic_observer) noexcept {
    return run_task7_collection_job_v3_impl(
        job, diagnostic_observer, std::nullopt, nullptr);
}

policy::TeacherRunnerV4TrajectoryRunResult
run_task7_collection_job_v3_bounded_for_diagnostics(
    const Task7CollectionJobV3& job,
    const std::uint64_t decision_limit,
    const diagnostics::Task7DiagnosticObserver& diagnostic_observer) noexcept {
    return run_task7_collection_job_v3_impl(
        job, diagnostic_observer, std::optional<std::uint64_t>{decision_limit}, nullptr);
}

Task7V3BoundedDiagnosticResult
run_task7_collection_job_v3_bounded_with_replay_evidence(
    const Task7CollectionJobV3& job,
    const std::uint64_t decision_limit,
    const diagnostics::Task7DiagnosticObserver& diagnostic_observer) noexcept {
    Task7V3BoundedDiagnosticResult result;
    result.run = run_task7_collection_job_v3_impl(
        job, diagnostic_observer, std::optional<std::uint64_t>{decision_limit},
        &result.restricted_replay_evidence);
    return result;
}

policy::TeacherRunnerV4TrajectoryRunResult run_task7_collection_job_v3(
    const Task7CollectionJobV3& job) noexcept {
    return run_task7_collection_job_v3(job, {});
}

Task7V3ProvisioningResult provision_task7_dataset_authority_v3(
    const Task7CollectionScheduleV3& schedule,
    const Task7V3JobExecutor& executor) {
    Task7V3ProvisioningResult result;
    try {
        validate_schedule(schedule);
        if (!executor) throw std::invalid_argument("Task7 V3 executor is missing");
        std::vector<Task7V3JobOutcome> outcomes;
        outcomes.reserve(schedule.jobs.size());
        bool failed = false;
        std::string first_error;
        for (const auto& job : schedule.jobs) {
            auto run = executor(job);
            Task7V3JobOutcome outcome{job, std::move(run)};
            std::string error;
            if (!finalize_task7_v3_collection(job, outcome, error) ||
                !eligible_run(job, outcome, error)) {
                failed = true;
                if (first_error.empty()) first_error = error;
            }
            outcomes.push_back(std::move(outcome));
        }
        result.outcomes = outcomes;
        if (failed) {
            result.error = first_error.empty() ? "Task7 V3 collection is unusable" : first_error;
            return result;
        }
        DerivedAuthorityValues derived;
        std::string derivation_error;
        if (!derive_authority_values(schedule, outcomes, derived, derivation_error)) {
            result.error = std::move(derivation_error);
            return result;
        }
        result.value = Task7V3DatasetAuthority{
            schedule, std::move(outcomes), std::move(derived.manifest),
            std::move(derived.split), std::move(*derived.vocabulary)};
        return result;
    } catch (const std::exception& exception) {
        if (!result.error.has_value()) result.error = exception.what();
        return result;
    } catch (...) {
        result.error = "Task7 V3 provisioning threw";
        return result;
    }
}

Task7V3ProvisioningResult provision_task7_dataset_authority_v3(
    const Task7CollectionScheduleV3& schedule) {
    return provision_task7_dataset_authority_v3(
        schedule,
        [](const Task7CollectionJobV3& job) {
            return run_task7_collection_job_v3(job);
        });
}

Phase6SplitResult derive_training_dataset_split_v1_from_v3(
    std::string source_dataset_identity,
    const std::vector<std::string>& episode_semantic_ids) noexcept {
    auto result = make_phase6_split_v1(std::move(source_dataset_identity), episode_semantic_ids);
    if (!result || !result.value.has_value() || result.value->train_episode_ids.empty() ||
        result.value->validation_episode_ids.empty() || result.value->test_episode_ids.empty()) {
        return {std::nullopt,
                Phase6DataError{Phase6DataErrorCode::InvalidSplit,
                                "Task7 V3 split has an empty partition"}};
    }
    return result;
}

model::CardVocabularyResult derive_card_vocabulary_v1_from_public_observations_v3(
    const std::vector<environment::PublicEnvironmentObservation>& observations) noexcept {
    try {
        std::set<std::uint32_t> passcodes;
        for (const auto& observation : observations) {
            const auto safe_bytes = extract_safe_state_bytes(observation);
            if (!safe_bytes.has_value()) {
                return {std::nullopt,
                        model::CardVocabularyError{model::CardVocabularyErrorCode::InvalidPasscodeList,
                                                    "public observation safe-state decode failed"}};
            }
            const auto decoded = environment::decode_canonical_public_safe_state(*safe_bytes);
            if (!decoded) {
                return {std::nullopt,
                        model::CardVocabularyError{model::CardVocabularyErrorCode::InvalidPasscodeList,
                                                    "public safe-state decode failed"}};
            }
            const auto& safe = *decoded.value;
            const auto add = [&passcodes](const std::uint32_t value) {
                if (value != 0) passcodes.insert(value);
            };
            const auto add_deck = [&add](const observation::StaticDeckContext& deck) {
                if (!deck.known) return;
                for (const auto value : deck.main_deck) add(value);
                for (const auto value : deck.extra_deck) add(value);
            };
            add_deck(safe.match_context().own_deck);
            add_deck(safe.match_context().opponent_deck);
            for (const auto& entity : safe.entities()) {
                if (entity.identity_known && entity.passcode.has_value()) add(*entity.passcode);
            }
            for (const auto& event : safe.visible_events()) {
                if (event.public_passcode.has_value()) add(*event.public_passcode);
            }
        }
        return model::CardVocabularyV1::from_ascending_passcodes(
            std::vector<std::uint32_t>(passcodes.begin(), passcodes.end()));
    } catch (const std::exception& exception) {
        return {std::nullopt,
                model::CardVocabularyError{model::CardVocabularyErrorCode::InternalFailure,
                                            exception.what()}};
    } catch (...) {
        return {std::nullopt,
                model::CardVocabularyError{model::CardVocabularyErrorCode::InternalFailure,
                                            "Task7 V3 vocabulary derivation threw"}};
    }
}

bool validate_task7_v3_authority(
    const Task7V3DatasetAuthority& authority,
    std::string* error) noexcept {
    try {
        validate_schedule(authority.schedule);
        if (authority.outcomes.size() != authority.schedule.jobs.size()) {
            throw std::invalid_argument("Task7 V3 authority outcome count is invalid");
        }
        for (std::size_t index = 0; index < authority.outcomes.size(); ++index) {
            if (task7_collection_job_identity_v3(authority.outcomes[index].job) !=
                task7_collection_job_identity_v3(authority.schedule.jobs[index])) {
                throw std::invalid_argument("Task7 V3 authority outcome order is invalid");
            }
        }
        DerivedAuthorityValues derived;
        std::string derivation_error;
        if (!derive_authority_values(authority.schedule, authority.outcomes, derived,
                                     derivation_error)) {
            throw std::invalid_argument(derivation_error);
        }
        if (trajectory::dataset_v3::canonical_dataset_manifest_bytes_v3(
                authority.dataset_manifest) !=
                trajectory::dataset_v3::canonical_dataset_manifest_bytes_v3(
                    derived.manifest) ||
            canonical_phase6_split_identity_bytes(authority.split) !=
                canonical_phase6_split_identity_bytes(derived.split) ||
            !derived.vocabulary.has_value() ||
            authority.vocabulary.canonical_bytes() != derived.vocabulary->canonical_bytes()) {
            throw std::invalid_argument("Task7 V3 authority detached derived values");
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return false;
    } catch (...) {
        if (error != nullptr) *error = "Task7 V3 authority validation threw";
        return false;
    }
}

std::vector<std::uint8_t> canonical_task7_v3_authority_bytes(
    const Task7V3DatasetAuthority& authority) {
    std::string validation_error;
    if (!validate_task7_v3_authority(authority, &validation_error)) {
        throw std::invalid_argument("Task7 V3 authority closure failed: " + validation_error);
    }
    const auto manifest_bytes = trajectory::dataset_v3::canonical_dataset_manifest_bytes_v3(
        authority.dataset_manifest);
    const auto vocabulary_bytes = authority.vocabulary.canonical_bytes();
    trajectory::ByteWriter writer;
    writer.string(kTask7V3AuthoritySchemaId);
    writer.string(kTask7V3AuthoritySchemaId);
    writer.bytes(canonical_task7_collection_schedule_bytes_v3(authority.schedule));
    writer.u32be(static_cast<std::uint32_t>(authority.outcomes.size()));
    for (const auto& outcome : authority.outcomes) {
        const auto job_id = task7_collection_job_identity_v3(outcome.job);
        std::string error;
        if (!eligible_run(outcome.job, outcome, error)) {
            throw std::invalid_argument("Task7 V3 authority contains an ineligible job: " + error);
        }
        writer.string(job_id);
        writer.bytes(trajectory::canonical_episode_envelope_bytes_v3(*outcome.run.envelope));
        writer.bytes(trajectory::canonical_candidate_trajectory_shard_bytes_v3(
            *outcome.candidate_shard));
        writer.bytes(trajectory::canonical_restricted_collection_evidence_bundle_bytes_v3(
            *outcome.restricted_collection_evidence));
        writer.bytes(trajectory::canonical_admission_receipt_bytes_v3(
            outcome.admission_receipt->receipt()));
    }
    writer.bytes(manifest_bytes);
    writer.bytes(canonical_phase6_split_identity_bytes(authority.split));
    writer.bytes(vocabulary_bytes);
    return std::move(writer).take();
}

std::string task7_v3_authority_identity(
    const Task7V3DatasetAuthority& authority) {
    return "phase6_task7_dataset_authority.v3." +
           trace::sha256_bytes(canonical_task7_v3_authority_bytes(authority));
}

VerifiedTask7V3Authority::VerifiedTask7V3Authority(
    std::shared_ptr<const Task7V3DatasetAuthority> value,
    std::string identity)
    : value_(std::move(value)), identity_(std::move(identity)) {
    if (!value_) throw std::invalid_argument("Task7 V3 authority capability is empty");
}

const Task7V3DatasetAuthority& VerifiedTask7V3Authority::value() const noexcept {
    return *value_;
}

trajectory::DecodeResult<VerifiedTask7V3Authority> decode_task7_v3_authority(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        std::string domain;
        std::string schema;
        std::vector<std::uint8_t> schedule_bytes;
        std::uint32_t outcome_count = 0;
        if (!reader.string(domain) || domain != kTask7V3AuthoritySchemaId ||
            !reader.string(schema) || schema != kTask7V3AuthoritySchemaId ||
            !reader.bytes(schedule_bytes) || !reader.u32be(outcome_count) ||
            outcome_count > 16) {
            return decode_failure<VerifiedTask7V3Authority>(
                "malformed Task7 V3 authority header");
        }
        const auto schedule = decode_task7_collection_schedule_v3(schedule_bytes);
        if (!schedule || schedule.value->jobs.size() != outcome_count) {
            return decode_failure<VerifiedTask7V3Authority>(
                "Task7 V3 authority schedule/count mismatch");
        }

        std::vector<Task7V3JobOutcome> outcomes;
        outcomes.reserve(outcome_count);
        for (std::uint32_t index = 0; index < outcome_count; ++index) {
            std::string job_id;
            std::vector<std::uint8_t> envelope_bytes;
            std::vector<std::uint8_t> shard_bytes;
            std::vector<std::uint8_t> evidence_bytes;
            std::vector<std::uint8_t> receipt_bytes;
            if (!reader.string(job_id) || !reader.bytes(envelope_bytes) ||
                !reader.bytes(shard_bytes) || !reader.bytes(evidence_bytes) ||
                !reader.bytes(receipt_bytes)) {
                return decode_failure<VerifiedTask7V3Authority>(
                    "truncated Task7 V3 authority outcome");
            }
            const auto& job = schedule.value->jobs[index];
            if (job_id != task7_collection_job_identity_v3(job)) {
                return decode_failure<VerifiedTask7V3Authority>(
                    "Task7 V3 authority job identity mismatch");
            }
            const auto envelope = trajectory::decode_episode_envelope_v3(envelope_bytes);
            const auto shard = trajectory::decode_candidate_trajectory_shard_v3(shard_bytes);
            const auto evidence =
                trajectory::decode_restricted_collection_evidence_bundle_v3(evidence_bytes);
            const auto receipt = trajectory::decode_admission_receipt_v3(receipt_bytes);
            if (!envelope || !shard || !evidence || !receipt ||
                !std::holds_alternative<trajectory::TerminalClosureV3>(envelope.value->closure) ||
                !evidence.value->interrupted_episodes.empty()) {
                return decode_failure<VerifiedTask7V3Authority>(
                    "Task7 V3 authority outcome is not a clean terminal record");
            }
            const auto shard_id = trajectory::candidate_shard_artifact_sha256_v3(*shard.value);
            const auto evidence_id =
                trajectory::restricted_collection_evidence_artifact_sha256_v3(*evidence.value);
            trajectory::replay_v3::ReplayOptions replay_options =
                replay_options_for_task7_job(job);
            std::string admission_error;
            const auto verification =
                trajectory::admission_v3::verify_collection_for_admission_v3(
                    *shard.value, *evidence.value, shard_id, evidence_id,
                    replay_options, policy::make_production_policy_provenance_resolver(),
                    &admission_error);
            if (!verification.has_value()) {
                return decode_failure<VerifiedTask7V3Authority>(
                    "Task7 V3 authority replay/admission failed: " + admission_error);
            }
            const auto verified_receipt =
                trajectory::issue_admission_receipt_v3(*verification, &admission_error);
            if (!verified_receipt.has_value() ||
                trajectory::canonical_admission_receipt_bytes_v3(
                    verified_receipt->receipt()) != receipt_bytes ||
                !same_commitments(verified_receipt->receipt().entries,
                                  receipt.value->entries)) {
                return decode_failure<VerifiedTask7V3Authority>(
                    "Task7 V3 authority receipt commitment mismatch");
            }
            Task7V3JobOutcome outcome;
            outcome.job = job;
            outcome.run.envelope = *envelope.value;
            outcome.candidate_shard = *shard.value;
            outcome.restricted_collection_evidence = *evidence.value;
            outcome.admission_verification = *verification;
            outcome.admission_receipt = *verified_receipt;
            outcome.dataset_manifest = make_single_member_manifest(
                *verification, *verified_receipt, shard_id);
            outcomes.push_back(std::move(outcome));
        }

        std::vector<std::uint8_t> manifest_bytes;
        std::vector<std::uint8_t> split_bytes;
        std::vector<std::uint8_t> vocabulary_bytes;
        if (!reader.bytes(manifest_bytes) || !reader.bytes(split_bytes) ||
            !reader.bytes(vocabulary_bytes) || !reader.at_end()) {
            return decode_failure<VerifiedTask7V3Authority>(
                "malformed Task7 V3 authority derived values");
        }
        const auto manifest =
            trajectory::dataset_v3::decode_dataset_manifest_v3(manifest_bytes);
        const auto split = decode_phase6_split_v1(split_bytes);
        const auto vocabulary = decode_card_vocabulary_v1(vocabulary_bytes);
        if (!manifest || !split || !vocabulary) {
            return decode_failure<VerifiedTask7V3Authority>(
                "Task7 V3 authority derived value decode failed");
        }
        Task7V3DatasetAuthority authority{
            *schedule.value,
            std::move(outcomes),
            *manifest.value,
            *split.value,
            *vocabulary.value};
        std::string validation_error;
        if (!validate_task7_v3_authority(authority, &validation_error) ||
            canonical_task7_v3_authority_bytes(authority) != bytes) {
            return decode_failure<VerifiedTask7V3Authority>(
                "Task7 V3 authority closure or canonical bytes failed: " +
                validation_error);
        }
        const auto identity = task7_v3_authority_identity(authority);
        return decode_success(VerifiedTask7V3Authority(
            std::make_shared<const Task7V3DatasetAuthority>(std::move(authority)), identity));
    } catch (const std::exception& exception) {
        return decode_failure<VerifiedTask7V3Authority>(exception.what());
    } catch (...) {
        return decode_failure<VerifiedTask7V3Authority>(
            "Task7 V3 authority decode threw");
    }
}

Phase6DatasetResultV2 materialize_phase6_dataset_v2(
    const VerifiedTask7V3Authority& authority) noexcept {
    try {
        const auto& source = authority.value();
        if (task7_v3_authority_identity(source) != authority.identity()) {
            return {std::nullopt,
                    Phase6DataError{Phase6DataErrorCode::InvalidDatasetManifest,
                                    "Task7 V3 authority identity does not recompute"}};
        }
        std::string error;
        if (!validate_task7_v3_authority(source, &error)) {
            return {std::nullopt,
                    Phase6DataError{Phase6DataErrorCode::InvalidDatasetManifest,
                                    "Task7 V3 authority is not valid: " + error}};
        }
        std::vector<trajectory::VerifiedAdmissionReceiptV3> receipts;
        std::vector<trajectory::EpisodeEnvelopeV3> envelopes;
        receipts.reserve(source.outcomes.size());
        envelopes.reserve(source.outcomes.size());
        for (const auto& outcome : source.outcomes) {
            if (!outcome.admission_receipt.has_value() ||
                !outcome.run.envelope.has_value()) {
                return {std::nullopt,
                        Phase6DataError{Phase6DataErrorCode::InvalidDatasetManifest,
                                        "Task7 V3 authority outcome is incomplete"}};
            }
            receipts.push_back(*outcome.admission_receipt);
            envelopes.push_back(*outcome.run.envelope);
        }
        return materialize_phase6_dataset_v2(
            source.dataset_manifest, receipts, envelopes, source.vocabulary);
    } catch (const std::exception& exception) {
        return {std::nullopt,
                Phase6DataError{Phase6DataErrorCode::InternalFailure, exception.what()}};
    } catch (...) {
        return {std::nullopt,
                Phase6DataError{Phase6DataErrorCode::InternalFailure,
                                "Task7 V3 authority dataset materialization threw"}};
    }
}

Task7MaterializationResultV2 materialize_task7_input_v2(
    const VerifiedTask7V3Authority& authority) noexcept {
    try {
        const auto dataset = materialize_phase6_dataset_v2(authority);
        if (!dataset || !dataset.value.has_value()) {
            Task7MaterializationResultV2 result;
            result.error = Task7MaterializationErrorV2{
                Task7MaterializationErrorCodeV2::InvalidDatasetBinding,
                "Task7 V3 authority could not produce a validated V2 dataset"};
            return result;
        }
        const auto& authority_value = authority.value();
        const auto manifest_bytes =
            trajectory::dataset_v3::canonical_dataset_manifest_bytes_v3(
                authority_value.dataset_manifest);
        Task7MaterializationSourceBatchV2 source;
        source.vocabulary = &authority_value.vocabulary;
        source.source_task7_authority_identity = authority.identity();
        source.source_dataset_manifest_identity =
            std::string(kTask7V2DatasetManifestIdentityPrefix) +
            trace::sha256_bytes(manifest_bytes);
        source.source_dataset_semantic_identity =
            authority_value.dataset_manifest.dataset_semantic_id;
        source.source_training_dataset_split_identity = authority_value.split.split_identity;
        source.source_card_vocabulary_identity = authority_value.vocabulary.identity();

        std::vector<model::EncodedModelInputV2> encoded;
        const auto add_samples = [&](const std::vector<Phase6BcSampleV2>& samples) {
            for (const auto& sample : samples) {
                encoded.push_back(sample.encoded_model_input);
                source.samples.push_back({
                    &sample,
                    source.source_task7_authority_identity,
                    source.source_dataset_manifest_identity,
                    source.source_dataset_semantic_identity,
                    source.source_training_dataset_split_identity,
                    source.source_card_vocabulary_identity});
            }
        };
        add_samples(dataset.value->train_samples);
        add_samples(dataset.value->validation_samples);
        add_samples(dataset.value->test_samples);
        const auto ragged = model::make_ragged_model_batch_v2(encoded);
        if (!ragged || !ragged.value.has_value()) {
            Task7MaterializationResultV2 result;
            result.error = Task7MaterializationErrorV2{
                Task7MaterializationErrorCodeV2::InvalidBatch,
                "Task7 V2 ragged batch construction failed"};
            return result;
        }
        source.ragged = &*ragged.value;
        return materialize_task7_input_v2_from_source(source);
    } catch (const std::exception& exception) {
        Task7MaterializationResultV2 result;
        result.error = Task7MaterializationErrorV2{
            Task7MaterializationErrorCodeV2::InternalFailure, exception.what()};
        return result;
    } catch (...) {
        Task7MaterializationResultV2 result;
        result.error = Task7MaterializationErrorV2{
            Task7MaterializationErrorCodeV2::InternalFailure,
            "Task7 V3 authority physical materialization threw"};
        return result;
    }
}

}  // namespace ygo::phase6
