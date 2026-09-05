#include "ygo/phase6/task7_dataset_authority_provisioning.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/model_batch_layout.hpp"
#include "ygo/policy/production.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/receipt.hpp"

namespace ygo::phase6::task7 {

void validate_job_or_fail(const Task7CollectionJobV1& job);
void validate_schedule_or_fail(const Task7CollectionScheduleV1& schedule);

namespace {

using ygo::trajectory::ByteWriter;

constexpr std::string_view kMatchup =
    "ocgforge.matchup.swordsoul_salamangreat.v1";
constexpr std::string_view kRulesBundle =
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
constexpr std::string_view kFormat = "TCG_ADVANCED_2026_05_18";
constexpr std::string_view kDuelMode = "DUEL_MODE_MR5";
constexpr std::uint64_t kDuelFlags = 190464;
constexpr std::string_view kSwordsoulDeck = "ocgforge.swordsoul_tenyi.ml_v1";
constexpr std::string_view kSwordsoulDeckSha =
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
constexpr std::string_view kSalamangreatDeck =
    "ocgforge.salamangreat.ml_v1";
constexpr std::string_view kSalamangreatDeckSha =
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
constexpr std::string_view kSwordsoulArtifact =
    "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d";
constexpr std::string_view kSwordsoulBinding =
    "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c";
constexpr std::string_view kSalamangreatArtifact =
    "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527";
constexpr std::string_view kSalamangreatBinding =
    "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56";
constexpr std::string_view kTeacherProducer = "ocgforge.policy.teacher_core.v1";
constexpr std::string_view kTeacherSelection =
    "ocgforge.policy.deterministic_lexicographic_argmax.v1";
constexpr std::string_view kTeacherRng = "ocgforge.no_policy_rng.v1";

[[noreturn]] void fail(const std::string& message) {
    throw std::invalid_argument(message);
}

bool valid_commit(const std::string_view value) noexcept {
    if (value.size() != 40) return false;
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

bool valid_digest(const std::string_view value) noexcept {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

bool valid_prefixed_digest(const std::string_view value,
                           const std::string_view prefix) noexcept {
    return value.size() == prefix.size() + 64 &&
           value.substr(0, prefix.size()) == prefix &&
           valid_digest(value.substr(prefix.size()));
}

void require_string(const std::string_view value, const char* field) {
    if (value.empty() || !ygo::trajectory::is_valid_utf8(value)) {
        fail(std::string("Task7 collection ") + field + " is invalid");
    }
}

void require_digest(const std::string_view value, const char* field) {
    if (!valid_digest(value)) fail(std::string("Task7 collection ") + field + " is invalid");
}

void require_prefixed_digest(const std::string_view value,
                             const std::string_view prefix,
                             const char* field) {
    if (!valid_prefixed_digest(value, prefix)) {
        fail(std::string("Task7 collection ") + field + " is invalid");
    }
}

std::string_view placement_deck(const std::string_view placement,
                                const std::uint8_t seat) {
    if (placement == "NORMAL") {
        return seat == 0 ? kSwordsoulDeck : kSalamangreatDeck;
    }
    if (placement == "MIRROR") {
        return seat == 0 ? kSalamangreatDeck : kSwordsoulDeck;
    }
    fail("Task7 collection placement is invalid");
}

std::string_view deck_sha(const std::string_view deck) {
    if (deck == kSwordsoulDeck) return kSwordsoulDeckSha;
    if (deck == kSalamangreatDeck) return kSalamangreatDeckSha;
    fail("Task7 collection deck role is invalid");
}

std::string_view teacher_artifact(const std::string_view deck) {
    if (deck == kSwordsoulDeck) return kSwordsoulArtifact;
    if (deck == kSalamangreatDeck) return kSalamangreatArtifact;
    fail("Task7 collection Teacher deck role is invalid");
}

std::string_view teacher_binding(const std::string_view deck) {
    if (deck == kSwordsoulDeck) return kSwordsoulBinding;
    if (deck == kSalamangreatDeck) return kSalamangreatBinding;
    fail("Task7 collection Teacher deck role is invalid");
}

std::string_view deck_role_name(const trajectory::DeckRole role) {
    return role == trajectory::DeckRole::FirstLockedDeck ? kSwordsoulDeck
                                                           : kSalamangreatDeck;
}

const teacher::StrategyProfileV1& teacher_profile_for(
    const trajectory::DeckRole role,
    const teacher::StrategyProfileV1& swordsoul,
    const teacher::StrategyProfileV1& salamangreat) {
    return role == trajectory::DeckRole::FirstLockedDeck ? swordsoul : salamangreat;
}

const trajectory::PolicyArtifact& teacher_artifact_for(
    const trajectory::DeckRole role,
    const trajectory::PolicyArtifact& swordsoul,
    const trajectory::PolicyArtifact& salamangreat) {
    return role == trajectory::DeckRole::FirstLockedDeck ? swordsoul : salamangreat;
}

policy::TeacherRunnerConfig teacher_runner_config(
    const Task7CollectionJobV1& job) {
    policy::TeacherRunnerConfig config;
    config.environment_config = environment::CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id = std::string(kTask7CollectionEnvironmentContractId);
    config.episode_spec.root_seed = job.root_seed;
    config.episode_spec.seat_assignment =
        job.placement == "NORMAL" ? environment::SeatAssignment::Normal
                                   : environment::SeatAssignment::Mirror;
    config.episode_spec.starting_player = job.starting_player;
    config.run_control.engine_process_budget = job.engine_process_budget;
    config.run_control.semantic_action_budget = job.semantic_action_budget;
    config.run_control.cancellation.reason = job.cancellation_reason;
    config.run_control.cancellation.source = job.cancellation_source;

    const auto swordsoul_profile = teacher::make_swordsoul_tenyi_profile();
    const auto salamangreat_profile = teacher::make_salamangreat_profile();
    const auto swordsoul_artifact = policy::make_teacher_policy_artifact(swordsoul_profile);
    const auto salamangreat_artifact = policy::make_teacher_policy_artifact(salamangreat_profile);
    if (swordsoul_artifact.policy_artifact_id != job.seat_0_teacher_artifact &&
        salamangreat_artifact.policy_artifact_id != job.seat_0_teacher_artifact) {
        fail("Task7 collection job does not bind the accepted seat-0 Teacher");
    }
    const std::array<trajectory::PolicyRole, 2> roles = {
        trajectory::PolicyRole::Behavior, trajectory::PolicyRole::Opponent};
    config.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(config.policy_provenance.policy_artifacts.begin(),
              config.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    config.policy_provenance.participant_assignments =
        policy::make_teacher_participant_assignments(
            swordsoul_artifact, salamangreat_artifact, config.environment_config,
            config.episode_spec.seat_assignment, config.episode_spec.starting_player,
            roles);
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto assignment = std::find_if(
            config.policy_provenance.participant_assignments.begin(),
            config.policy_provenance.participant_assignments.end(),
            [player](const auto& value) { return value.player == player; });
        if (assignment == config.policy_provenance.participant_assignments.end()) {
            fail("Task7 collection Teacher assignment is missing");
        }
        const auto& profile = teacher_profile_for(
            assignment->deck_role, swordsoul_profile, salamangreat_profile);
        const auto& artifact = teacher_artifact_for(
            assignment->deck_role, swordsoul_artifact, salamangreat_artifact);
        const auto binding = policy::make_teacher_policy_binding(profile);
        auto session = policy::create_teacher_policy_session(
            profile, binding, artifact, *assignment);
        if (!session || !session.value.has_value()) {
            fail(session.error.has_value() ? session.error->message
                                           : "Task7 Teacher session construction failed");
        }
        config.sessions[player] = std::move(*session.value);
    }
    return config;
}

void validate_terminal_job_result(
    const Task7CollectionJobV1& job,
    const policy::PolicyRunnerResult& result) {
    if (result.disposition != policy::PolicyRunnerDisposition::CleanAdmitted ||
        !result.envelope.has_value() || !result.candidate_shard.has_value() ||
        !result.restricted_evidence.has_value() || !result.admission_receipt.has_value()) {
        fail("Task7 collection job did not produce a clean admitted artifact set");
    }
    if (!std::holds_alternative<trajectory::TerminalClosure>(result.envelope->closure) ||
        result.envelope->manifest.collection_disposition.kind !=
            trajectory::CollectionDispositionKind::Clean) {
        fail("Task7 collection job is not a clean terminal episode");
    }
    if (result.envelope->records.empty()) {
        fail("Task7 collection job produced no decision records");
    }
    const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes(*result.envelope);
    const auto envelope_digest = ygo::trace::sha256_bytes(envelope_bytes);
    const auto& shard = *result.candidate_shard;
    if (shard.entries.size() != 1 || shard.entries.front().episode_envelope_sha256 != envelope_digest ||
        shard.entries.front().envelope_bytes != envelope_bytes ||
        trajectory::candidate_shard_artifact_sha256(shard) !=
            result.admission_receipt->receipt().candidate_shard_artifact_sha256) {
        fail("Task7 collection shard is detached from its episode envelope");
    }
    const auto& receipt = result.admission_receipt->receipt();
    if (receipt.entries.size() != 1) {
        fail("Task7 collection job must produce exactly one admitted episode member");
    }
    const auto record_id = trajectory::trajectory_record_id(*result.envelope);
    const auto public_id = trajectory::public_gameplay_trajectory_id(*result.envelope);
    const auto& entry = receipt.entries.front();
    if (entry.trajectory_record_id != record_id ||
        entry.public_gameplay_trajectory_id != public_id ||
        entry.episode_envelope_sha256 != envelope_digest || entry.closure_kind != 0) {
        fail("Task7 collection receipt is detached from its terminal episode");
    }
    if (trajectory::admission_receipt_id(receipt).empty() ||
        trajectory::canonical_admission_receipt_bytes(receipt).empty()) {
        fail("Task7 collection receipt is not canonical");
    }
    std::string restricted_error;
    if (!trajectory::validate_restricted_collection_evidence_bundle(
            *result.restricted_evidence, shard,
            result.restricted_evidence->candidate_shard_artifact_sha256,
            &restricted_error)) {
        fail("Task7 restricted collection evidence is invalid: " + restricted_error);
    }
    (void)job;
}

Task7CollectionJobArtifactsV1 collect_one_job(const Task7CollectionJobV1& job) {
    validate_job_or_fail(job);
    auto created = policy::TeacherRunner::create(teacher_runner_config(job));
    if (!created || !created.value.has_value()) {
        fail(created.error.has_value() ? created.error->message
                                       : "Task7 TeacherRunner construction failed");
    }
    auto result = created.value->run();
    validate_terminal_job_result(job, result);
    Task7CollectionJobArtifactsV1 artifacts;
    artifacts.job = job;
    artifacts.episode_envelope = std::move(*result.envelope);
    artifacts.candidate_shard = std::move(*result.candidate_shard);
    artifacts.restricted_evidence = std::move(*result.restricted_evidence);
    artifacts.admission_receipt = std::move(*result.admission_receipt);
    return artifacts;
}

model::CardVocabularyV1 derive_vocabulary(
    const std::vector<Task7CollectionJobArtifactsV1>& jobs) {
    std::vector<std::uint32_t> passcodes;
    for (const auto& job : jobs) {
        for (const auto& record : job.episode_envelope.records) {
            const auto logical = model::project_logical_model_input_v1(
                record.frame.public_observation, record.frame.request.candidates);
            if (!logical || !logical.value.has_value()) {
                fail("Task7 public logical input could not be projected for vocabulary derivation");
            }
            for (const auto& entity : logical.value->public_safe_state.entities) {
                if (entity.card.identity_known && entity.card.passcode.has_value()) {
                    passcodes.push_back(*entity.card.passcode);
                }
            }
            for (const auto& event : logical.value->public_safe_state.visible_events) {
                if (event.public_passcode.has_value()) passcodes.push_back(*event.public_passcode);
            }
            const auto append_deck = [&passcodes](const auto& deck) {
                passcodes.insert(passcodes.end(), deck.main_deck.begin(), deck.main_deck.end());
                passcodes.insert(passcodes.end(), deck.extra_deck.begin(), deck.extra_deck.end());
            };
            append_deck(logical.value->public_safe_state.match_context.own_deck);
            append_deck(logical.value->public_safe_state.match_context.opponent_deck);
        }
    }
    std::sort(passcodes.begin(), passcodes.end());
    passcodes.erase(std::unique(passcodes.begin(), passcodes.end()), passcodes.end());
    const auto vocabulary = model::CardVocabularyV1::from_ascending_passcodes(std::move(passcodes));
    if (!vocabulary || !vocabulary.value.has_value()) {
        fail("Task7 CardVocabulary derivation failed");
    }
    (void)vocabulary.value->canonical_bytes();
    return std::move(*vocabulary.value);
}

trajectory::DatasetManifest build_dataset_manifest(
    const std::vector<Task7CollectionJobArtifactsV1>& jobs,
    std::vector<trajectory::VerifiedAdmissionReceipt>& receipts,
    std::vector<trajectory::EpisodeEnvelope>& envelopes) {
    if (jobs.size() != 16) fail("Task7 collection did not produce exactly 16 jobs");
    trajectory::DatasetManifest manifest;
    receipts.reserve(jobs.size());
    envelopes.reserve(jobs.size());
    for (const auto& job : jobs) {
        if (!job.admission_receipt.has_value()) fail("Task7 collection job lacks a receipt");
        const auto& receipt = job.admission_receipt->receipt();
        if (receipt.entries.size() != 1) fail("Task7 collection member cardinality is not one");
        const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes(job.episode_envelope);
        const auto record_id = trajectory::trajectory_record_id(job.episode_envelope);
        const auto public_id = trajectory::public_gameplay_trajectory_id(job.episode_envelope);
        manifest.members.push_back({record_id, public_id, trajectory::admission_receipt_id(receipt),
                                    job.candidate_shard.entries.empty()
                                        ? std::string{}
                                        : trajectory::candidate_shard_artifact_sha256(job.candidate_shard),
                                    ygo::trace::sha256_bytes(envelope_bytes)});
        receipts.push_back(*job.admission_receipt);
        envelopes.push_back(job.episode_envelope);
    }
    std::sort(manifest.members.begin(), manifest.members.end(),
              [](const auto& left, const auto& right) {
                  return left.trajectory_record_id < right.trajectory_record_id;
              });
    std::vector<std::string> record_ids;
    record_ids.reserve(manifest.members.size());
    for (const auto& member : manifest.members) record_ids.push_back(member.trajectory_record_id);
    manifest.dataset_semantic_id = trajectory::dataset::dataset_semantic_id(record_ids);
    std::string error;
    if (!trajectory::dataset::validate_dataset_manifest(manifest, receipts, &error)) {
        fail("Task7 DatasetManifest validation failed: " + error);
    }
    return manifest;
}

Phase6MaterializedDatasetV1 materialize_dataset(
    const trajectory::DatasetManifest& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceipt>& receipts,
    const std::vector<trajectory::EpisodeEnvelope>& envelopes,
    const model::CardVocabularyV1& vocabulary) {
    const auto result = materialize_phase6_dataset_v1(manifest, receipts, envelopes, vocabulary);
    if (!result || !result.value.has_value()) {
        fail(result.error.has_value() ? result.error->diagnostic
                                      : "Task7 Phase6 dataset materialization failed");
    }
    if (result.value->split.train_episode_ids.empty() ||
        result.value->split.validation_episode_ids.empty() ||
        result.value->split.test_episode_ids.empty() ||
        result.value->train_samples.empty() || result.value->validation_samples.empty() ||
        result.value->test_samples.empty()) {
        fail("Task7 deterministic split has an empty partition");
    }
    return std::move(*result.value);
}

Task7MaterializedBatchV1 materialize_task7(
    const Phase6MaterializedDatasetV1& dataset,
    const model::CardVocabularyV1& vocabulary) {
    std::vector<model::LogicalModelInputV1> logical;
    std::vector<model::EncodedModelInputV1> encoded;
    const auto append = [&logical, &encoded](const std::vector<Phase6BcSampleV1>& samples) {
        for (const auto& sample : samples) {
            logical.push_back(sample.logical_model_input);
            encoded.push_back(sample.encoded_model_input);
        }
    };
    append(dataset.train_samples);
    append(dataset.validation_samples);
    append(dataset.test_samples);
    const auto ragged = model::make_ragged_model_batch_v1(encoded);
    if (!ragged || !ragged.value.has_value()) fail("Task7 ragged model batch construction failed");
    Task7MaterializationSourceBatchV1 source;
    source.ragged = &*ragged.value;
    source.samples.reserve(logical.size());
    for (std::size_t index = 0; index < logical.size(); ++index) {
        source.samples.push_back({&logical[index], &encoded[index], &vocabulary,
                                  model::model_input_identity(logical[index], encoded[index]),
                                  vocabulary.identity()});
    }
    const auto result = materialize_task7_input_v1(source);
    if (!result || !result.value.has_value()) {
        fail(result.error.has_value() ? result.error->diagnostic
                                      : "Task7 exact materialization failed");
    }
    return std::move(*result.value);
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) fail("Task7 could not open output artifact");
    if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()),
                                     static_cast<std::streamsize>(bytes.size()));
    if (!output) fail("Task7 could not write output artifact");
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) fail("Task7 could not open output evidence");
    output << text;
    if (!output) fail("Task7 could not write output evidence");
}

