#include "ygo/model/model_batch_layout_v2.hpp"
#include "ygo/model/model_batch_layout.hpp"
#include "ygo/model/encoded_model_input_v2.hpp"
#include "ygo/model/logical_model_input_v2.hpp"
#include "ygo/phase6/supervision_dataset_v2.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning_v2.hpp"
#include "ygo/phase6/task7_input_materialization_v2.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trace/sha256.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::PublicCardSelectionOperation;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

EnvironmentActionCandidate card_candidate(const PublicCardSelectionOperation operation) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::CardSelection;
    result.card_selection_operation = operation;
    ygo::environment::PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.card_selection_operation = operation;
    result.public_action_key = ygo::environment::public_action_key_v3(key);
    return result;
}

EnvironmentActionCandidate cancel_candidate() {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::Cancel;
    ygo::environment::PublicActionKeyInput key;
    key.action_kind = "cancel";
    result.public_action_key = ygo::environment::public_action_key_v3(key);
    return result;
}

struct ModelFixture final {
    ygo::model::LogicalModelInputV2 logical;
    ygo::model::EncodedModelInputV2 encoded;
    ygo::phase6::Phase6BcSampleV2 sample;
    ygo::model::CardVocabularyV1 vocabulary;
};

ModelFixture make_model_fixture() {
    ygo::observation::PlayerObservation source;
    source.perspective_player = 0;
    source.match_context.perspective_player = 0;
    source.match_context.own_deck.known = true;
    source.match_context.own_deck.main_deck = {100};
    source.decision_context.kind = "unselect_card";
    source.decision_context.player = 0;
    const auto observation = ygo::environment::project_public_observation(source);
    const std::vector<EnvironmentActionCandidate> candidates = {
        card_candidate(PublicCardSelectionOperation::Select),
        card_candidate(PublicCardSelectionOperation::Unselect),
        cancel_candidate()};
    const auto vocabulary_result =
        ygo::model::CardVocabularyV1::from_ascending_passcodes({100});
    require(static_cast<bool>(vocabulary_result), "V2 fixture vocabulary construction failed");
    const auto logical_result =
        ygo::model::project_logical_model_input_v2(observation, candidates);
    require(static_cast<bool>(logical_result), "V2 fixture logical projection failed");
    const auto encoded_result =
        ygo::model::encode_model_input_v2(*logical_result.value, *vocabulary_result.value);
    require(static_cast<bool>(encoded_result), "V2 fixture encoded projection failed");

    ModelFixture result{*logical_result.value, *encoded_result.value, {},
                        *vocabulary_result.value};
    result.sample.logical_model_input = result.logical;
    result.sample.encoded_model_input = result.encoded;
    result.sample.trajectory_record_id = "trajectory_record.v3." + std::string(64, 'a');
    result.sample.episode_semantic_id = std::string(64, 'b');
    result.sample.supervision.model_input_identity =
        ygo::model::model_input_identity_v2(result.logical, result.encoded);
    result.sample.supervision.source_public_semantic_decision_id = std::string(64, 'c');
    result.sample.supervision.selected_public_action_key =
        result.encoded.routing_keys.front();
    result.sample.supervision.candidate_ordinal = 0;
    result.sample.sample_identity = ygo::phase6::phase6_sample_identity_v2(result.sample);
    return result;
}

ygo::phase6::detail::Task7MaterializationSourceBatchV2 make_source(
    const ModelFixture& fixture,
    const ygo::model::RaggedModelBatchV2& ragged) {
    ygo::phase6::detail::Task7MaterializationSourceSampleV2 sample;
    sample.sample = &fixture.sample;
    sample.source_task7_authority_identity =
        "phase6_task7_dataset_authority.v3." + std::string(64, 'd');
    sample.source_dataset_manifest_identity =
        "phase6_task7_dataset_manifest.v3." + std::string(64, 'e');
    sample.source_dataset_semantic_identity = std::string(64, 'f');
    sample.source_training_dataset_split_identity =
        "phase6_dataset_split.v1." + std::string(64, '1');
    sample.source_card_vocabulary_identity = fixture.vocabulary.identity();

    ygo::phase6::detail::Task7MaterializationSourceBatchV2 result;
    result.ragged = &ragged;
    result.vocabulary = &fixture.vocabulary;
    result.source_task7_authority_identity = sample.source_task7_authority_identity;
    result.source_dataset_manifest_identity = sample.source_dataset_manifest_identity;
    result.source_dataset_semantic_identity = sample.source_dataset_semantic_identity;
    result.source_training_dataset_split_identity =
        sample.source_training_dataset_split_identity;
    result.source_card_vocabulary_identity = sample.source_card_vocabulary_identity;
    result.samples.push_back(std::move(sample));
    return result;
}

