#include "ygo/phase6/model_input_inspector.hpp"
#include "ygo/phase6/supervision_dataset.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_decision.hpp"
#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/model_batch_layout.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/policy/production.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/types.hpp"

namespace {

using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::environment::PublicEnvironmentObservation;
using ygo::environment::SeatAssignment;
using ygo::model::CardVocabularyV1;
using ygo::model::EncodedModelInputV1;
using ygo::model::LogicalModelInputV1;
using ygo::phase6::Phase6BcSampleV1;
using ygo::phase6::Phase6DataErrorCode;
using ygo::phase6::Phase6DatasetResult;
using ygo::phase6::TrainingDatasetSplitV1;
using ygo::policy::PolicyRunnerConfig;
using ygo::policy::PolicyRunnerDisposition;
using ygo::policy::PolicyRunnerResult;
using ygo::policy::TeacherRunnerConfig;
using ygo::trajectory::CollectionDispositionKind;
using ygo::trajectory::DatasetManifest;
using ygo::trajectory::EpisodeEnvelope;
using ygo::trajectory::FailedClosure;
using ygo::trajectory::ParticipantPolicyAssignment;
using ygo::trajectory::PolicyRole;
using ygo::trajectory::TerminalClosure;
using ygo::trajectory::TransitionClass;
using ygo::trajectory::VerifiedAdmissionReceipt;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto found = std::find_if(
        assignments.begin(), assignments.end(),
        [player](const auto& assignment) { return assignment.player == player; });
    require(found != assignments.end(), "Teacher fixture lacks a participant assignment");
    return *found;
}

TeacherRunnerConfig teacher_config(const std::uint64_t root_seed,
                                  const std::uint64_t semantic_action_budget,
                                  const std::uint8_t starting_player = 0) {
    const auto swordsoul = ygo::teacher::make_swordsoul_tenyi_profile();
    const auto salamangreat = ygo::teacher::make_salamangreat_profile();
    const auto swordsoul_artifact = ygo::policy::make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = ygo::policy::make_teacher_policy_artifact(salamangreat);

    TeacherRunnerConfig config;
    config.environment_config = CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = root_seed;
    config.episode_spec.seat_assignment = SeatAssignment::Normal;
    config.episode_spec.starting_player = starting_player;
    config.run_control.engine_process_budget = 20000;
    config.run_control.semantic_action_budget = semantic_action_budget;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "phase6-task2-test";

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
        const auto& assignment = assignment_for_player(assignments, player);
        const auto& profile = assignment.deck_role == ygo::trajectory::DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == ygo::trajectory::DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        const auto binding = ygo::policy::make_teacher_policy_binding(profile);
        auto session = ygo::policy::create_teacher_policy_session(
            profile, binding, artifact, assignment);
        require(session && session.value.has_value(),
                "Teacher fixture session creation failed");
        config.sessions[player] = std::move(*session.value);
    }
    return config;
}

struct TeacherFixture final {
    EpisodeEnvelope envelope;
    VerifiedAdmissionReceipt receipt;
    DatasetManifest manifest;
    CardVocabularyV1 vocabulary;
};

CardVocabularyV1 vocabulary_for(const EpisodeEnvelope& envelope) {
    std::vector<std::uint32_t> passcodes;
    for (const auto& record : envelope.records) {
        const auto logical = ygo::model::project_logical_model_input_v1(
            record.frame.public_observation, record.frame.request.candidates);
        require(logical && logical.value.has_value(),
                "fixture logical projection failed while building vocabulary");
        for (const auto& entity : logical.value->public_safe_state.entities) {
            if (entity.card.identity_known && entity.card.passcode.has_value()) {
                passcodes.push_back(*entity.card.passcode);
            }
        }
        for (const auto& event : logical.value->public_safe_state.visible_events) {
            if (event.public_passcode.has_value()) {
                passcodes.push_back(*event.public_passcode);
            }
        }
        const auto append_deck = [&passcodes](const auto& deck) {
            passcodes.insert(passcodes.end(), deck.main_deck.begin(), deck.main_deck.end());
            passcodes.insert(passcodes.end(), deck.extra_deck.begin(), deck.extra_deck.end());
        };
        append_deck(logical.value->public_safe_state.match_context.own_deck);
        append_deck(logical.value->public_safe_state.match_context.opponent_deck);
    }
    std::sort(passcodes.begin(), passcodes.end());
    passcodes.erase(std::unique(passcodes.begin(), passcodes.end()), passcodes.end());
    const auto result = CardVocabularyV1::from_ascending_passcodes(std::move(passcodes));
    require(result && result.value.has_value(), "fixture vocabulary construction failed");
    return std::move(*result.value);
}