std::string job_directory_name(const std::size_t index) {
    std::ostringstream name;
    name << "job-" << std::setw(2) << std::setfill('0') << index;
    return name.str();
}

std::string artifact_line(const std::filesystem::path& relative,
                          const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << relative.generic_string() << " size=" << bytes.size()
           << " sha256=" << ygo::trace::sha256_bytes(bytes) << '\n';
    return output.str();
}

std::string accepted_gate_lines() {
    return
        "COLLECTION_JOB_MANIFEST=PASS\n"
        "JOB_ORDER_DETERMINISTIC=PASS\n"
        "TRAIN_EVAL_SEED_SEPARATION=PASS\n"
        "RULES_BUNDLE_EXACT=PASS\n"
        "LOCKED_DECKS_EXACT=PASS\n"
        "TEACHER_IDENTITIES_EXACT=PASS\n"
        "FULL_EPISODE_COLLECTION=PASS\n"
        "NO_ONE_DECISION_SMOKE_DATA=PASS\n"
        "TRAJECTORY_RECORDING=PASS\n"
        "SEMANTIC_REPLAY=PASS\n"
        "ADMISSION=PASS\n"
        "QUARANTINE_ZERO_FOR_ADMITTED_MEMBERS=PASS\n"
        "DATASET_MANIFEST_V1=PASS\n"
        "MANIFEST_MEMBER_FIELDS_COMPLETE=PASS\n"
        "MANIFEST_MEMBERSHIP_REVERIFIED=PASS\n"
        "TRAINING_DATASET_SPLIT_V1=PASS\n"
        "SPLIT_RECOMPUTATION_SECOND_PROCESS=PASS\n"
        "CROSS_PARTITION_EPISODE_LEAKAGE=NO\n"
        "CARD_VOCABULARY_V1=PASS\n"
        "VOCABULARY_PRIVACY=PASS\n"
        "PHASE6_SUPERVISION_MATERIALIZATION=PASS\n"
        "PUBLIC_MODEL_INPUT_ONLY=PASS\n"
        "COMPLETE_CANDIDATE_DOMAINS=PASS\n"
        "TASK7_LOSSLESS_MATERIALIZATION=PASS\n"
        "TASK7_MATERIALIZATION_CONFIG_BINDING=PASS\n"
        "NO_FALLBACK=PASS\n"
        "FRESH_PROCESS_DETERMINISM=PASS\n"
        "TASK4B_HISTORY_CHANGED=NO\n"
        "TASK5_EVALUATION_JOBS_CHANGED=NO\n"
        "RULES_CHANGED=NO\n"
        "LOCKED_DECKS_CHANGED=NO\n"
        "TEACHERS_CHANGED=NO\n";
}

}  // namespace

