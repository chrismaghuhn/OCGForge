#include "task4_numeric_projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/phase6/supervision_dataset.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/types.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::SeatAssignment;
using ygo::model::CardVocabularyV1;
using ygo::phase6::Phase6BcCandidateInputV1;
using ygo::phase6::Phase6BcCandidateRepresentationV1;
using ygo::phase6::Phase6BcReferenceScorerV1;
using ygo::phase6::Phase6BcStateInputV1;
using ygo::phase6::Phase6BcStateRepresentationV1;
using ygo::phase6::Phase6BcCallbackResult;
using ygo::policy::TeacherRunnerConfig;
using ygo::policy::TeacherRunner;
using ygo::policy::PolicyRunnerDisposition;
using ygo::trajectory::DatasetManifest;
using ygo::trajectory::DatasetManifestMember;
using ygo::trajectory::EpisodeEnvelope;
using ygo::trajectory::ParticipantPolicyAssignment;
using ygo::trajectory::PolicyRole;
using ygo::trajectory::VerifiedAdmissionReceipt;

constexpr std::string_view kCorpusSchemaId =
    "ocgforge.phase6.task4.smoke_corpus.v1";
constexpr std::string_view kCorpusArtifactPrefix = "phase6_corpus_artifact.v1.";
constexpr std::string_view kOrderedDomainIdentityDomain =
    "ocgforge.phase6.ordered_candidate_domain.v1";
constexpr std::string_view kOrderedDomainIdentityPrefix =
    "phase6_ordered_candidate_domain.v1.";

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

std::uint32_t checked_u32(const std::size_t value, const std::string_view field) {
    require(value <= std::numeric_limits<std::uint32_t>::max(), field);
    return static_cast<std::uint32_t>(value);
}

class Writer final {
public:
    void u8(const std::uint8_t value) { bytes_.push_back(value); }