TeacherFixture make_teacher_fixture(const std::uint64_t root_seed = 2,
                                    const std::uint8_t starting_player = 0) {
    auto created = ygo::policy::TeacherRunner::create(
        teacher_config(root_seed, 1, starting_player));
    require(created && created.value.has_value(), "Teacher fixture construction failed");
    auto result = created.value->run();
    require(result.disposition == PolicyRunnerDisposition::CleanAdmitted,
            "Teacher fixture was not cleanly admitted");
    require(result.envelope.has_value() && result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "Teacher fixture lacks the admitted output chain");
    auto envelope = std::move(*result.envelope);
    auto vocabulary = vocabulary_for(envelope);
    return TeacherFixture{std::move(envelope), std::move(*result.admission_receipt),
                          std::move(*result.dataset_manifest), std::move(vocabulary)};
}

const TeacherFixture& teacher_fixture() {
    static const TeacherFixture fixture = make_teacher_fixture();
    return fixture;
}

const TeacherFixture& teacher_fixture_other_perspective() {
    static const TeacherFixture fixture = make_teacher_fixture(3, 1);
    return fixture;
}

Phase6DatasetResult materialize_teacher_fixture(const TeacherFixture& fixture) {
    std::vector<VerifiedAdmissionReceipt> receipts;
    receipts.push_back(fixture.receipt);
    std::vector<EpisodeEnvelope> envelopes;
    envelopes.push_back(fixture.envelope);
    return ygo::phase6::materialize_phase6_dataset_v1(
        fixture.manifest, receipts, envelopes, fixture.vocabulary);
}

PublicEnvironmentObservation paired_public_observation(const std::string& marker);
std::vector<EnvironmentActionCandidate> candidates(std::size_t count);

struct RandomFixture final {
    EpisodeEnvelope envelope;
    VerifiedAdmissionReceipt receipt;
    DatasetManifest manifest;
};

RandomFixture make_random_fixture() {
    PolicyRunnerConfig config;
    config.environment_config = CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = 7;
    config.episode_spec.seat_assignment = SeatAssignment::Normal;
    config.episode_spec.starting_player = 0;
    config.run_control.engine_process_budget = 512;
    config.run_control.semantic_action_budget = 1;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "phase6-random-test";

    const auto artifact = ygo::policy::make_random_legal_policy_artifact();
    const std::array<PolicyRole, 2> roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    config.policy_provenance.policy_artifacts = {artifact};
    config.policy_provenance.participant_assignments =
        ygo::policy::make_random_legal_participant_assignments(
            artifact, config.environment_config, config.episode_spec.seat_assignment,
            config.episode_spec.starting_player, roles);
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(
            config.policy_provenance.participant_assignments, player);
        auto session = ygo::policy::create_random_legal_policy_session(
            artifact, assignment, player == 0 ? 11 : 22,
            player == 0 ? "phase6-random-0" : "phase6-random-1");
        require(session && session.value.has_value(),
                "RandomLegal fixture session creation failed");
        config.sessions[player] = std::move(*session.value);
    }
    auto created = ygo::policy::PolicyRunner::create(std::move(config));
    require(created && created.value.has_value(), "RandomLegal fixture construction failed");
    auto result = created.value->run();
    require(result.disposition == PolicyRunnerDisposition::CleanAdmitted &&
                result.envelope.has_value() && result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "RandomLegal fixture was not cleanly admitted");
    return RandomFixture{std::move(*result.envelope), std::move(*result.admission_receipt),
                         std::move(*result.dataset_manifest)};
}

const RandomFixture& random_fixture() {
    static const RandomFixture fixture = make_random_fixture();
    return fixture;
}

const Phase6BcSampleV1& first_sample(const ygo::phase6::Phase6MaterializedDatasetV1& dataset) {
    if (!dataset.train_samples.empty()) return dataset.train_samples.front();
    if (!dataset.validation_samples.empty()) return dataset.validation_samples.front();
    require(!dataset.test_samples.empty(), "materialized dataset has no samples");
    return dataset.test_samples.front();
}