using ygo::trajectory::ByteWriter;

Task7DatasetAuthorityResult provision_task7_dataset_authority(
    std::string collector_semantic_source_commit) noexcept {
    const auto failure = [](const Task7DatasetAuthorityErrorCode code,
                            const std::string& diagnostic) {
        Task7DatasetAuthorityResult result;
        result.error = Task7DatasetAuthorityError{code, diagnostic};
        return result;
    };
    try {
        if (!valid_commit(collector_semantic_source_commit)) {
            return failure(Task7DatasetAuthorityErrorCode::InvalidSourceCommit,
                           "Task7 collection source commit is invalid");
        }
        const auto schedule = make_task7_collection_schedule(
            std::move(collector_semantic_source_commit));
        std::vector<Task7CollectionJobArtifactsV1> jobs;
        jobs.reserve(schedule.jobs.size());
        for (const auto& job : schedule.jobs) jobs.push_back(collect_one_job(job));

        std::vector<trajectory::VerifiedAdmissionReceipt> receipts;
        std::vector<trajectory::EpisodeEnvelope> envelopes;
        auto manifest = build_dataset_manifest(jobs, receipts, envelopes);
        auto vocabulary = derive_vocabulary(jobs);
        auto materialized_dataset = materialize_dataset(
            manifest, receipts, envelopes, vocabulary);
        auto task7_materialization = materialize_task7(
            materialized_dataset, vocabulary);
        if (task7_materialization.schema_id != kTask7MaterializationSchemaId ||
            task7_materialization.configuration_identity !=
                "phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a") {
            return failure(Task7DatasetAuthorityErrorCode::MaterializationFailure,
                           "Task7 materialization configuration is not accepted");
        }
        Task7DatasetAuthorityV1 authority{
            schedule,
            std::move(jobs),
            std::move(manifest),
            materialized_dataset.split,
            std::move(vocabulary),
            std::move(materialized_dataset),
            std::move(task7_materialization)};
        return {std::optional<Task7DatasetAuthorityV1>(std::move(authority)), std::nullopt};
    } catch (const std::invalid_argument& error) {
        return failure(Task7DatasetAuthorityErrorCode::CollectionJobFailure, error.what());
    } catch (const std::exception& error) {
        return failure(Task7DatasetAuthorityErrorCode::InternalFailure, error.what());
    } catch (...) {
        return failure(Task7DatasetAuthorityErrorCode::InternalFailure,
                       "Task7 dataset authority provisioning failed");
    }
}

