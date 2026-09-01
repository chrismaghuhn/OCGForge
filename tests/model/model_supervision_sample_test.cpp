#include "ygo/model/model_supervision_sample.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/types.hpp"

namespace {

using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::EnvironmentDecisionKind;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::environment::PublicEnvironmentObservation;
using ygo::model::CardVocabularyV1;
using ygo::model::EncodedModelInputV1;
using ygo::model::LogicalModelInputV1;
using ygo::model::ModelSupervisionSampleErrorCode;
using ygo::model::ModelSupervisionSampleV1;
using ygo::trajectory::DecisionRecord;
using ygo::trajectory::PublicFrameSnapshot;
using ygo::trajectory::SuccessorKind;
using ygo::trajectory::TransitionClass;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    DecisionRecord record;
    LogicalModelInputV1 logical;
    EncodedModelInputV1 encoded;
    std::optional<CardVocabularyV1> vocabulary;
};

PublicEnvironmentObservation public_observation(const std::uint64_t decision_index) {
    ygo::observation::PlayerObservation source;
    source.perspective_player = 0;
    source.decision_index = decision_index;
    source.match_context.perspective_player = 0;
    source.match_context.own_deck.known = true;
    source.match_context.opponent_deck.known = false;
    source.decision_context.kind = "option";
    source.decision_context.player = 0;
    return ygo::environment::project_public_observation(source);
}

EnvironmentActionCandidate candidate(const std::uint32_t index) {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::Option;
    value.choice = PublicChoice{PublicChoiceKind::EffectChoice, index, std::nullopt};
    PublicActionKeyInput key;
    key.action_kind = "option";
    key.choice = value.choice;
    value.public_action_key = ygo::environment::public_action_key(key);
    return value;
}

Fixture fixture(const std::uint32_t candidate_count,
                const std::uint32_t selected_ordinal) {
    require(candidate_count > 0 && selected_ordinal < candidate_count,
            "invalid supervision fixture dimensions");
    Fixture result;
    const auto observation = public_observation(17);
    result.record.frame.episode_semantic_id = std::string(64, 'a');
    result.record.frame.public_observation = observation;
    result.record.frame.decision_index = observation.decision_index;
    result.record.frame.acting_player = 0;
    result.record.frame.public_observation_digest =
        ygo::environment::public_observation_digest(observation);
    result.record.frame.request.kind = EnvironmentDecisionKind::Option;
    result.record.frame.request.player = 0;
    std::vector<std::string> keys;
    keys.reserve(candidate_count);
    for (std::uint32_t index = 0; index < candidate_count; ++index) {
        result.record.frame.request.candidates.push_back(candidate(index));
        keys.push_back(result.record.frame.request.candidates.back().public_action_key);
    }
    result.record.frame.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest("option", keys);
    ygo::environment::PublicSemanticDecisionIdentityInput decision_identity;
    decision_identity.episode_semantic_id = result.record.frame.episode_semantic_id;
    decision_identity.decision_index = result.record.frame.decision_index;
    decision_identity.acting_player = result.record.frame.acting_player;
    decision_identity.request_kind = "option";
    decision_identity.public_observation_digest =
        result.record.frame.public_observation_digest;
    decision_identity.public_candidate_domain_digest =
        result.record.frame.public_candidate_domain_digest;
    result.record.frame.public_semantic_decision_id =
        ygo::environment::public_semantic_decision_id(decision_identity);
    result.record.selected_public_action_key = keys[selected_ordinal];
    result.record.transition_class = TransitionClass::AtomicEngineResponse;
    result.record.successor.kind = SuccessorKind::Terminal;

    const auto logical_result = ygo::model::project_logical_model_input_v1(
        result.record.frame.public_observation,
        result.record.frame.request.candidates);
    require(logical_result && logical_result.value.has_value(),
            "supervision logical fixture was rejected");
    result.logical = std::move(*logical_result.value);
    const auto vocabulary_result = CardVocabularyV1::from_ascending_passcodes({});
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "empty supervision vocabulary was rejected");
    result.vocabulary = std::move(*vocabulary_result.value);
    const auto encoded_result =
        ygo::model::encode_model_input_v1(result.logical, *result.vocabulary);
    require(encoded_result && encoded_result.value.has_value(),
            "supervision encoded fixture was rejected");
    result.encoded = std::move(*encoded_result.value);
    return result;
}

ModelSupervisionSampleV1 require_value(
    const ygo::model::ModelSupervisionSampleResult& result,
    const std::string& context) {
    require(result && result.value.has_value(), context + " was rejected");
    return *result.value;
}