template <typename Function>
void for_each_sample(const ygo::phase6::Phase6MaterializedDatasetV1& dataset,
                     Function&& function) {
    for (const auto& sample : dataset.train_samples) function(sample);
    for (const auto& sample : dataset.validation_samples) function(sample);
    for (const auto& sample : dataset.test_samples) function(sample);
}

void test_valid_admitted_materialization_and_exact_domain() {
    const auto& fixture = teacher_fixture();
    const auto& other_fixture = teacher_fixture_other_perspective();
    const auto before = ygo::trajectory::canonical_episode_envelope_bytes(fixture.envelope);
    const auto result = materialize_teacher_fixture(fixture);
    const auto other_result = materialize_teacher_fixture(other_fixture);
    require(result && result.value.has_value(), "valid admitted dataset was rejected");
    require(other_result && other_result.value.has_value(),
            "second perspective admitted dataset was rejected");
    const auto& dataset = *result.value;
    const auto& other_dataset = *other_result.value;
    require(dataset.sample_count() == fixture.envelope.records.size(),
            "dataset materialization lost DecisionRecords");
    require(other_dataset.sample_count() == other_fixture.envelope.records.size(),
            "second perspective materialization lost DecisionRecords");
    require(dataset.split.source_dataset_identity == fixture.manifest.dataset_semantic_id,
            "split did not bind the DatasetManifest semantic identity");

    std::set<std::uint8_t> acting_players;
    for (const auto& record : fixture.envelope.records) acting_players.insert(record.frame.acting_player);
    for (const auto& record : other_fixture.envelope.records) {
        acting_players.insert(record.frame.acting_player);
    }
    require(acting_players == std::set<std::uint8_t>{0, 1},
            "Teacher fixture did not preserve both participant perspectives");

    const auto check_samples = [&](const auto& checked_dataset) {
      for_each_sample(checked_dataset, [&](const auto& sample) {
        require(sample.sample_identity == ygo::phase6::phase6_sample_identity(sample),
                "Phase-6 sample identity did not recompute");
        require(sample.logical_model_input.candidate_count() ==
                    sample.encoded_model_input.candidate_count() &&
                    sample.logical_model_input.candidate_count() > 0,
                "materialized sample changed candidate cardinality");
        require(sample.logical_model_input.candidate_routing.size() ==
                    sample.logical_model_input.candidate_features.size() &&
                    sample.encoded_model_input.routing_keys.size() ==
                        sample.encoded_model_input.candidate_features.size(),
                "materialized sample lost routing rows");
        for (std::size_t index = 0;
             index < sample.logical_model_input.candidate_count(); ++index) {
            require(sample.logical_model_input.candidate_routing[index].public_action_key ==
                        sample.encoded_model_input.routing_keys[index],
                    "logical and encoded candidate order diverged");
        }
        const auto identity_bytes =
            ygo::phase6::canonical_phase6_sample_identity_bytes(sample);
        require(identity_bytes == ygo::phase6::canonical_phase6_sample_identity_bytes(sample),
                "Phase-6 sample identity bytes were not deterministic");
      });
    };
    check_samples(dataset);
    check_samples(other_dataset);
    require(ygo::trajectory::canonical_episode_envelope_bytes(fixture.envelope) == before,
            "materialization mutated the admitted trajectory");
}