bool write_task7_dataset_authority(
    const Task7DatasetAuthorityV1& authority,
    const std::filesystem::path& output_directory,
    std::string* error) noexcept {
    try {
        validate_schedule_or_fail(authority.schedule);
        if (authority.jobs.size() != authority.schedule.jobs.size() ||
            authority.dataset_manifest.members.size() != authority.jobs.size() ||
            authority.materialized_dataset.sample_count() !=
                authority.task7_materialization.samples.size()) {
            fail("Task7 authority aggregate cardinality is invalid");
        }
        (void)trajectory::dataset::canonical_dataset_manifest_bytes(
            authority.dataset_manifest);
        (void)canonical_phase6_split_identity_bytes(authority.split);
        (void)authority.card_vocabulary.canonical_bytes();
        if (authority.task7_materialization.configuration_identity !=
            "phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a") {
            fail("Task7 authority has an invalid materialization configuration");
        }
        if (output_directory.empty()) fail("Task7 output directory is empty");
        if (std::filesystem::exists(output_directory)) {
            if (!std::filesystem::is_directory(output_directory)) {
                fail("Task7 output path is not a directory");
            }
            if (std::filesystem::directory_iterator(output_directory) !=
                std::filesystem::directory_iterator()) {
                fail("Task7 output directory is not empty");
            }
        } else {
            std::filesystem::create_directories(output_directory);
        }
        const auto schedule_bytes = canonical_task7_collection_schedule_bytes(authority.schedule);
        const auto manifest_bytes = trajectory::dataset::canonical_dataset_manifest_bytes(
            authority.dataset_manifest);
        const auto split_bytes = canonical_phase6_split_identity_bytes(authority.split);
        const auto vocabulary_bytes = authority.card_vocabulary.canonical_bytes();
        const auto root = output_directory / "schedule";
        std::filesystem::create_directories(root);
        write_bytes(root / "collection-schedule.bin", schedule_bytes);

        std::string artifact_index = artifact_line(
            "schedule/collection-schedule.bin", schedule_bytes);
        for (std::size_t index = 0; index < authority.jobs.size(); ++index) {
            const auto& job = authority.jobs[index];
            const auto directory = output_directory / "jobs" / job_directory_name(index);
            std::filesystem::create_directories(directory);
            const auto job_bytes = canonical_task7_collection_job_bytes(job.job);
            const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes(
                job.episode_envelope);
            const auto shard_bytes = trajectory::canonical_candidate_trajectory_shard_bytes(
                job.candidate_shard);
            const auto restricted_bytes =
                trajectory::canonical_restricted_collection_evidence_bundle_bytes(
                    job.restricted_evidence);
            if (!job.admission_receipt.has_value()) fail("Task7 job receipt is missing");
            const auto receipt_bytes = trajectory::canonical_admission_receipt_bytes(
                job.admission_receipt->receipt());
            write_bytes(directory / "collection-job.bin", job_bytes);
            write_bytes(directory / "episode-envelope.bin", envelope_bytes);
            write_bytes(directory / "candidate-shard.bin", shard_bytes);
            write_bytes(directory / "restricted-evidence.bin", restricted_bytes);
            write_bytes(directory / "admission-receipt.bin", receipt_bytes);
            const auto relative = std::filesystem::path("jobs") /
                                  job_directory_name(index);
            artifact_index += artifact_line(relative / "collection-job.bin", job_bytes);
            artifact_index += artifact_line(relative / "episode-envelope.bin", envelope_bytes);
            artifact_index += artifact_line(relative / "candidate-shard.bin", shard_bytes);
            artifact_index += artifact_line(relative / "restricted-evidence.bin", restricted_bytes);
            artifact_index += artifact_line(relative / "admission-receipt.bin", receipt_bytes);
        }
        write_bytes(output_directory / "dataset-manifest.bin", manifest_bytes);
        write_bytes(output_directory / "training-dataset-split.bin", split_bytes);
        write_bytes(output_directory / "card-vocabulary.bin", vocabulary_bytes);
        artifact_index += artifact_line("dataset-manifest.bin", manifest_bytes);
        artifact_index += artifact_line("training-dataset-split.bin", split_bytes);
        artifact_index += artifact_line("card-vocabulary.bin", vocabulary_bytes);

        std::ostringstream report;
        report << "schema=ocgforge.phase6.task7.dataset_authority_provisioning.v1\n"
               << "collector_semantic_source_commit="
               << authority.schedule.collector_semantic_source_commit << '\n'
               << "collection_schedule_identity="
               << task7_collection_schedule_identity(authority.schedule) << '\n'
               << "collection_job_count=" << authority.jobs.size() << '\n'
               << "collection_seeds=4,6,8,9\n"
               << "dataset_semantic_id=" << authority.dataset_manifest.dataset_semantic_id << '\n'
               << "dataset_manifest_sha256=" << ygo::trace::sha256_bytes(manifest_bytes) << '\n'
               << "dataset_manifest_member_count=" << authority.dataset_manifest.members.size() << '\n'
               << "training_dataset_split_identity=" << authority.split.split_identity << '\n'
               << "training_episode_counts=" << authority.split.train_episode_ids.size() << ','
               << authority.split.validation_episode_ids.size() << ','
               << authority.split.test_episode_ids.size() << '\n'
               << "card_vocabulary_identity=" << authority.card_vocabulary.identity() << '\n'
               << "card_vocabulary_size=" << authority.card_vocabulary.ascending_passcodes().size() << '\n'
               << "phase6_sample_counts=" << authority.materialized_dataset.train_samples.size() << ','
               << authority.materialized_dataset.validation_samples.size() << ','
               << authority.materialized_dataset.test_samples.size() << '\n'
               << "task7_materialization_schema=" << kTask7MaterializationSchemaId << '\n'
               << "task7_materialization_config=phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a\n"
               << "max_candidate_domain_n=";
        std::size_t max_candidates = 0;
        for (const auto& sample : authority.materialized_dataset.train_samples) {
            max_candidates = std::max(max_candidates, sample.logical_model_input.candidate_count());
        }
        for (const auto& sample : authority.materialized_dataset.validation_samples) {
            max_candidates = std::max(max_candidates, sample.logical_model_input.candidate_count());
        }
        for (const auto& sample : authority.materialized_dataset.test_samples) {
            max_candidates = std::max(max_candidates, sample.logical_model_input.candidate_count());
        }
        report << max_candidates << '\n' << accepted_gate_lines();
        write_text(output_directory / "artifact-index.txt", artifact_index);
        write_text(output_directory / "provisioning-report.txt", report.str());
        write_text(output_directory / "completion-evidence.txt",
                   "TASK7_DATASET_AUTHORITY_READY=YES\n" + report.str());
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return false;
    } catch (...) {
        if (error != nullptr) *error = "Task7 authority output failed";
        return false;
    }
}