void test_v2_configuration_kat() {
    const auto bytes = ygo::phase6::canonical_task7_materialization_config_bytes_v2();
    require(bytes.size() == 8317, "V2 materialization configuration length changed");
    require(ygo::trace::sha256_bytes(bytes) ==
                "ce39fdd472614f4fa9e622d93fb5628549dd3705e501e9d287e679fa307063b9",
            "V2 materialization configuration KAT changed");
    require(ygo::phase6::task7_materialization_config_identity_v2() ==
                "phase6_task7_input_materialization_config.v2.ce39fdd472614f4fa9e622d93fb5628549dd3705e501e9d287e679fa307063b9",
            "V2 materialization configuration identity changed");
}

void test_v2_ragged_and_padded_batch_roundtrip() {
    const auto fixture = make_model_fixture();
    const auto batch = ygo::model::make_ragged_model_batch_v2({fixture.encoded});
    require(static_cast<bool>(batch), "V2 ragged batch construction failed");
    require(batch.value->candidate_rows.size() == 3 &&
                batch.value->candidate_routing_keys.size() == 3,
            "V2 ragged batch changed candidate cardinality");
    require(batch.value->candidate_rows[0].card_selection_operation_code == 1 &&
                batch.value->candidate_rows[1].card_selection_operation_code == 2 &&
                batch.value->candidate_rows[2].card_selection_operation_code == 0,
            "V2 ragged batch lost operation codes");
    const auto reconstructed =
        ygo::model::reconstruct_model_batch_sample_v2(*batch.value, 0);
    require(ygo::model::canonical_encoded_model_input_bytes(reconstructed) ==
                ygo::model::canonical_encoded_model_input_bytes(fixture.encoded),
            "V2 ragged batch reconstruction changed encoded input");

    ygo::model::ModelBatchPaddingRequestV2 request;
    request.candidate_width = 3;
    const auto padded = ygo::model::pad_model_batch_v2(*batch.value, request);
    require(static_cast<bool>(padded), "V2 padded batch construction failed");
    const auto unpadded = ygo::model::unpad_model_batch_v2(*padded.value);
    require(static_cast<bool>(unpadded), "V2 padded batch unpadding failed");
    require(ygo::model::canonical_encoded_model_input_bytes(
                ygo::model::reconstruct_model_batch_sample_v2(*unpadded.value, 0)) ==
                ygo::model::canonical_encoded_model_input_bytes(fixture.encoded),
            "V2 padded/unpadded batch changed encoded input");

    auto wrong_ragged_schema = *batch.value;
    wrong_ragged_schema.schema_id = ygo::model::kModelBatchLayoutSchemaId;
    bool rejected = false;
    try {
        (void)ygo::model::canonical_model_batch_layout_bytes_v2(wrong_ragged_schema);
    } catch (...) {
        rejected = true;
    }
    require(rejected, "V2 batch accepted a historical V1 layout schema");

    auto wrong_input_schema = fixture.encoded;
    wrong_input_schema.schema_id = ygo::model::kEncodedModelInputSchemaId;
    require(!ygo::model::make_ragged_model_batch_v2({wrong_input_schema}),
            "V2 batch accepted a historical V1 encoded input");

    auto too_small = request;
    too_small.candidate_width = 2;
    require(!ygo::model::pad_model_batch_v2(*batch.value, too_small),
            "V2 padding accepted a capacity below N");

    auto changed_padded = *padded.value;
    changed_padded.candidate_features_padded[0].card_selection_operation_code = 2;
    require(!ygo::model::unpad_model_batch_v2(changed_padded),
            "V2 unpadding accepted a changed operation code");
}