void test_continuation_samples_are_materialized_normally() {
    const auto& fixture = teacher_fixture();
    const auto result = materialize_teacher_fixture(fixture);
    require(result && result.value.has_value(), "Teacher dataset materialization failed");
    require(result.value->sample_count() == fixture.envelope.records.size(),
            "continuation-capable materializer changed the record boundary");

    auto continuation_candidates = candidates(3);
    for (std::size_t index = 0; index < continuation_candidates.size(); ++index) {
        auto& candidate = continuation_candidates[index];
        candidate.continuation_operation = "pick";
        candidate.submits_engine_response = false;
        PublicActionKeyInput key;
        key.action_kind = "option";
        key.choice = PublicChoice{PublicChoiceKind::OptionValue,
                                  static_cast<std::uint64_t>(index),
                                  static_cast<std::uint32_t>(index)};
        key.continuation_operation = "pick";
        candidate.public_action_key = ygo::environment::public_action_key(key);
    }
    const auto public_observation = paired_public_observation("continuation-public");
    const auto logical = ygo::model::project_logical_model_input_v1(
        public_observation, continuation_candidates);
    require(logical && logical.value.has_value(),
            "continuation candidate domain was not projected");
    const auto vocabulary = CardVocabularyV1::from_ascending_passcodes({});
    require(vocabulary && vocabulary.value.has_value(),
            "continuation candidate vocabulary construction failed");
    const auto encoded = ygo::model::encode_model_input_v1(
        *logical.value, *vocabulary.value);
    require(encoded && encoded.value.has_value(),
            "continuation candidate domain was not encoded");
    Phase6BcSampleV1 continuation_sample;
    continuation_sample.trajectory_record_id =
        "trajectory_record.v1." + std::string(64, 'a');
    continuation_sample.episode_semantic_id = std::string(64, 'b');
    continuation_sample.logical_model_input = *logical.value;
    continuation_sample.encoded_model_input = *encoded.value;
    continuation_sample.supervision.model_input_identity =
        ygo::model::model_input_identity(*logical.value, *encoded.value);
    continuation_sample.supervision.source_public_semantic_decision_id =
        std::string(64, 'c');
    continuation_sample.supervision.selected_public_action_key =
        logical.value->candidate_routing[1].public_action_key;
    continuation_sample.supervision.candidate_ordinal = 1;
    continuation_sample.sample_identity =
        ygo::phase6::phase6_sample_identity(continuation_sample);
    require(ygo::phase6::canonical_phase6_sample_identity_bytes(continuation_sample).size() > 0,
            "continuation sample identity could not be materialized");
    const auto inspection = ygo::phase6::inspect_public_model_input_v1(
        *logical.value, *encoded.value, std::optional<std::uint32_t>(1));
    require(inspection && inspection.value.has_value() &&
                inspection.value->find("continuation_operation=pick") != std::string::npos &&
                inspection.value->find("submits_engine_response=0") != std::string::npos,
            "continuation candidate fields were not preserved by the public inspector");
}

void test_non_admitted_and_non_teacher_sources_fail_closed() {
    const auto& fixture = teacher_fixture();
    auto invalid_manifest = fixture.manifest;
    invalid_manifest.members.front().admission_receipt_id =
        "admission_receipt.v1." + std::string(64, 'f');
    std::vector<VerifiedAdmissionReceipt> receipts{fixture.receipt};
    std::vector<EpisodeEnvelope> envelopes{fixture.envelope};
    const auto bad_manifest = ygo::phase6::materialize_phase6_dataset_v1(
        invalid_manifest, receipts, envelopes, fixture.vocabulary);
    require(!bad_manifest && bad_manifest.error.has_value() &&
                bad_manifest.error->code == Phase6DataErrorCode::InvalidDatasetManifest,
            "non-manifest receipt membership was accepted");

    auto dropped_candidate = fixture.envelope;
    dropped_candidate.records.front().frame.request.candidates.clear();
    const auto dropped_result = ygo::phase6::materialize_phase6_dataset_v1(
        fixture.manifest, receipts, {dropped_candidate}, fixture.vocabulary);
    require(!dropped_result, "candidate loss was repaired instead of rejected");

    auto reordered_candidates = fixture.envelope;
    if (reordered_candidates.records.front().frame.request.candidates.size() > 1) {
        std::reverse(reordered_candidates.records.front().frame.request.candidates.begin(),
                     reordered_candidates.records.front().frame.request.candidates.end());
        const auto reordered_result = ygo::phase6::materialize_phase6_dataset_v1(
            fixture.manifest, receipts, {reordered_candidates}, fixture.vocabulary);
        require(!reordered_result,
                "candidate reorder was repaired instead of rejected");
    }

    auto quarantined = fixture.envelope;
    quarantined.manifest.collection_disposition.kind =
        CollectionDispositionKind::QuarantinedAfterPolicyRejection;
    const auto quarantined_result = ygo::phase6::materialize_phase6_dataset_v1(
        fixture.manifest, receipts, {quarantined}, fixture.vocabulary);
    require(!quarantined_result, "quarantined trajectory was materialized");

    auto failed = fixture.envelope;
    failed.closure = FailedClosure{};
    const auto failed_result = ygo::phase6::materialize_phase6_dataset_v1(
        fixture.manifest, receipts, {failed}, fixture.vocabulary);
    require(!failed_result, "failed trajectory was materialized");

    const auto& random = random_fixture();
    const auto random_vocabulary = vocabulary_for(random.envelope);
    std::vector<VerifiedAdmissionReceipt> random_receipts{random.receipt};
    const auto random_result = ygo::phase6::materialize_phase6_dataset_v1(
        random.manifest, random_receipts, {random.envelope}, random_vocabulary);
    require(!random_result && random_result.error.has_value() &&
                random_result.error->code == Phase6DataErrorCode::IneligibleTeacherPolicy,
            "RandomLegal trajectory was accepted as a Teacher corpus");

    const auto missing_result = ygo::phase6::materialize_phase6_dataset_v1(
        fixture.manifest, receipts, {}, fixture.vocabulary);
    require(!missing_result, "missing non-manifest episode was accepted");
}

