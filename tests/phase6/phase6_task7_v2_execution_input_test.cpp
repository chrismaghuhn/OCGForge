#include "ygo/model/model_batch_layout.hpp"
#include "ygo/model/model_batch_layout_v2.hpp"
#include "ygo/model/encoded_model_input_v2.hpp"
#include "ygo/model/logical_model_input_v2.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/phase6/supervision_dataset_v2.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning_v2.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"
#include "ygo/phase6/task7_input_materialization_v2.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
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

ygo::phase6::Task7MaterializedSampleV2 make_materialized_sample(
    const ModelFixture& fixture) {
    ygo::phase6::Task7MaterializedSampleV2 result;
    result.source_task7_authority_identity =
        "phase6_task7_dataset_authority.v3." + std::string(64, 'd');
    result.source_dataset_manifest_identity =
        "phase6_task7_dataset_manifest.v3." + std::string(64, 'e');
    result.source_dataset_semantic_identity = std::string(64, 'f');
    result.source_training_dataset_split_identity =
        "phase6_dataset_split.v1." + std::string(64, '1');
    result.source_card_vocabulary_identity = fixture.vocabulary.identity();
    result.source_trajectory_record_id = fixture.sample.trajectory_record_id;
    result.source_episode_semantic_id = fixture.sample.episode_semantic_id;
    result.source_public_semantic_decision_id =
        fixture.sample.supervision.source_public_semantic_decision_id;
    result.source_model_input_identity_v2 =
        fixture.sample.supervision.model_input_identity;
    result.supervision = fixture.sample.supervision;
    result.logical_model_input = fixture.logical;
    result.encoded_model_input = fixture.encoded;
    result.routing_keys = fixture.encoded.routing_keys;
    result.canonical_bytes =
        ygo::phase6::canonical_task7_materialized_sample_bytes_v2(result);
    result.sample_identity = ygo::phase6::materialized_sample_identity_v2(result);
    return result;
}

ygo::phase6::Task7MaterializedBatchV2 make_materialized_batch(
    const ModelFixture& fixture, const ygo::model::RaggedModelBatchV2& ragged) {
    ygo::phase6::Task7MaterializedBatchV2 result;
    const auto sample = make_materialized_sample(fixture);
    result.configuration_identity = ygo::phase6::task7_materialization_config_identity_v2();
    result.source_task7_authority_identity = sample.source_task7_authority_identity;
    result.source_dataset_manifest_identity = sample.source_dataset_manifest_identity;
    result.source_dataset_semantic_identity = sample.source_dataset_semantic_identity;
    result.source_training_dataset_split_identity =
        sample.source_training_dataset_split_identity;
    result.source_card_vocabulary_identity = sample.source_card_vocabulary_identity;
    result.ragged = ragged;
    result.samples.push_back(sample);
    result.canonical_bytes =
        ygo::phase6::canonical_task7_materialized_batch_bytes_v2(result);
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

void test_v2_physical_sample_grammar_and_batch_identity() {
    const auto fixture = make_model_fixture();
    const auto ragged = ygo::model::make_ragged_model_batch_v2({fixture.encoded});
    require(static_cast<bool>(ragged), "V2 physical fixture batch construction failed");
    (void)ygo::model::canonical_model_batch_layout_bytes_v2(*ragged.value);
    const auto sample = make_materialized_sample(fixture);
    const auto bytes =
        ygo::phase6::canonical_task7_materialized_sample_bytes_v2(sample);

    ygo::trajectory::ByteReader reader(bytes);
    std::string value;
    for (int index = 0; index < 15; ++index) {
        require(reader.string(value), "V2 sample provenance prefix is truncated");
    }
    std::uint32_t ordinal = 0;
    require(reader.u32be(ordinal) && ordinal == 0,
            "V2 sample supervision ordinal is not canonical");
    require(reader.string(value), "V2 sample observation digest is missing");
    std::uint8_t domain_present = 0;
    require(reader.u8(domain_present) && domain_present <= 1,
            "V2 sample domain presence is invalid");
    if (domain_present != 0) {
        require(reader.string(value), "V2 sample domain digest is truncated");
    }
    require(reader.string(value) && value == "sample_header",
            "V2 sample did not enter the canonical physical table vector");
    std::uint64_t row_count = 0;
    require(reader.u64be(row_count) && row_count == 1,
            "V2 sample header table row count is not canonical");
    require(!ygo::model::canonical_encoded_model_input_bytes(sample.encoded_model_input)
                  .empty(),
            "V2 physical sample lost its encoded source");

    ygo::trajectory::ByteWriter marker_writer;
    marker_writer.string("card_selection_operation_code");
    const auto marker = std::move(marker_writer).take();
    require(std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) !=
                bytes.end(),
            "V2 physical candidate table omitted card-selection operation column");

    auto batch = make_materialized_batch(fixture, *ragged.value);
    require(ygo::phase6::materialized_batch_identity_v2(batch) ==
                std::string(ygo::phase6::kTask7V2MaterializedBatchIdentityPrefix) +
                    ygo::trace::sha256_bytes(batch.canonical_bytes),
            "V2 materialized batch identity did not recompute");
    require(!batch.canonical_bytes.empty() && !sample.canonical_bytes.empty(),
            "V2 physical materialization emitted no canonical bytes");
}