void test_v2_materialization_preserves_semantics() {
    const auto fixture = make_model_fixture();
    const auto batch = ygo::model::make_ragged_model_batch_v2({fixture.encoded});
    require(static_cast<bool>(batch), "V2 materialization fixture batch failed");
    auto source = make_source(fixture, *batch.value);
    const auto reconstructed =
        ygo::model::reconstruct_model_batch_sample_v2(*batch.value, 0);
    require(fixture.sample.schema_id == ygo::phase6::kPhase6BcSampleIdentityDomainV2,
            "V2 fixture sample schema is detached");
    require(ygo::phase6::phase6_sample_identity_v2(fixture.sample) ==
                fixture.sample.sample_identity,
            "V2 fixture supervision identity is detached");
    require(fixture.sample.encoded_model_input.card_vocabulary_identity ==
                source.source_card_vocabulary_identity,
            "V2 fixture vocabulary binding is detached");
    require(ygo::model::canonical_encoded_model_input_bytes(
                fixture.sample.encoded_model_input) ==
                ygo::model::canonical_encoded_model_input_bytes(reconstructed),
            "V2 fixture encoded input is detached from its ragged batch");
    const auto materialized = ygo::phase6::detail::materialize_task7_input_v2(source);
    require(static_cast<bool>(materialized),
            "V2 physical materialization failed: " +
                (materialized.error.has_value() ? materialized.error->diagnostic
                                                 : "unknown"));
    require(materialized.value->samples.size() == 1 &&
                materialized.value->samples.front().encoded_model_input
                        .candidate_features.size() == 3,
            "V2 materialization changed sample/candidate cardinality");
    require(materialized.value->samples.front().encoded_model_input
                    .candidate_features[0].card_selection_operation_code == 1 &&
                materialized.value->samples.front().encoded_model_input
                        .candidate_features[1].card_selection_operation_code == 2,
            "V2 materialization lost card-selection operation codes");
    require(materialized.value->samples.front().routing_keys == fixture.encoded.routing_keys,
            "V2 materialization changed routing-key order");
    require(ygo::phase6::materialized_sample_identity_v2(
                materialized.value->samples.front()) ==
                materialized.value->samples.front().sample_identity,
            "V2 materialized sample identity did not recompute");
    require(!materialized.value->canonical_bytes.empty() &&
                !materialized.value->samples.front().canonical_bytes.empty(),
            "V2 materialization did not emit canonical bytes");
    require(ygo::phase6::materialized_batch_identity_v2(*materialized.value) ==
                std::string(ygo::phase6::kTask7V2MaterializedBatchIdentityPrefix) +
                    ygo::trace::sha256_bytes(materialized.value->canonical_bytes),
            "V2 materialized batch identity did not recompute");
}