void require_rejected(
    const ygo::model::ModelSupervisionSampleResult& result,
    const std::string& context) {
    require(!result && !result.value.has_value() && result.error.has_value(),
            context + " was accepted");
}

void test_first_middle_last_and_repeatability() {
    for (const std::uint32_t count : {24U, 25U, 129U}) {
        for (const std::uint32_t ordinal : {0U, count / 2U, count - 1U}) {
            auto value = fixture(count, ordinal);
            const auto first = require_value(
                ygo::model::materialize_model_supervision_sample_v1(
                    value.record, value.logical, value.encoded, *value.vocabulary),
                "N=" + std::to_string(count) + " ordinal=" +
                    std::to_string(ordinal));
            require(first.candidate_ordinal == ordinal &&
                        first.selected_public_action_key ==
                            value.record.selected_public_action_key &&
                        first.source_public_semantic_decision_id ==
                            value.record.frame.public_semantic_decision_id,
                    "selected public key did not map to its exact ordinal");
            require(value.logical.candidate_count() == count &&
                        value.encoded.candidate_count() == count,
                    "supervision fixture changed candidate N");
            const auto second = require_value(
                ygo::model::materialize_model_supervision_sample_v1(
                    value.record, value.logical, value.encoded, *value.vocabulary),
                "repeat materialization");
            require(ygo::model::canonical_model_supervision_sample_bytes(first) ==
                        ygo::model::canonical_model_supervision_sample_bytes(second),
                    "supervision materialization was not deterministic");
        }
    }
}

void test_missing_duplicate_malformed_and_mismatched_inputs_fail_closed() {
    auto value = fixture(5, 2);
    auto missing = value.record;
    missing.selected_public_action_key = "public_action.v1." + std::string(64, 'f');
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            missing, value.logical, value.encoded, *value.vocabulary),
        "missing selected public key");

    auto duplicate = value.record;
    duplicate.frame.request.candidates[1].public_action_key =
        duplicate.frame.request.candidates[0].public_action_key;
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            duplicate, value.logical, value.encoded, *value.vocabulary),
        "duplicate public candidate key");

    auto malformed = value.record;
    malformed.selected_public_action_key = "not-a-public-action-key";
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            malformed, value.logical, value.encoded, *value.vocabulary),
        "malformed selected public key");

    auto wrong_logical = value.logical;
    wrong_logical.decision_index++;
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            value.record, wrong_logical, value.encoded, *value.vocabulary),
        "mismatched logical model input");

    auto wrong_encoded = value.encoded;
    wrong_encoded.globals.duel_flags++;
    require_rejected(
        ygo::model::materialize_model_supervision_sample_v1(
            value.record, value.logical, wrong_encoded, *value.vocabulary),
        "mismatched encoded model input");
}

void test_trajectory_and_private_metadata_are_not_changed_or_exported() {
    auto value = fixture(3, 1);
    const auto before = ygo::trajectory::canonical_public_decision_record_bytes(
        value.record);
    const auto sample = require_value(
        ygo::model::materialize_model_supervision_sample_v1(
            value.record, value.logical, value.encoded, *value.vocabulary),
        "trajectory preservation");
    const auto after = ygo::trajectory::canonical_public_decision_record_bytes(
        value.record);
    require(before == after, "supervision materialization changed trajectory bytes");
    require(sample.schema_id == ygo::model::kModelSupervisionSampleSchemaId &&
                sample.model_input_identity.rfind("model_input.v1.", 0) == 0 &&
                sample.source_public_semantic_decision_id.size() == 64 &&
                sample.selected_public_action_key.find("public_action.v1.") == 0,
            "supervision sample contains an invalid or non-public field");

    auto private_metadata_changed = value.record;
    private_metadata_changed.acting_policy_assignment_id = "private-value-that-is-not-output";
    private_metadata_changed.policy_rng_decision_provenance.policy_rng_identity =
        "private-rng-value-that-is-not-output";
    const auto changed_metadata_sample = require_value(
        ygo::model::materialize_model_supervision_sample_v1(
            private_metadata_changed, value.logical, value.encoded, *value.vocabulary),
        "private metadata shielding");
    require(ygo::model::canonical_model_supervision_sample_bytes(sample) ==
                ygo::model::canonical_model_supervision_sample_bytes(changed_metadata_sample),
            "private trajectory metadata reached the supervision sample");
}

}  // namespace

int main() {
    try {
        test_first_middle_last_and_repeatability();
        test_missing_duplicate_malformed_and_mismatched_inputs_fail_closed();
        test_trajectory_and_private_metadata_are_not_changed_or_exported();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