void test_v2_physical_negative_matrix() {
    const auto fixture = make_model_fixture();
    const auto ragged = ygo::model::make_ragged_model_batch_v2({fixture.encoded});
    require(static_cast<bool>(ragged), "V2 negative fixture batch construction failed");
    const auto sample = make_materialized_sample(fixture);
    const auto expect_reject = [](const auto& value, const std::string& message) {
        bool rejected = false;
        try {
            (void)ygo::phase6::canonical_task7_materialized_batch_bytes_v2(value);
        } catch (...) {
            rejected = true;
        }
        require(rejected, message);
    };

    auto detached_sample = sample;
    detached_sample.source_task7_authority_identity =
        "phase6_task7_dataset_authority.v3." + std::string(64, '0');
    auto detached_batch = make_materialized_batch(fixture, *ragged.value);
    detached_batch.samples.front() = detached_sample;
    expect_reject(detached_batch, "V2 batch accepted detached authority binding");

    auto changed_operation = sample;
    changed_operation.encoded_model_input.candidate_features[0]
        .card_selection_operation_code = 2;
    bool rejected = false;
    try {
        (void)ygo::phase6::canonical_task7_materialized_sample_bytes_v2(
            changed_operation);
    } catch (...) {
        rejected = true;
    }
    require(rejected, "V2 sample accepted detached operation metadata");

    auto valid_batch = make_materialized_batch(fixture, *ragged.value);
    auto wrong_routing_batch = valid_batch;
    wrong_routing_batch.ragged.candidate_routing_keys[0] =
        wrong_routing_batch.ragged.candidate_routing_keys[1];
    expect_reject(wrong_routing_batch, "V2 batch accepted detached routing sidecar");

    auto missing_candidate_batch = valid_batch;
    missing_candidate_batch.ragged.candidate_rows.pop_back();
    expect_reject(missing_candidate_batch,
                  "V2 batch accepted a missing source candidate");

    auto duplicate_ragged = ygo::model::make_ragged_model_batch_v2(
        {fixture.encoded, fixture.encoded});
    require(static_cast<bool>(duplicate_ragged),
            "V2 duplicate fixture batch construction failed");
    auto duplicate_batch = valid_batch;
    duplicate_batch.ragged = *duplicate_ragged.value;
    duplicate_batch.samples.push_back(duplicate_batch.samples.front());
    expect_reject(duplicate_batch,
                  "V2 batch accepted duplicate materialized sample identity");

    auto duplicate_record_batch = valid_batch;
    duplicate_record_batch.ragged = *duplicate_ragged.value;
    auto second_record = duplicate_record_batch.samples.front();
    second_record.source_public_semantic_decision_id = std::string(64, '9');
    second_record.supervision.source_public_semantic_decision_id =
        second_record.source_public_semantic_decision_id;
    second_record.canonical_bytes =
        ygo::phase6::canonical_task7_materialized_sample_bytes_v2(second_record);
    second_record.sample_identity =
        ygo::phase6::materialized_sample_identity_v2(second_record);
    duplicate_record_batch.samples.push_back(std::move(second_record));
    const auto duplicate_record_bytes =
        ygo::phase6::canonical_task7_materialized_batch_bytes_v2(duplicate_record_batch);
    require(!duplicate_record_bytes.empty() &&
                duplicate_record_batch.samples[0].sample_identity !=
                    duplicate_record_batch.samples[1].sample_identity,
            "V2 batch rejected distinct decisions from one source episode");

    auto missing_record_batch = valid_batch;
    missing_record_batch.ragged.batch_size = 2;
    expect_reject(missing_record_batch,
                  "V2 batch accepted a missing source record");
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

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open authority file: " + path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                    std::istreambuf_iterator<char>());
}

std::string ordered_sample_identity_projection_sha256(
    const ygo::phase6::Task7MaterializedBatchV2& batch) {
    require(batch.samples.size() <= std::numeric_limits<std::uint32_t>::max(),
            "V2 sample identity projection exceeds u32");
    ygo::trajectory::ByteWriter writer;
    writer.u32be(static_cast<std::uint32_t>(batch.samples.size()));
    for (const auto& sample : batch.samples) writer.string(sample.sample_identity);
    return ygo::trace::sha256_bytes(std::move(writer).take());
}