void test_v2_materialization_rejects_detached_fields() {
    const auto fixture = make_model_fixture();
    const auto batch = ygo::model::make_ragged_model_batch_v2({fixture.encoded});
    require(static_cast<bool>(batch), "V2 negative fixture batch failed");
    auto source = make_source(fixture, *batch.value);

    auto wrong_authority = source;
    wrong_authority.source_task7_authority_identity =
        "phase6_task7_dataset_authority.v2." + std::string(64, '0');
    require(!ygo::phase6::detail::materialize_task7_input_v2(wrong_authority),
            "V2 materializer accepted a V2 authority identity");

    auto wrong_routing = *batch.value;
    wrong_routing.candidate_routing_keys[0] = wrong_routing.candidate_routing_keys[1];
    auto wrong_routing_source = make_source(fixture, wrong_routing);
    require(!ygo::phase6::detail::materialize_task7_input_v2(wrong_routing_source),
            "V2 materializer accepted detached routing keys");

    auto wrong_manifest = source;
    wrong_manifest.source_dataset_manifest_identity =
        "phase6_task7_dataset_manifest.v3." + std::string(64, '0');
    require(!ygo::phase6::detail::materialize_task7_input_v2(wrong_manifest),
            "V2 materializer accepted a detached DatasetManifest identity");

    auto wrong_split = source;
    wrong_split.source_training_dataset_split_identity =
        "phase6_dataset_split.v1." + std::string(64, '0');
    require(!ygo::phase6::detail::materialize_task7_input_v2(wrong_split),
            "V2 materializer accepted a detached split identity");

    auto wrong_vocabulary = source;
    wrong_vocabulary.source_card_vocabulary_identity =
        "model_card_vocabulary.v1." + std::string(64, '0');
    require(!ygo::phase6::detail::materialize_task7_input_v2(wrong_vocabulary),
            "V2 materializer accepted a detached vocabulary identity");

    auto missing_candidate = *batch.value;
    missing_candidate.candidate_rows.pop_back();
    auto missing_candidate_source = make_source(fixture, missing_candidate);
    require(!ygo::phase6::detail::materialize_task7_input_v2(missing_candidate_source),
            "V2 materializer accepted a candidate-count mismatch");

    auto reordered = *batch.value;
    std::swap(reordered.candidate_rows[0], reordered.candidate_rows[1]);
    std::swap(reordered.candidate_routing_keys[0], reordered.candidate_routing_keys[1]);
    auto reordered_source = make_source(fixture, reordered);
    require(!ygo::phase6::detail::materialize_task7_input_v2(reordered_source),
            "V2 materializer accepted a candidate reorder");

    auto changed_continuation = fixture.encoded;
    changed_continuation.candidate_features[0].continuation_operation_code = 1;
    const auto changed_continuation_batch =
        ygo::model::make_ragged_model_batch_v2({changed_continuation});
    require(!changed_continuation_batch,
            "V2 batch accepted detached continuation semantics");

    auto changed_submission = fixture.encoded;
    changed_submission.candidate_features[0].submits_engine_response = false;
    const auto changed_submission_batch =
        ygo::model::make_ragged_model_batch_v2({changed_submission});
    require(static_cast<bool>(changed_submission_batch),
            "V2 response-flag mutation fixture was rejected too early");
    auto changed_submission_source = make_source(
        fixture, *changed_submission_batch.value);
    require(!ygo::phase6::detail::materialize_task7_input_v2(changed_submission_source),
            "V2 materializer accepted detached response semantics");

    auto wrong_operation = fixture.encoded;
    wrong_operation.candidate_features[0].card_selection_operation_code = 2;
    const auto wrong_operation_batch =
        ygo::model::make_ragged_model_batch_v2({wrong_operation});
    require(!wrong_operation_batch,
            "V2 batch accepted a noncanonical operation/source mismatch");
}

void test_v3_authority_decoder_rejects_historical_or_malformed_bytes() {
    require(!ygo::phase6::decode_task7_v3_authority({}),
            "V3 authority decoder accepted empty bytes");
    const auto v2_schedule = ygo::phase6::make_task7_collection_schedule_v2(
        "849b1f4f6f6ed3e73252996b9effa85d597a5e9d");
    const auto v2_bytes = ygo::phase6::canonical_task7_collection_schedule_bytes_v2(
        v2_schedule);
    require(!ygo::phase6::decode_task7_v3_authority(v2_bytes),
            "V3 authority decoder accepted historical schedule bytes");
}

}  // namespace

int main() {
    try {
        test_v2_configuration_kat();
        test_v2_ragged_and_padded_batch_roundtrip();
        test_v2_materialization_preserves_semantics();
        test_v2_materialization_rejects_detached_fields();
        test_v3_authority_decoder_rejects_historical_or_malformed_bytes();
        std::cout << "TASK7_V2_EXECUTION_INPUT_TEST=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TASK7_V2_EXECUTION_INPUT_TEST=FAIL\n" << error.what() << '\n';
        return 1;
    }
}
