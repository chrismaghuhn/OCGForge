#include "ygo/model/model_supervision_sample.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/policy/production.hpp"
#include "ygo/policy/runner.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/types.hpp"

namespace {

using ygo::model::CardVocabularyV1;
using ygo::model::EncodedModelInputV1;
using ygo::model::LogicalModelInputV1;
using ygo::model::ModelSupervisionSampleErrorCode;
using ygo::model::ModelSupervisionSampleResult;
using ygo::model::ModelSupervisionSampleV1;
using ygo::trajectory::CollectionDispositionKind;
using ygo::trajectory::DecisionRecord;
using ygo::trajectory::EpisodeEnvelope;
using ygo::trajectory::SuccessorKind;
using ygo::trajectory::TerminalClosure;
using ygo::trajectory::TransitionClass;
using ygo::trajectory::VerifiedAdmissionReceipt;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct AdmittedFixture final {
    EpisodeEnvelope envelope;
    VerifiedAdmissionReceipt admission_receipt;
    LogicalModelInputV1 logical;
    EncodedModelInputV1 encoded;
    CardVocabularyV1 vocabulary;
};

void append_public_passcode(std::vector<std::uint32_t>& output,
                            const std::optional<std::uint32_t>& passcode) {
    if (passcode.has_value()) output.push_back(*passcode);
}

std::vector<std::uint32_t> public_passcodes(const LogicalModelInputV1& logical) {
    std::vector<std::uint32_t> result;
    for (const auto& entity : logical.public_safe_state.entities) {
        if (entity.card.identity_known) {
            append_public_passcode(result, entity.card.passcode);
        }
    }
    for (const auto& event : logical.public_safe_state.visible_events) {
        append_public_passcode(result, event.public_passcode);
    }
    const auto append_deck = [&result](const auto& deck) {
        result.insert(result.end(), deck.main_deck.begin(), deck.main_deck.end());
        result.insert(result.end(), deck.extra_deck.begin(), deck.extra_deck.end());
    };
    append_deck(logical.public_safe_state.match_context.own_deck);
    append_deck(logical.public_safe_state.match_context.opponent_deck);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

AdmittedFixture admitted_fixture() {
    ygo::policy::PolicyRunnerConfig config;
    config.environment_config =
        ygo::environment::CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = 2;
    config.episode_spec.seat_assignment = ygo::environment::SeatAssignment::Normal;
    config.episode_spec.starting_player = 0;
    config.run_control.engine_process_budget = 512;
    config.run_control.semantic_action_budget = 1;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "model-supervision-sample-test";

    const auto artifact = ygo::policy::make_random_legal_policy_artifact();
    const std::array<ygo::trajectory::PolicyRole, 2> roles = {
        ygo::trajectory::PolicyRole::Behavior,
        ygo::trajectory::PolicyRole::Opponent};
    const auto assignments = ygo::policy::make_random_legal_participant_assignments(
        artifact, config.environment_config, config.episode_spec.seat_assignment,
        config.episode_spec.starting_player, roles);
    config.policy_provenance.policy_artifacts = {artifact};
    config.policy_provenance.participant_assignments = assignments;
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto assignment = std::find_if(
            assignments.begin(), assignments.end(),
            [player](const auto& value) { return value.player == player; });
        require(assignment != assignments.end(),
                "admitted fixture lacks a participant assignment");
        const auto session = ygo::policy::create_random_legal_policy_session(
            artifact, *assignment,
            player == 0 ? 0x1111111111111111ULL : 0x2222222222222222ULL,
            player == 0 ? "model-test-player0" : "model-test-player1");
        require(session && session.value.has_value(),
                "admitted fixture RandomLegal session construction failed");
        config.sessions[player] = std::move(*session.value);
    }

    auto created = ygo::policy::PolicyRunner::create(std::move(config));
    require(created && created.value.has_value(),
            "admitted fixture policy runner construction failed");
    auto run = created.value->run();
    require(run.disposition == ygo::policy::PolicyRunnerDisposition::CleanAdmitted,
            "admitted fixture did not complete clean admission");
    require(run.envelope.has_value() && run.admission_receipt.has_value() &&
                !run.envelope->records.empty(),
            "admitted fixture lacks an envelope, receipt, or decision record");

    const auto& record = run.envelope->records.front();
    const auto logical_result = ygo::model::project_logical_model_input_v1(
        record.frame.public_observation, record.frame.request.candidates);
    require(logical_result && logical_result.value.has_value(),
            "admitted fixture logical projection failed");
    auto logical = std::move(*logical_result.value);
    const auto vocabulary_result = CardVocabularyV1::from_ascending_passcodes(
        public_passcodes(logical));
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "admitted fixture vocabulary construction failed");
    auto vocabulary = std::move(*vocabulary_result.value);
    const auto encoded_result =
        ygo::model::encode_model_input_v1(logical, vocabulary);
    require(encoded_result && encoded_result.value.has_value(),
            "admitted fixture encoded projection failed");
    auto encoded = std::move(*encoded_result.value);
    return AdmittedFixture{std::move(*run.envelope), std::move(*run.admission_receipt),
                           std::move(logical), std::move(encoded),
                           std::move(vocabulary)};
}