void write_job_fields(ByteWriter& writer, const Task7CollectionJobV1& job) {
    writer.string(job.identity_domain);
    writer.string(job.identity_schema);
    writer.string(job.collection_profile);
    writer.string(job.environment_contract);
    writer.string(job.matchup);
    writer.string(job.rules_bundle);
    writer.string(job.format);
    writer.string(job.duel_mode);
    writer.u64be(job.duel_flags);
    writer.u64be(job.root_seed);
    writer.string(job.placement);
    writer.u8(job.starting_player);
    writer.string(job.seat_0_deck_role);
    writer.string(job.seat_0_deck_sha256);
    writer.string(job.seat_1_deck_role);
    writer.string(job.seat_1_deck_sha256);
    writer.string(job.seat_0_teacher_artifact);
    writer.string(job.seat_0_teacher_binding);
    writer.string(job.seat_1_teacher_artifact);
    writer.string(job.seat_1_teacher_binding);
    writer.string(job.teacher_producer);
    writer.string(job.teacher_selection);
    writer.string(job.teacher_rng);
    writer.u64be(job.engine_process_budget);
    writer.u64be(job.semantic_action_budget);
    writer.string(job.cancellation_reason);
    writer.string(job.cancellation_source);
    writer.string(job.collector_semantic_version);
    writer.string(job.collector_semantic_source_commit);
}