void test_public_authority_handoff(const int argc, char** argv) {
    require(argc <= 2, "usage: phase6_task7_v2_execution_input_test [authority-file]");
    if (argc == 1) {
        std::cout << "TASK7_V2_PUBLIC_AUTHORITY_E2E=NOT_RUN_NO_AUTHORITY_ARGUMENT\n";
        return;
    }

    const auto bytes = read_binary_file(argv[1]);
    const auto verified = ygo::phase6::decode_task7_v3_authority(bytes);
    require(static_cast<bool>(verified),
            "public V3 authority decoder rejected the supplied authority");
    const auto dataset = ygo::phase6::materialize_phase6_dataset_v2(*verified.value);
    require(static_cast<bool>(dataset),
            "verified V3 authority did not produce a V2 dataset");
    const auto materialized = ygo::phase6::materialize_task7_input_v2(*verified.value);
    require(static_cast<bool>(materialized),
            "verified V3 authority did not produce V2 physical inputs");
    require(!materialized.value->samples.empty() &&
                !materialized.value->canonical_bytes.empty(),
            "public authority handoff produced no V2 materialization");

    const auto& physical = *materialized.value;
    const auto& authority = *verified.value;
    const auto& source_dataset = *dataset.value;
    require(physical.source_task7_authority_identity == authority.identity(),
            "V2 materialization authority binding is detached");
    require(physical.samples.size() == source_dataset.sample_count(),
            "V2 materialization sample count differs from the validated dataset");
    require(physical.ragged.batch_size == physical.samples.size(),
            "V2 materialization ragged batch count is detached");

    const auto ragged_identity =
        ygo::model::model_batch_layout_identity_v2(physical.ragged);
    const auto materialized_identity =
        ygo::phase6::materialized_batch_identity_v2(physical);
    const auto materialized_sha256 =
        ygo::trace::sha256_bytes(physical.canonical_bytes);
    require(materialized_identity ==
                std::string(ygo::phase6::kTask7V2MaterializedBatchIdentityPrefix) +
                    materialized_sha256,
            "V2 materialized batch identity did not match its canonical bytes");

    std::cout << "VERIFIED_AUTHORITY_ID=" << authority.identity() << '\n';
    std::cout << "CONFIGURATION_IDENTITY=" << physical.configuration_identity << '\n';
    std::cout << "DATASET_SEMANTIC_ID=" << source_dataset.source_dataset_identity << '\n';
    std::cout << "SPLIT_IDENTITY=" << source_dataset.split.split_identity << '\n';
    std::cout << "VOCABULARY_IDENTITY=" << authority.value().vocabulary.identity() << '\n';
    std::cout << "TRAIN_SAMPLE_COUNT=" << source_dataset.train_samples.size() << '\n';
    std::cout << "VALIDATION_SAMPLE_COUNT=" << source_dataset.validation_samples.size() << '\n';
    std::cout << "TEST_SAMPLE_COUNT=" << source_dataset.test_samples.size() << '\n';
    std::cout << "TOTAL_SAMPLE_COUNT=" << physical.samples.size() << '\n';
    std::cout << "TOTAL_CANDIDATE_COUNT=" << physical.ragged.candidate_rows.size() << '\n';
    std::cout << "RAGGED_MODEL_BATCH_IDENTITY_V2=" << ragged_identity << '\n';
    std::cout << "MATERIALIZED_BATCH_BYTES=" << physical.canonical_bytes.size() << '\n';
    std::cout << "MATERIALIZED_BATCH_SHA256=" << materialized_sha256 << '\n';
    std::cout << "MATERIALIZED_BATCH_IDENTITY=" << materialized_identity << '\n';
    std::cout << "ORDERED_SAMPLE_IDENTITY_PROJECTION_SHA256="
              << ordered_sample_identity_projection_sha256(physical) << '\n';
    std::cout << "TASK7_V2_PUBLIC_AUTHORITY_E2E=PASS\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        test_v2_configuration_kat();
        test_v2_ragged_and_padded_batch_roundtrip();
        test_v2_physical_sample_grammar_and_batch_identity();
        test_v2_physical_negative_matrix();
        test_v3_authority_decoder_rejects_historical_or_malformed_bytes();
        test_public_authority_handoff(argc, argv);
        std::cout << "TASK7_V2_EXECUTION_INPUT_TEST=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TASK7_V2_EXECUTION_INPUT_TEST=FAIL\n" << error.what() << '\n';
        return 1;
    }
}