ModelSupervisionSampleResult materialize(const AdmittedFixture& fixture,
                                          const std::size_t record_index = 0) {
    return ygo::model::materialize_model_supervision_sample_v1(
        fixture.envelope, fixture.admission_receipt, record_index, fixture.logical,
        fixture.encoded, fixture.vocabulary);
}

ModelSupervisionSampleV1 require_value(const ModelSupervisionSampleResult& result,
                                       const std::string& context) {
    require(result && result.value.has_value(), context + " was rejected");
    return *result.value;
}

void require_rejected(const ModelSupervisionSampleResult& result,
                      const std::string& context,
                      const std::optional<ModelSupervisionSampleErrorCode>& code = {}) {
    require(!result && !result.value.has_value() && result.error.has_value(),
            context + " was accepted");
    if (code.has_value()) {
        require(result.error->code == *code,
                context + " returned the wrong rejection code");
    }
}

std::size_t selected_ordinal(const DecisionRecord& record) {
    std::size_t matches = 0;
    std::size_t ordinal = 0;
    for (std::size_t index = 0; index < record.frame.request.candidates.size(); ++index) {
        if (record.frame.request.candidates[index].public_action_key ==
            record.selected_public_action_key) {
            ++matches;
            ordinal = index;
        }
    }
    require(matches == 1, "admitted fixture selection is not unique");
    return ordinal;
}

void test_real_admitted_record_maps_exact_key_and_binds_identity() {
    auto fixture = admitted_fixture();
    const auto& record = fixture.envelope.records.front();
    const auto trajectory_bytes_before =
        ygo::trajectory::canonical_collection_decision_record_bytes(record);
    const auto sample = require_value(materialize(fixture), "admitted supervision sample");
    require(sample.candidate_ordinal == selected_ordinal(record),
            "admitted selected key mapped to the wrong candidate ordinal");
    require(sample.selected_public_action_key == record.selected_public_action_key &&
                sample.source_public_semantic_decision_id ==
                    record.frame.public_semantic_decision_id,
            "admitted public selection metadata was not retained exactly");
    require(sample.model_input_identity ==
                ygo::model::model_input_identity(fixture.logical, fixture.encoded),
            "supervision sample did not bind model_input.v1 identity");
    require(fixture.logical.candidate_count() == record.frame.request.candidates.size() &&
                fixture.encoded.candidate_count() == record.frame.request.candidates.size(),
            "admitted model values changed the complete candidate domain");
    require(ygo::trajectory::canonical_collection_decision_record_bytes(record) ==
                trajectory_bytes_before,
            "materialization mutated the trusted trajectory record");

    const auto first_bytes =
        ygo::model::canonical_model_supervision_sample_bytes(sample);
    const auto second_bytes =
        ygo::model::canonical_model_supervision_sample_bytes(sample);
    require(first_bytes == second_bytes, "supervision canonical bytes are not deterministic");
}