void test_split_identity_is_deterministic_and_episode_scoped(const char* executable) {
    std::vector<std::string> episode_ids;
    for (char value = '0'; value <= '9'; ++value) {
        episode_ids.emplace_back(64, value);
    }
    auto reversed = episode_ids;
    std::reverse(reversed.begin(), reversed.end());
    const auto first = ygo::phase6::make_phase6_split_v1(
        std::string(64, 'a'), episode_ids);
    const auto second = ygo::phase6::make_phase6_split_v1(
        std::string(64, 'a'), reversed);
    require(first && first.value.has_value() && second && second.value.has_value(),
            "deterministic split construction failed");
    require(first.value->split_identity ==
                "phase6_dataset_split.v1.effb91b341d28629a334487033c4688fd626a621fa39e9aa88e339712b0bdca4",
            "frozen 80/10/10 split identity golden mismatch");
    require(first.value->split_identity == second.value->split_identity &&
                ygo::phase6::canonical_phase6_split_identity_bytes(*first.value) ==
                    ygo::phase6::canonical_phase6_split_identity_bytes(*second.value),
            "split identity changed with source enumeration order");

    std::set<std::string> all;
    for (const auto& id : first.value->train_episode_ids) all.insert(id);
    for (const auto& id : first.value->validation_episode_ids) all.insert(id);
    for (const auto& id : first.value->test_episode_ids) all.insert(id);
    require(all.size() == episode_ids.size(), "split lost an episode identity");
    for (const auto& id : episode_ids) {
        const auto partition = ygo::phase6::phase6_partition_for_episode(id);
        require(partition.has_value(), "valid episode identity was not partitionable");
        const auto expected = id.front() == '0'
                                  ? ygo::phase6::Phase6DatasetPartition::Validation
                                  : id.front() == '4'
                                        ? ygo::phase6::Phase6DatasetPartition::Test
                                        : ygo::phase6::Phase6DatasetPartition::Train;
        require(*partition == expected, "frozen split bucket assignment changed");
    }

    const auto& fixture = teacher_fixture();
    const auto dataset = materialize_teacher_fixture(fixture);
    require(dataset && dataset.value.has_value(), "Teacher dataset split failed");
    std::size_t nonempty_partitions = 0;
    if (!dataset.value->train_samples.empty()) ++nonempty_partitions;
    if (!dataset.value->validation_samples.empty()) ++nonempty_partitions;
    if (!dataset.value->test_samples.empty()) ++nonempty_partitions;
    require(nonempty_partitions == 1,
            "one episode was split across train/validation/test partitions");
    const auto process_output = [&]() {
        std::string command = "\"" + std::string(executable) + "\" --split-probe";
#ifdef _WIN32
        FILE* pipe = _popen(command.c_str(), "r");
#else
        FILE* pipe = popen(command.c_str(), "r");
#endif
        require(pipe != nullptr, "could not start split identity probe");
        std::string output;
        char buffer[256];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
#ifdef _WIN32
        const auto status = _pclose(pipe);
#else
        const auto status = pclose(pipe);
#endif
        require(status == 0, "split identity probe failed");
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
            output.pop_back();
        }
        return output;
    };
    require(process_output() == process_output(),
            "split identity changed across independent processes");
}