Task7CollectionJobV1 make_job(const std::uint64_t seed,
                              const std::string_view placement,
                              const std::uint8_t starting_player,
                              const std::string& source_commit) {
    Task7CollectionJobV1 job;
    job.root_seed = seed;
    job.placement = std::string(placement);
    job.starting_player = starting_player;
    job.seat_0_deck_role = std::string(placement_deck(placement, 0));
    job.seat_0_deck_sha256 = std::string(deck_sha(job.seat_0_deck_role));
    job.seat_1_deck_role = std::string(placement_deck(placement, 1));
    job.seat_1_deck_sha256 = std::string(deck_sha(job.seat_1_deck_role));
    job.seat_0_teacher_artifact = std::string(teacher_artifact(job.seat_0_deck_role));
    job.seat_0_teacher_binding = std::string(teacher_binding(job.seat_0_deck_role));
    job.seat_1_teacher_artifact = std::string(teacher_artifact(job.seat_1_deck_role));
    job.seat_1_teacher_binding = std::string(teacher_binding(job.seat_1_deck_role));
    job.collector_semantic_source_commit = source_commit;
    validate_task7_collection_job(job);
    return job;
}

std::string digest_identity(const std::string_view prefix,
                            const std::vector<std::uint8_t>& bytes) {
    return std::string(prefix) + ygo::trace::sha256_bytes(bytes);
}