void test_full_attribution_and_rng_validation_are_required() {
    auto fixture = admitted_fixture();
    auto bad_assignment = fixture.envelope;
    bad_assignment.records.front().acting_policy_assignment_id = "malformed-assignment";
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            bad_assignment, fixture.admission_receipt, 0, fixture.logical,
            fixture.encoded, fixture.vocabulary),
        "malformed policy attribution",
        ModelSupervisionSampleErrorCode::AdmissionBindingFailure);

    auto bad_rng = fixture.envelope;
    bad_rng.records.front().policy_rng_decision_provenance.policy_rng_identity =
        "malformed-rng-identity";
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            bad_rng, fixture.admission_receipt, 0, fixture.logical,
            fixture.encoded, fixture.vocabulary),
        "malformed RNG provenance",
        ModelSupervisionSampleErrorCode::AdmissionBindingFailure);
}

void test_non_admitted_envelope_and_record_index_fail_closed() {
    auto fixture = admitted_fixture();
    auto quarantined = fixture.envelope;
    quarantined.manifest.collection_disposition.kind =
        CollectionDispositionKind::QuarantinedAfterPolicyRejection;
    quarantined.manifest.collection_disposition.policy_rejections = {
        ygo::environment::RejectionCode::StaleSubmissionToken};
    (void)ygo::trajectory::canonical_episode_envelope_bytes(quarantined);
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            quarantined, fixture.admission_receipt, 0, fixture.logical,
            fixture.encoded, fixture.vocabulary),
        "canonical but non-admitted envelope",
        ModelSupervisionSampleErrorCode::AdmissionBindingFailure);

    require_rejected(materialize(fixture, 1), "out-of-range admitted record index",
                     ModelSupervisionSampleErrorCode::RecordIndexOutOfRange);
}

void test_selection_and_model_mismatches_fail_closed() {
    auto fixture = admitted_fixture();
    auto missing = fixture.envelope;
    missing.records.front().selected_public_action_key =
        "public_action.v1." + std::string(64, 'f');
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            missing, fixture.admission_receipt, 0, fixture.logical,
            fixture.encoded, fixture.vocabulary),
        "missing selected public key");

    auto duplicate = fixture.envelope;
    duplicate.records.front().frame.request.candidates[1].public_action_key =
        duplicate.records.front().frame.request.candidates[0].public_action_key;
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            duplicate, fixture.admission_receipt, 0, fixture.logical,
            fixture.encoded, fixture.vocabulary),
        "duplicate public candidate key");

    auto malformed = fixture.envelope;
    malformed.records.front().selected_public_action_key = "not-a-public-action-key";
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            malformed, fixture.admission_receipt, 0, fixture.logical,
            fixture.encoded, fixture.vocabulary),
        "malformed selected public key");

    auto wrong_logical = fixture.logical;
    ++wrong_logical.decision_index;
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            fixture.envelope, fixture.admission_receipt, 0, wrong_logical,
            fixture.encoded, fixture.vocabulary),
        "mismatched logical model input",
        ModelSupervisionSampleErrorCode::ModelInputMismatch);

    auto wrong_encoded = fixture.encoded;
    ++wrong_encoded.globals.duel_flags;
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            fixture.envelope, fixture.admission_receipt, 0, fixture.logical,
            wrong_encoded, fixture.vocabulary),
        "mismatched encoded model input",
        ModelSupervisionSampleErrorCode::ModelInputMismatch);
}

void test_u32_label_boundaries_and_trajectory_schema_stability() {
    auto fixture = admitted_fixture();
    const auto base = require_value(materialize(fixture), "boundary base sample");
    require(base.candidate_ordinal <= std::numeric_limits<std::uint32_t>::max(),
            "base candidate ordinal is not a u32 label");
    for (const auto value : {24U, 25U, 129U}) {
        auto boundary = base;
        boundary.candidate_ordinal = value - 1;
        const auto first =
            ygo::model::canonical_model_supervision_sample_bytes(boundary);
        const auto second =
            ygo::model::canonical_model_supervision_sample_bytes(boundary);
        require(first == second, "u32 boundary label bytes are not deterministic");
    }
    require(std::string(ygo::trajectory::kTrustedTrajectoryContractId) ==
                "ocgforge.trusted_trajectory.v1",
            "trusted trajectory schema identifier changed");
}

}  // namespace

int main() {
    try {
        test_real_admitted_record_maps_exact_key_and_binds_identity();
        test_full_attribution_and_rng_validation_are_required();
        test_non_admitted_envelope_and_record_index_fail_closed();
        test_selection_and_model_mismatches_fail_closed();
        test_u32_label_boundaries_and_trajectory_schema_stability();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