PublicEnvironmentObservation paired_public_observation(const std::string& marker) {
    ygo::observation::PlayerObservation source;
    source.perspective_player = 0;
    source.decision_index = 17;
    source.engine_step_index = 9001;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_player = 0;
    source.globals.turn_count = 3;
    source.globals.phase = 2;
    source.match_context.perspective_player = 0;
    source.match_context.own_deck.known = true;
    source.match_context.opponent_deck.known = false;
    source.decision_context.kind = "option";
    source.decision_context.player = 0;
    source.decision_context.decision_id = "hidden-decision." + marker;
    source.decision_context.continuation_id = "hidden-continuation." + marker;
    source.decision_context.engine_step_index = 9001;
    source.observation_hash = ygo::observation::observation_hash(source);
    return ygo::environment::project_public_observation(source);
}

std::vector<EnvironmentActionCandidate> candidates(const std::size_t count) {
    std::vector<EnvironmentActionCandidate> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        PublicActionKeyInput key;
        key.action_kind = "option";
        key.choice = PublicChoice{PublicChoiceKind::OptionValue,
                                   static_cast<std::uint64_t>(index),
                                   static_cast<std::uint32_t>(index)};
        EnvironmentActionCandidate candidate;
        candidate.action_kind = EnvironmentActionKind::Option;
        candidate.choice = key.choice;
        candidate.public_action_key = ygo::environment::public_action_key(key);
        result.push_back(std::move(candidate));
    }
    return result;
}

struct ModelInputs final {
    LogicalModelInputV1 logical;
    EncodedModelInputV1 encoded;
};

ModelInputs model_inputs(const std::size_t count) {
    const auto public_observation = paired_public_observation("same-public-state");
    const auto public_candidates = candidates(count);
    const auto logical = ygo::model::project_logical_model_input_v1(
        public_observation, public_candidates);
    require(logical && logical.value.has_value(), "capacity logical projection failed");
    const auto vocabulary = CardVocabularyV1::from_ascending_passcodes({});
    require(vocabulary && vocabulary.value.has_value(), "capacity vocabulary failed");
    const auto encoded = ygo::model::encode_model_input_v1(
        *logical.value, *vocabulary.value);
    require(encoded && encoded.value.has_value(), "capacity encoded projection failed");
    return ModelInputs{std::move(*logical.value), std::move(*encoded.value)};
}

void test_capacity_witnesses_and_public_inspector() {
    for (const std::size_t count : {std::size_t{24}, std::size_t{25}, std::size_t{129}}) {
        auto inputs = model_inputs(count);
        const auto inspection = ygo::phase6::inspect_public_model_input_v1(
            inputs.logical, inputs.encoded,
            std::optional<std::uint32_t>(static_cast<std::uint32_t>(count - 1)));
        require(inspection && inspection.value.has_value(),
                "public model-input inspector rejected a valid boundary");
        require(inspection.value->find("candidate_count=" + std::to_string(count)) !=
                    std::string::npos,
                "inspector did not show exact candidate count");
        require(inspection.value->find("candidate[0].") != std::string::npos &&
                    inspection.value->find("candidate[" + std::to_string(count - 1) + "].") !=
                        std::string::npos,
                "inspector did not show exact candidate order");
        for (const auto forbidden : {"PlayerObservation", "CoreHost", "semantic_key",
                                     "SubmissionToken", "exact_response_bytes",
                                     "raw_message_hash", "continuation_id", "private"}) {
            require(inspection.value->find(forbidden) == std::string::npos,
                    std::string("inspector exposed forbidden field: ") + forbidden);
        }
        require(inspection.value->find("logical_model_input_digest=") != std::string::npos &&
                    inspection.value->find("encoded_model_input_digest=") != std::string::npos &&
                    inspection.value->find("model_input_identity=") != std::string::npos &&
                    inspection.value->find("selected_label=") != std::string::npos,
                "inspector omitted required identity or label fields");

        auto mismatched_encoded = inputs.encoded;
        mismatched_encoded.routing_keys[0].clear();
        const auto mismatched_inspection = ygo::phase6::inspect_public_model_input_v1(
            inputs.logical, mismatched_encoded, std::optional<std::uint32_t>(0));
        require(!mismatched_inspection,
                "inspector accepted a changed encoded candidate routing row");

        const auto ragged = ygo::model::make_ragged_model_batch_v1({inputs.encoded});
        require(ragged && ragged.value.has_value(), "boundary ragged layout failed");
        ygo::model::ModelBatchPaddingRequestV1 wide;
        wide.candidate_width = count + 1;
        const auto padded = ygo::model::pad_model_batch_v1(*ragged.value, wide);
        require(padded && padded.value.has_value(), "boundary padded layout failed");
        ygo::model::ModelBatchPaddingRequestV1 too_small;
        too_small.candidate_width = count - 1;
        const auto rejected = ygo::model::pad_model_batch_v1(*ragged.value, too_small);
        require(!rejected && rejected.error.has_value() &&
                    rejected.error->code == ygo::model::ModelBatchLayoutErrorCode::CapacityTooSmall,
                "physical capacity smaller than N was accepted");
    }
}