    void u32(const std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 24));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void u64(const std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void string(const std::string_view value) {
        u32(checked_u32(value.size(), "corpus string exceeds u32 length"));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void f32(const float value) {
        require(std::isfinite(value), "non-finite numeric projection value");
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }

    void raw(const std::vector<std::uint8_t>& bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    void strings(const std::vector<std::string>& values) {
        u32(checked_u32(values.size(), "corpus string vector exceeds u32 count"));
        for (const auto& value : values) string(value);
    }

    template <std::size_t Width>
    void rows(const std::vector<std::array<float, Width>>& values) {
        u32(checked_u32(values.size(), "corpus numeric rows exceed u32 count"));
        for (const auto& row : values) {
            for (const auto value : row) f32(value);
        }
    }

    std::vector<std::uint8_t> take() && noexcept { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

struct Job final {
    std::uint64_t root_seed;
    std::uint8_t starting_player;
};

struct AdmittedJob final {
    EpisodeEnvelope envelope;
    VerifiedAdmissionReceipt receipt;
    DatasetManifest manifest;
};

struct CapturedInput final {
    std::optional<Phase6BcStateInputV1> state;
    std::vector<Phase6BcCandidateInputV1> candidates;
};

TeacherRunnerConfig teacher_config(const Job job) {
    const auto swordsoul = ygo::teacher::make_swordsoul_tenyi_profile();
    const auto salamangreat = ygo::teacher::make_salamangreat_profile();
    const auto swordsoul_artifact = ygo::policy::make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = ygo::policy::make_teacher_policy_artifact(salamangreat);

    TeacherRunnerConfig config;
    config.environment_config = CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = job.root_seed;
    config.episode_spec.seat_assignment = SeatAssignment::Normal;
    config.episode_spec.starting_player = job.starting_player;
    config.run_control.engine_process_budget = 20000;
    config.run_control.semantic_action_budget = 1;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "phase6-task4-corpus-probe";

    const std::array<PolicyRole, 2> roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    const auto assignments = ygo::policy::make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config.environment_config,
        config.episode_spec.seat_assignment, config.episode_spec.starting_player, roles);
    config.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(config.policy_provenance.policy_artifacts.begin(),
              config.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    config.policy_provenance.participant_assignments = assignments;

    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto found = std::find_if(
            assignments.begin(), assignments.end(),
            [player](const auto& assignment) { return assignment.player == player; });
        require(found != assignments.end(), "Teacher assignment is missing");
        const auto& profile = found->deck_role == ygo::trajectory::DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = found->deck_role == ygo::trajectory::DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        const auto binding = ygo::policy::make_teacher_policy_binding(profile);
        auto session = ygo::policy::create_teacher_policy_session(
            profile, binding, artifact, *found);
        require(session && session.value.has_value(), "Teacher session creation failed");
        config.sessions[player] = std::move(*session.value);
    }
    return config;
}

AdmittedJob run_admitted_job(const Job job) {
    auto created = TeacherRunner::create(teacher_config(job));
    require(created && created.value.has_value(), "Teacher runner creation failed");
    auto result = created.value->run();
    require(result.disposition == PolicyRunnerDisposition::CleanAdmitted,
            "fixed Teacher job was not cleanly admitted");
    require(result.envelope.has_value() && result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "admitted Teacher job lacks required membership values");
    return {std::move(*result.envelope), std::move(*result.admission_receipt),
            std::move(*result.dataset_manifest)};
}

CardVocabularyV1 vocabulary_for(const std::vector<AdmittedJob>& jobs) {
    std::vector<std::uint32_t> passcodes;
    for (const auto& job : jobs) {
        for (const auto& record : job.envelope.records) {
            const auto logical = ygo::model::project_logical_model_input_v1(
                record.frame.public_observation, record.frame.request.candidates);
            require(logical && logical.value.has_value(),
                    "logical projection failed while collecting vocabulary");
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
    const auto result = CardVocabularyV1::from_ascending_passcodes(std::move(passcodes));
    require(result && result.value.has_value(), "combined vocabulary construction failed");
    return std::move(*result.value);
}

DatasetManifest combined_manifest(const std::vector<AdmittedJob>& jobs) {
    DatasetManifest manifest;
    for (const auto& job : jobs) {
        manifest.members.insert(manifest.members.end(), job.manifest.members.begin(),
                                job.manifest.members.end());
    }
    std::sort(manifest.members.begin(), manifest.members.end(),
              [](const DatasetManifestMember& left, const DatasetManifestMember& right) {
                  return left.trajectory_record_id < right.trajectory_record_id;
              });
    std::vector<std::string> record_ids;
    record_ids.reserve(manifest.members.size());
    for (std::size_t index = 0; index < manifest.members.size(); ++index) {
        if (index != 0 && manifest.members[index - 1].trajectory_record_id ==
                              manifest.members[index].trajectory_record_id) {
            throw std::runtime_error("fixed corpus contains duplicate trajectory record");
        }
        record_ids.push_back(manifest.members[index].trajectory_record_id);
    }
    require(!record_ids.empty(), "fixed corpus has no manifest members");
    manifest.dataset_semantic_id = ygo::trajectory::dataset::dataset_semantic_id(record_ids);
    return manifest;
}

std::string ordered_domain_identity(const std::vector<std::string>& routing_keys) {
    require(!routing_keys.empty(), "ordered candidate domain is empty");
    if (std::adjacent_find(routing_keys.begin(), routing_keys.end()) != routing_keys.end()) {
        throw std::runtime_error("ordered candidate domain contains duplicate keys");
    }
    Writer writer;
    writer.string(kOrderedDomainIdentityDomain);
    writer.u32(checked_u32(routing_keys.size(), "ordered domain exceeds u32 count"));
    for (const auto& key : routing_keys) writer.string(key);
    return std::string(kOrderedDomainIdentityPrefix) +
           ygo::trace::sha256_bytes(std::move(writer).take());
}

CapturedInput capture_task3_inputs(const ygo::model::EncodedModelInputV1& encoded) {
    CapturedInput capture;
    Phase6BcReferenceScorerV1 scorer;
    scorer.state_encoder = [&capture](const Phase6BcStateInputV1& state) {
        capture.state = state;
        return Phase6BcCallbackResult<Phase6BcStateRepresentationV1>{
            std::optional<Phase6BcStateRepresentationV1>(Phase6BcStateRepresentationV1{}),
            std::nullopt};
    };
    scorer.candidate_encoder = [&capture](const Phase6BcStateRepresentationV1&,
                                           const Phase6BcCandidateInputV1& candidate) {
        capture.candidates.push_back(candidate);
        return Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>{
            std::optional<Phase6BcCandidateRepresentationV1>(Phase6BcCandidateRepresentationV1{}),
            std::nullopt};
    };
    scorer.candidate_scoring_function =
        [](const Phase6BcStateRepresentationV1&, const Phase6BcCandidateRepresentationV1&) {
            return Phase6BcCallbackResult<double>{std::optional<double>(0.0), std::nullopt};
        };
    const auto result = ygo::phase6::score_encoded_model_input_v1(encoded, scorer);
    require(result && result.value.has_value(), "Task-3 input capture failed");
    require(capture.state.has_value() && capture.candidates.size() == encoded.candidate_count(),
            "Task-3 capture changed exact candidate cardinality");
    return capture;
}

void write_sample(Writer& writer,
                  const ygo::phase6::Phase6BcSampleV1& sample,
                  const std::string& partition) {
    const auto capture = capture_task3_inputs(sample.encoded_model_input);
    const auto state_rows = ygo::phase6::task4::project_state_numeric_rows(*capture.state);
    std::vector<ygo::phase6::task4::CandidateNumericRow> candidate_rows;
    candidate_rows.reserve(capture.candidates.size());
    for (const auto& candidate : capture.candidates) {
        candidate_rows.push_back(ygo::phase6::task4::project_candidate_numeric_row(candidate));
    }
    require(candidate_rows.size() == sample.encoded_model_input.candidate_count(),
            "numeric candidate projection changed N");
    require(sample.supervision.candidate_ordinal < candidate_rows.size(),
            "supervision ordinal is outside numeric candidate domain");
    require(sample.supervision.selected_public_action_key ==
                sample.encoded_model_input.routing_keys[sample.supervision.candidate_ordinal],
            "supervision key is not paired with numeric candidate ordinal");

    writer.string(sample.sample_identity);
    writer.string(sample.trajectory_record_id);
    writer.string(sample.episode_semantic_id);
    writer.string(sample.supervision.source_public_semantic_decision_id);
    writer.string(sample.supervision.model_input_identity);
    writer.string(sample.supervision.selected_public_action_key);
    writer.string(partition);
    writer.u32(sample.supervision.candidate_ordinal);
    writer.string(ordered_domain_identity(sample.encoded_model_input.routing_keys));
    writer.rows(state_rows);
    writer.rows(candidate_rows);
    writer.strings(sample.encoded_model_input.routing_keys);
}

std::vector<std::uint8_t> corpus_body(const DatasetManifest& manifest,
                                      const std::string& split_identity,
                                      const CardVocabularyV1& vocabulary,
                                      const ygo::phase6::Phase6MaterializedDatasetV1& dataset) {
    Writer writer;
    writer.string(kCorpusSchemaId);
    writer.string(manifest.dataset_semantic_id);
    writer.string(split_identity);
    writer.string(ygo::phase6::task4::kNumericProjectionContractId);
    writer.string(vocabulary.identity());

    std::vector<std::string> episode_ids;
    for (const auto& id : dataset.split.train_episode_ids) episode_ids.push_back(id);
    for (const auto& id : dataset.split.validation_episode_ids) episode_ids.push_back(id);
    for (const auto& id : dataset.split.test_episode_ids) episode_ids.push_back(id);
    std::sort(episode_ids.begin(), episode_ids.end());
    episode_ids.erase(std::unique(episode_ids.begin(), episode_ids.end()), episode_ids.end());
    writer.strings(episode_ids);

    const auto sample_count = dataset.sample_count();
    writer.u32(checked_u32(sample_count, "corpus sample count exceeds u32"));
    const auto write_partition = [&writer](
                                      const auto& samples,
                                      const std::string_view partition) {
        for (const auto& sample : samples) write_sample(writer, sample, std::string(partition));
    };
    write_partition(dataset.train_samples, "train");
    write_partition(dataset.validation_samples, "validation");
    write_partition(dataset.test_samples, "test");
    return std::move(writer).take();
}

std::string output_path(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--output" || argv[2][0] == '\0') {
        throw std::runtime_error("usage: phase6_task4_corpus_probe --output <path>");
    }
    return argv[2];
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::array<Job, 8> jobs = {{
            {2, 0}, {3, 1}, {5, 0}, {7, 1},
            {11, 0}, {13, 1}, {17, 0}, {19, 1},
        }};
        std::vector<AdmittedJob> admitted;
        admitted.reserve(jobs.size());
        for (const auto job : jobs) admitted.push_back(run_admitted_job(job));

        const auto manifest = combined_manifest(admitted);
        const auto vocabulary = vocabulary_for(admitted);
        std::vector<VerifiedAdmissionReceipt> receipts;
        std::vector<EpisodeEnvelope> envelopes;
        receipts.reserve(admitted.size());
        envelopes.reserve(admitted.size());
        for (auto& job : admitted) {
            receipts.push_back(std::move(job.receipt));
            envelopes.push_back(std::move(job.envelope));
        }
        const auto dataset = ygo::phase6::materialize_phase6_dataset_v1(
            manifest, receipts, envelopes, vocabulary);
        require(dataset && dataset.value.has_value(), "Phase-6 dataset materialization failed");
        require(!dataset.value->train_samples.empty(), "fixed corpus train partition is empty");
        const auto body = corpus_body(manifest, dataset.value->split.split_identity,
                                      vocabulary, *dataset.value);
        const auto artifact_identity = std::string(kCorpusArtifactPrefix) +
                                       ygo::trace::sha256_bytes(body);
        Writer artifact;
        artifact.string(artifact_identity);
        artifact.raw(body);
        const auto bytes = std::move(artifact).take();

        const auto path = output_path(argc, argv);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        require(output.is_open(), "could not open corpus output");
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        require(output.good(), "could not write corpus output");
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