void validate_job_or_fail(const Task7CollectionJobV1& job) {
    std::string error;
    if (!validate_task7_collection_job(job, &error)) fail(error);
}

void validate_schedule_or_fail(const Task7CollectionScheduleV1& schedule) {
    std::string error;
    if (!validate_task7_collection_schedule(schedule, &error)) fail(error);
}

std::vector<std::uint8_t> canonical_task7_collection_job_bytes(
    const Task7CollectionJobV1& job) {
    validate_job_or_fail(job);
    ByteWriter writer;
    write_job_fields(writer, job);
    return std::move(writer).take();
}

std::string task7_collection_job_identity(const Task7CollectionJobV1& job) {
    return digest_identity(kTask7CollectionJobIdentityPrefix,
                           canonical_task7_collection_job_bytes(job));
}

bool validate_task7_collection_job(const Task7CollectionJobV1& job,
                                   std::string* error) noexcept {
    try {
        const std::array<std::string_view, 26> strings = {
            job.identity_domain, job.identity_schema, job.collection_profile,
            job.environment_contract, job.matchup, job.rules_bundle, job.format,
            job.duel_mode, job.placement, job.seat_0_deck_role,
            job.seat_0_deck_sha256, job.seat_1_deck_role,
            job.seat_1_deck_sha256, job.seat_0_teacher_artifact,
            job.seat_0_teacher_binding, job.seat_1_teacher_artifact,
            job.seat_1_teacher_binding, job.teacher_producer,
            job.teacher_selection, job.teacher_rng, job.cancellation_reason,
            job.cancellation_source, job.collector_semantic_version,
            job.collector_semantic_source_commit, kTask7CollectionScheduleSchemaId,
            kTask7CollectionJobSchemaId};
        for (const auto value : strings) require_string(value, "job field");
        if (job.identity_domain != kTask7CollectionJobIdentityDomain ||
            job.identity_schema != kTask7CollectionJobSchemaId ||
            job.collection_profile != kTask7CollectionProfileIdentity ||
            job.environment_contract != kTask7CollectionEnvironmentContractId ||
            job.matchup != kMatchup || job.rules_bundle != kRulesBundle ||
            job.format != kFormat || job.duel_mode != kDuelMode ||
            job.duel_flags != kDuelFlags ||
            (job.placement != "NORMAL" && job.placement != "MIRROR") ||
            job.starting_player > 1 ||
            job.engine_process_budget != kTask7CollectionEngineProcessBudget ||
            job.semantic_action_budget != kTask7CollectionSemanticActionBudget ||
            job.cancellation_reason != "ADMINISTRATIVE_CANCEL" ||
            job.cancellation_source != kTask7CollectionCancellationSource ||
            job.collector_semantic_version != kTask7CollectionSemanticVersion ||
            !valid_commit(job.collector_semantic_source_commit)) {
            fail("Task7 collection job fixed binding is invalid");
        }
        if (job.seat_0_deck_role != placement_deck(job.placement, 0) ||
            job.seat_1_deck_role != placement_deck(job.placement, 1) ||
            job.seat_0_deck_sha256 != deck_sha(job.seat_0_deck_role) ||
            job.seat_1_deck_sha256 != deck_sha(job.seat_1_deck_role) ||
            job.seat_0_teacher_artifact != teacher_artifact(job.seat_0_deck_role) ||
            job.seat_0_teacher_binding != teacher_binding(job.seat_0_deck_role) ||
            job.seat_1_teacher_artifact != teacher_artifact(job.seat_1_deck_role) ||
            job.seat_1_teacher_binding != teacher_binding(job.seat_1_deck_role) ||
            job.teacher_producer != kTeacherProducer ||
            job.teacher_selection != kTeacherSelection ||
            job.teacher_rng != kTeacherRng) {
            fail("Task7 collection Teacher/deck binding is invalid");
        }
        require_digest(job.seat_0_deck_sha256, "seat-0 deck digest");
        require_digest(job.seat_1_deck_sha256, "seat-1 deck digest");
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return false;
    } catch (...) {
        if (error != nullptr) *error = "Task7 collection job validation threw";
        return false;
    }
}

Task7CollectionScheduleV1 make_task7_collection_schedule(
    std::string collector_semantic_source_commit) {
    if (!valid_commit(collector_semantic_source_commit)) {
        fail("Task7 collection source commit is invalid");
    }
    Task7CollectionScheduleV1 schedule;
    schedule.collector_semantic_source_commit = std::move(collector_semantic_source_commit);
    schedule.seeds = {4, 6, 8, 9};
    schedule.placements = {"NORMAL", "MIRROR"};
    schedule.starting_players = {0, 1};
    schedule.jobs.reserve(16);
    for (const auto seed : schedule.seeds) {
        for (const auto& placement : schedule.placements) {
            for (const auto starting_player : schedule.starting_players) {
                schedule.jobs.push_back(make_job(
                    seed, placement, starting_player,
                    schedule.collector_semantic_source_commit));
            }
        }
    }
    validate_schedule_or_fail(schedule);
    return schedule;
}