void test_paired_hidden_worlds_have_equal_model_inputs() {
    const auto public_a = paired_public_observation("world-a");
    const auto public_b = paired_public_observation("world-b");
    require(ygo::environment::canonical_public_environment_observation_bytes(public_a) ==
                ygo::environment::canonical_public_environment_observation_bytes(public_b),
            "paired hidden worlds changed accepted public observation");
    const auto public_candidates = candidates(3);
    const auto logical_a = ygo::model::project_logical_model_input_v1(public_a, public_candidates);
    const auto logical_b = ygo::model::project_logical_model_input_v1(public_b, public_candidates);
    require(logical_a && logical_b && logical_a.value.has_value() && logical_b.value.has_value(),
            "paired hidden logical projection failed");
    const auto vocabulary = CardVocabularyV1::from_ascending_passcodes({});
    require(vocabulary && vocabulary.value.has_value(), "paired hidden vocabulary failed");
    const auto encoded_a = ygo::model::encode_model_input_v1(*logical_a.value, *vocabulary.value);
    const auto encoded_b = ygo::model::encode_model_input_v1(*logical_b.value, *vocabulary.value);
    require(encoded_a && encoded_b && encoded_a.value.has_value() && encoded_b.value.has_value(),
            "paired hidden encoded projection failed");
    require(ygo::model::canonical_logical_model_input_bytes(*logical_a.value) ==
                ygo::model::canonical_logical_model_input_bytes(*logical_b.value) &&
                ygo::model::canonical_encoded_model_input_bytes(*encoded_a.value) ==
                    ygo::model::canonical_encoded_model_input_bytes(*encoded_b.value),
            "paired hidden worlds changed model representation");
    require(ygo::model::model_input_identity(*logical_a.value, *encoded_a.value) ==
                ygo::model::model_input_identity(*logical_b.value, *encoded_b.value),
            "paired hidden worlds changed model-input identity");
    const auto inspection_a = ygo::phase6::inspect_public_model_input_v1(
        *logical_a.value, *encoded_a.value, std::optional<std::uint32_t>(1));
    const auto inspection_b = ygo::phase6::inspect_public_model_input_v1(
        *logical_b.value, *encoded_b.value, std::optional<std::uint32_t>(1));
    require(inspection_a && inspection_b && inspection_a.value.has_value() &&
                inspection_b.value.has_value() && *inspection_a.value == *inspection_b.value,
            "paired hidden worlds changed inspector output");
}

std::vector<std::string> probe_episode_ids() {
    return {std::string(64, '0'), std::string(64, '1'), std::string(64, '2')};
}

int run_split_probe() {
    const auto result = ygo::phase6::make_phase6_split_v1(
        std::string(64, 'a'), probe_episode_ids());
    if (!result || !result.value.has_value()) return EXIT_FAILURE;
    std::cout << result.value->split_identity << '\n';
    return EXIT_SUCCESS;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--split-probe") {
        return run_split_probe();
    }
    try {
        test_valid_admitted_materialization_and_exact_domain();
        test_continuation_samples_are_materialized_normally();
        test_non_admitted_and_non_teacher_sources_fail_closed();
        test_split_identity_is_deterministic_and_episode_scoped(argv[0]);
        test_capacity_witnesses_and_public_inspector();
        test_paired_hidden_worlds_have_equal_model_inputs();
        std::cout << "phase6 supervision, split, and inspector tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "phase6 supervision, split, and inspector tests failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