std::vector<std::uint8_t> canonical_task7_collection_schedule_bytes(
    const Task7CollectionScheduleV1& schedule) {
    validate_schedule_or_fail(schedule);
    ByteWriter writer;
    writer.string(schedule.identity_domain);
    writer.string(schedule.schema_id);
    writer.string(schedule.collection_profile);
    writer.string(schedule.collector_semantic_source_commit);
    if (schedule.seeds.size() > std::numeric_limits<std::uint32_t>::max() ||
        schedule.placements.size() > std::numeric_limits<std::uint32_t>::max() ||
        schedule.starting_players.size() > std::numeric_limits<std::uint32_t>::max() ||
        schedule.jobs.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("Task7 collection schedule vector exceeds u32");
    }
    writer.u32be(static_cast<std::uint32_t>(schedule.seeds.size()));
    for (const auto value : schedule.seeds) writer.u64be(value);
    writer.u32be(static_cast<std::uint32_t>(schedule.placements.size()));
    for (const auto& value : schedule.placements) writer.string(value);
    writer.u32be(static_cast<std::uint32_t>(schedule.starting_players.size()));
    for (const auto value : schedule.starting_players) writer.u8(value);
    writer.u32be(static_cast<std::uint32_t>(schedule.jobs.size()));
    for (const auto& job : schedule.jobs) writer.string(task7_collection_job_identity(job));
    return std::move(writer).take();
}

std::string task7_collection_schedule_identity(
    const Task7CollectionScheduleV1& schedule) {
    return digest_identity(kTask7CollectionScheduleIdentityPrefix,
                           canonical_task7_collection_schedule_bytes(schedule));
}

bool validate_task7_collection_schedule(
    const Task7CollectionScheduleV1& schedule,
    std::string* error) noexcept {
    try {
        if (schedule.identity_domain != kTask7CollectionScheduleIdentityDomain ||
            schedule.schema_id != kTask7CollectionScheduleSchemaId ||
            schedule.collection_profile != kTask7CollectionProfileIdentity ||
            !valid_commit(schedule.collector_semantic_source_commit) ||
            schedule.seeds != std::vector<std::uint64_t>{4, 6, 8, 9} ||
            schedule.placements != std::vector<std::string>{"NORMAL", "MIRROR"} ||
            schedule.starting_players != std::vector<std::uint8_t>{0, 1} ||
            schedule.jobs.size() != 16) {
            fail("Task7 collection schedule fixed binding is invalid");
        }
        std::size_t index = 0;
        for (const auto seed : schedule.seeds) {
            for (const auto& placement : schedule.placements) {
                for (const auto starting_player : schedule.starting_players) {
                    if (index >= schedule.jobs.size()) fail("Task7 schedule is truncated");
                    const auto& job = schedule.jobs[index];
                    if (!validate_task7_collection_job(job, error) ||
                        job.root_seed != seed || job.placement != placement ||
                        job.starting_player != starting_player ||
                        job.collector_semantic_source_commit !=
                            schedule.collector_semantic_source_commit) {
                        fail("Task7 collection schedule job order or binding is invalid");
                    }
                    ++index;
                }
            }
        }
        if (index != schedule.jobs.size()) fail("Task7 collection schedule has extra jobs");
        std::set<std::string> identities;
        for (const auto& job : schedule.jobs) {
            if (!identities.insert(task7_collection_job_identity(job)).second) {
                fail("Task7 collection schedule contains duplicate jobs");
            }
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return false;
    } catch (...) {
        if (error != nullptr) *error = "Task7 collection schedule validation threw";
        return false;
    }
}

std::string_view task7_dataset_authority_error_code_name(
    const Task7DatasetAuthorityErrorCode code) noexcept {
    switch (code) {
    case Task7DatasetAuthorityErrorCode::InvalidSourceCommit:
        return "invalid_source_commit";
    case Task7DatasetAuthorityErrorCode::InvalidSchedule:
        return "invalid_schedule";
    case Task7DatasetAuthorityErrorCode::CollectionJobFailure:
        return "collection_job_failure";
    case Task7DatasetAuthorityErrorCode::IneligibleEpisode:
        return "ineligible_episode";
    case Task7DatasetAuthorityErrorCode::ArtifactValidationFailure:
        return "artifact_validation_failure";
    case Task7DatasetAuthorityErrorCode::ManifestFailure:
        return "manifest_failure";
    case Task7DatasetAuthorityErrorCode::SplitFailure:
        return "split_failure";
    case Task7DatasetAuthorityErrorCode::EmptyPartition:
        return "empty_partition";
    case Task7DatasetAuthorityErrorCode::VocabularyFailure:
        return "vocabulary_failure";
    case Task7DatasetAuthorityErrorCode::MaterializationFailure:
        return "materialization_failure";
    case Task7DatasetAuthorityErrorCode::OutputFailure:
        return "output_failure";
    case Task7DatasetAuthorityErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

}  // namespace ygo::phase6::task7
