#include "ygo/phase6/bc_candidate_scorer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/model/encoded_model_input.hpp"

namespace {

using ygo::environment::PublicActionKeyInput;
using ygo::model::EncodedCandidate;
using ygo::model::EncodedCardReference;
using ygo::model::EncodedCurrentReference;
using ygo::model::EncodedEntity;
using ygo::model::EncodedModelInputV1;
using ygo::phase6::Phase6BcCandidateInputV1;
using ygo::phase6::Phase6BcCandidateRepresentationV1;
using ygo::phase6::Phase6BcLocatorNamespace;
using ygo::phase6::Phase6BcReferenceScorerV1;
using ygo::phase6::Phase6BcScorerErrorCode;
using ygo::phase6::Phase6BcStateInputV1;
using ygo::phase6::Phase6BcStateRepresentationV1;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T, typename = void>
struct has_candidate_features final : std::false_type {};

template <typename T>
struct has_candidate_features<T, std::void_t<decltype(std::declval<T>().candidate_features)>>
    final : std::true_type {};

template <typename T, typename = void>
struct has_routing_keys final : std::false_type {};

template <typename T>
struct has_routing_keys<T, std::void_t<decltype(std::declval<T>().routing_keys)>>
    final : std::true_type {};

template <typename T, typename = void>
struct has_candidate_domain_digest final : std::false_type {};

template <typename T>
struct has_candidate_domain_digest<
    T, std::void_t<decltype(std::declval<T>().public_candidate_domain_digest)>>
    final : std::true_type {};

template <typename T, typename = void>
struct has_candidate_ordinal final : std::false_type {};

template <typename T>
struct has_candidate_ordinal<T, std::void_t<decltype(std::declval<T>().candidate_ordinal)>>
    final : std::true_type {};

template <typename T, typename = void>
struct has_public_action_key final : std::false_type {};

template <typename T>
struct has_public_action_key<T, std::void_t<decltype(std::declval<T>().public_action_key)>>
    final : std::true_type {};

std::string candidate_key(const std::uint32_t index) {
    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.source_index = index;
    return ygo::environment::public_action_key(key);
}

EncodedModelInputV1 encoded_input(const std::uint32_t candidate_count) {
    EncodedModelInputV1 input;
    input.card_vocabulary_identity =
        std::string(ygo::model::kCardVocabularyIdentityPrefix) + std::string(64, '0');
    input.public_observation_digest = std::string(64, '1');
    input.perspective_player = 0;
    input.decision_index = 17;
    input.public_observation_context_kind_code = std::uint16_t{5};
    input.public_observation_context_player = std::uint8_t{0};
    input.globals.life_points = {8000, 7000};
    input.match_context.perspective_player = 0;
    input.match_context.own_deck.known = true;

    std::vector<std::string> keys;
    for (std::uint32_t index = 0; index < candidate_count; ++index) {
        EncodedCandidate candidate;
        candidate.action_kind_code = 5;
        candidate.source_index = index;
        input.candidate_features.push_back(candidate);
        keys.push_back(candidate_key(index));
        input.routing_keys.push_back(keys.back());
    }
    input.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest("card_selection", keys);
    return input;
}

void test_state_input_cannot_expose_candidate_domain() {
    static_assert(!has_candidate_features<Phase6BcStateInputV1>::value,
                  "state input must not expose candidate features");
    static_assert(!has_routing_keys<Phase6BcStateInputV1>::value,
                  "state input must not expose routing keys");
    static_assert(!has_candidate_domain_digest<Phase6BcStateInputV1>::value,
                  "state input must not expose candidate-domain digest");
    static_assert(!has_candidate_ordinal<Phase6BcStateInputV1>::value,
                  "state input must not expose candidate ordinal");
    static_assert(!has_public_action_key<Phase6BcCandidateInputV1>::value,
                  "candidate input must not expose public action keys");
    static_assert(!has_routing_keys<Phase6BcCandidateInputV1>::value,
                  "candidate input must not expose routing keys");
}

void test_state_input_rebuilds_locator_table_without_candidate_locators() {
    auto input = encoded_input(2);
    input.public_locator_table = {"candidate:only", "state:only"};
    input.observation_context_reference_ordinals = {1};

    EncodedEntity state_entity;
    state_entity.public_locator_ordinal = 1;
    state_entity.identity_known = false;
    state_entity.card_vocabulary_id = 1;
    state_entity.zone_code = 3;
    state_entity.face_down = true;
    input.entities.push_back(state_entity);

    input.candidate_features.front().source_reference = EncodedCardReference{
        0, EncodedCurrentReference{1, std::nullopt}};
    input.candidate_features[1].source_reference = EncodedCardReference{
        0, EncodedCurrentReference{0, std::nullopt}};
    PublicActionKeyInput state_key_with_locator;
    state_key_with_locator.action_kind = "card_selection";
    state_key_with_locator.source_index = 0;
    state_key_with_locator.source_reference = {
        ygo::environment::PublicCardReferenceKind::VisibleCard, "state:only"};
    input.routing_keys.front() =
        ygo::environment::public_action_key(state_key_with_locator);
    PublicActionKeyInput candidate_key_with_locator;
    candidate_key_with_locator.action_kind = "card_selection";
    candidate_key_with_locator.source_index = 1;
    candidate_key_with_locator.source_reference = {
        ygo::environment::PublicCardReferenceKind::VisibleCard, "candidate:only"};
    input.routing_keys[1] =
        ygo::environment::public_action_key(candidate_key_with_locator);
    input.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest(
            "card_selection", input.routing_keys);

    bool state_only = false;
    bool state_reference_compatible = false;
    bool candidate_only_reference_separate = false;
    Phase6BcReferenceScorerV1 scorer;
    scorer.state_encoder = [&state_only](const Phase6BcStateInputV1& state) {
        state_only = state.public_locator_table == std::vector<std::string>{"state:only"} &&
                     state.observation_context_reference_ordinals ==
                         std::vector<std::uint32_t>{0} &&
                     state.entities.size() == 1 &&
                     state.entities.front().public_locator_ordinal == 0;
        Phase6BcStateRepresentationV1 representation;
        representation.values.push_back(state.entities.front().public_locator_ordinal);
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcStateRepresentationV1>{
            std::optional<Phase6BcStateRepresentationV1>(std::move(representation)),
            std::nullopt};
    };
    scorer.candidate_encoder = [&state_reference_compatible,
                                &candidate_only_reference_separate](
                                   const Phase6BcStateRepresentationV1&,
                                  const Phase6BcCandidateInputV1& candidate) {
        if (candidate.source_index == std::optional<std::uint32_t>(0) &&
            candidate.source_reference.has_value()) {
            const auto& reference = *candidate.source_reference;
            state_reference_compatible =
                reference.locator_namespace == Phase6BcLocatorNamespace::State &&
                reference.locator_ordinal == 0;
        }
        if (candidate.source_index == std::optional<std::uint32_t>(1) &&
            candidate.source_reference.has_value()) {
            const auto& reference = *candidate.source_reference;
            candidate_only_reference_separate =
                reference.locator_namespace == Phase6BcLocatorNamespace::CandidateOnly &&
                reference.locator_ordinal == 0;
        }
        Phase6BcCandidateRepresentationV1 representation;
        representation.values.push_back(candidate.source_index.value_or(0));
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>{
            std::optional<Phase6BcCandidateRepresentationV1>(std::move(representation)),
            std::nullopt};
    };
    scorer.candidate_scoring_function =
        [](const Phase6BcStateRepresentationV1&,
           const Phase6BcCandidateRepresentationV1& candidate) {
            return ygo::phase6::Phase6BcCallbackResult<double>{
                std::optional<double>(static_cast<double>(candidate.values.front())),
                std::nullopt};
    };

    const auto result = ygo::phase6::score_encoded_model_input_v1(input, scorer);
    require(result && result.value.has_value() && state_only &&
                state_reference_compatible && candidate_only_reference_separate,
            "state/candidate locator namespaces were not remapped compatibly");
}

void test_pipeline_preserves_order_and_returns_one_score_per_candidate() {
    const auto input = encoded_input(3);
    std::vector<std::size_t> encoded_ordinals;
    std::vector<std::size_t> scored_ordinals;
    std::size_t state_calls = 0;

    Phase6BcReferenceScorerV1 scorer;
    scorer.state_encoder = [&state_calls](const Phase6BcStateInputV1& value) {
        ++state_calls;
        Phase6BcStateRepresentationV1 state;
        state.values.push_back(value.decision_index);
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcStateRepresentationV1>{
            std::optional<Phase6BcStateRepresentationV1>(std::move(state)), std::nullopt};
    };
    scorer.candidate_encoder = [&encoded_ordinals](
                                   const Phase6BcStateRepresentationV1&,
                                   const Phase6BcCandidateInputV1& candidate) {
        encoded_ordinals.push_back(candidate.source_index.value_or(0));
        Phase6BcCandidateRepresentationV1 representation;
        representation.values.push_back(candidate.source_index.value_or(0));
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>{
            std::optional<Phase6BcCandidateRepresentationV1>(std::move(representation)),
            std::nullopt};
    };
    scorer.candidate_scoring_function = [&scored_ordinals](
                                            const Phase6BcStateRepresentationV1&,
                                            const Phase6BcCandidateRepresentationV1& candidate) {
        scored_ordinals.push_back(candidate.values.front());
        return ygo::phase6::Phase6BcCallbackResult<double>{
            std::optional<double>(10.0 + static_cast<double>(candidate.values.front())),
            std::nullopt};
    };

    const auto result = ygo::phase6::score_encoded_model_input_v1(input, scorer);
    require(result && result.value.has_value(), "reference scorer rejected valid input");
    require(result.value->scores == std::vector<double>({10.0, 11.0, 12.0}),
            "reference scorer did not return one score per ordered candidate");
    require(encoded_ordinals == std::vector<std::size_t>({0, 1, 2}) &&
                scored_ordinals == std::vector<std::size_t>({0, 1, 2}),
            "reference scorer changed candidate invocation order");
    require(state_calls == 1, "reference scorer did not encode state exactly once");
    require(result.value->selected_candidate_ordinal == 2 &&
                result.value->selected_public_action_key == input.routing_keys[2],
                "reference scorer did not resolve the selected score to its paired public key");
}

Phase6BcReferenceScorerV1 reference_scorer(std::vector<std::size_t>& calls) {
    Phase6BcReferenceScorerV1 scorer;
    scorer.state_encoder = [](const Phase6BcStateInputV1& value) {
        Phase6BcStateRepresentationV1 state;
        state.values.push_back(value.decision_index);
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcStateRepresentationV1>{
            std::optional<Phase6BcStateRepresentationV1>(std::move(state)), std::nullopt};
    };
    scorer.candidate_encoder = [&calls](const Phase6BcStateRepresentationV1&,
                                         const Phase6BcCandidateInputV1& candidate) {
        calls.push_back(candidate.source_index.value_or(0));
        Phase6BcCandidateRepresentationV1 representation;
        representation.values.push_back(candidate.source_index.value_or(0));
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>{
            std::optional<Phase6BcCandidateRepresentationV1>(std::move(representation)),
            std::nullopt};
    };
    scorer.candidate_scoring_function = [](const Phase6BcStateRepresentationV1&,
                                           const Phase6BcCandidateRepresentationV1& candidate) {
        return ygo::phase6::Phase6BcCallbackResult<double>{
            std::optional<double>(static_cast<double>(candidate.values.front())),
            std::nullopt};
    };
    return scorer;
}

void require_error(const ygo::phase6::Phase6BcInferenceResult& result,
                   const Phase6BcScorerErrorCode expected,
                   const std::string& message) {
    require(!result && !result.value.has_value() && result.error.has_value() &&
                result.error->code == expected,
            message);
}

void require_selection_error(const ygo::phase6::Phase6BcSelectionResult& result,
                             const Phase6BcScorerErrorCode expected,
                             const std::string& message) {
    require(!result && !result.value.has_value() && result.error.has_value() &&
                result.error->code == expected,
            message);
}

void test_ties_use_bytewise_public_key_order() {
    const auto input = encoded_input(3);
    require(input.routing_keys[0] < input.routing_keys[1],
            "fixture did not provide deterministic key ordering for tie test");
    const auto result = ygo::phase6::select_phase6_candidate_v1(
        input, std::vector<double>{4.0, 4.0, 3.0});
    require(result && result.value.has_value() &&
                result.value->selected_candidate_ordinal == 0 &&
                result.value->selected_public_action_key == input.routing_keys[0],
            "equal scores did not use the bytewise-ascending public key tie rule");
}

void test_capacity_boundaries_are_exact_and_fail_closed() {
    for (const std::uint32_t count : {24U, 25U, 129U}) {
        const auto input = encoded_input(count);
        const auto before = ygo::model::canonical_encoded_model_input_bytes(input);
        std::vector<std::size_t> calls;
        auto scorer = reference_scorer(calls);
        scorer.physical_candidate_capacity = count;
        const auto accepted = ygo::phase6::score_encoded_model_input_v1(input, scorer);
        require(accepted && accepted.value.has_value() &&
                    accepted.value->scores.size() == count && calls.size() == count,
                "exact candidate capacity rejected a valid boundary domain");
        require(accepted.value->selected_candidate_ordinal == count - 1 &&
                    accepted.value->selected_public_action_key == input.routing_keys.back(),
                "boundary scorer did not preserve the final candidate/key pairing");

        calls.clear();
        scorer.physical_candidate_capacity = count - 1;
        const auto rejected = ygo::phase6::score_encoded_model_input_v1(input, scorer);
        require_error(rejected, Phase6BcScorerErrorCode::CandidateCapacityTooSmall,
                      "undersized candidate capacity was not rejected");
        require(calls.empty() &&
                    ygo::model::canonical_encoded_model_input_bytes(input) == before,
                "undersized capacity invoked callbacks or mutated the encoded domain");
    }
}

void test_missing_and_failing_callbacks_fail_closed() {
    const auto input = encoded_input(3);
    std::vector<std::size_t> calls;

    auto missing_state = reference_scorer(calls);
    missing_state.state_encoder = {};
    require_error(ygo::phase6::score_encoded_model_input_v1(input, missing_state),
                  Phase6BcScorerErrorCode::MissingStateEncoder,
                  "missing state encoder was not rejected");

    auto failing_state = reference_scorer(calls);
    failing_state.state_encoder = [](const Phase6BcStateInputV1&) {
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcStateRepresentationV1>{
            std::nullopt, ygo::phase6::Phase6BcCallbackError{"state failure"}};
    };
    require_error(ygo::phase6::score_encoded_model_input_v1(input, failing_state),
                  Phase6BcScorerErrorCode::StateEncoderFailure,
                  "state encoder failure was not rejected");

    auto missing_candidate = reference_scorer(calls);
    missing_candidate.candidate_encoder = {};
    require_error(ygo::phase6::score_encoded_model_input_v1(input, missing_candidate),
                  Phase6BcScorerErrorCode::MissingCandidateEncoder,
                  "missing candidate encoder was not rejected");

    auto missing_score = reference_scorer(calls);
    missing_score.candidate_scoring_function = {};
    require_error(ygo::phase6::score_encoded_model_input_v1(input, missing_score),
                  Phase6BcScorerErrorCode::MissingCandidateScoringFunction,
                  "missing candidate scoring function was not rejected");

    auto failing_candidate = reference_scorer(calls);
    failing_candidate.candidate_encoder = [](const Phase6BcStateRepresentationV1&,
                                             const Phase6BcCandidateInputV1& candidate) {
        if (candidate.source_index == std::optional<std::uint32_t>(1)) {
            return ygo::phase6::Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>{
                std::nullopt, ygo::phase6::Phase6BcCallbackError{"candidate failure"}};
        }
        Phase6BcCandidateRepresentationV1 representation;
        representation.values.push_back(candidate.source_index.value_or(0));
        return ygo::phase6::Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>{
            std::optional<Phase6BcCandidateRepresentationV1>(std::move(representation)),
            std::nullopt};
    };
    require_error(ygo::phase6::score_encoded_model_input_v1(input, failing_candidate),
                  Phase6BcScorerErrorCode::CandidateEncoderFailure,
                  "candidate encoder failure was not rejected");

    auto failing_score = reference_scorer(calls);
    failing_score.candidate_scoring_function =
        [](const Phase6BcStateRepresentationV1&,
           const Phase6BcCandidateRepresentationV1&) {
            return ygo::phase6::Phase6BcCallbackResult<double>{
                std::nullopt, ygo::phase6::Phase6BcCallbackError{"score failure"}};
        };
    require_error(ygo::phase6::score_encoded_model_input_v1(input, failing_score),
                  Phase6BcScorerErrorCode::CandidateScoringFailure,
                  "candidate scoring failure was not rejected");
}

void test_invalid_encoded_input_and_scores_fail_closed() {
    const auto input = encoded_input(3);
    std::vector<std::size_t> calls;
    auto scorer = reference_scorer(calls);

    auto malformed = input;
    malformed.routing_keys.pop_back();
    require_error(ygo::phase6::score_encoded_model_input_v1(malformed, scorer),
                  Phase6BcScorerErrorCode::InvalidEncodedModelInput,
                  "malformed encoded routing sidecar was accepted");
    require(calls.empty(), "malformed encoded input reached the callbacks");

    require_selection_error(
        ygo::phase6::select_phase6_candidate_v1(input, std::vector<double>{1.0, 2.0}),
        Phase6BcScorerErrorCode::ScoreCountMismatch,
        "wrong score cardinality was accepted");

    for (const double invalid_score : {std::numeric_limits<double>::quiet_NaN(),
                                       std::numeric_limits<double>::infinity(),
                                       -std::numeric_limits<double>::infinity()}) {
        require_selection_error(
            ygo::phase6::select_phase6_candidate_v1(
                input, std::vector<double>{invalid_score, 2.0, 1.0}),
            Phase6BcScorerErrorCode::NonFiniteScore,
            "non-finite score was accepted");
    }

    auto non_finite = reference_scorer(calls);
    non_finite.candidate_scoring_function =
        [](const Phase6BcStateRepresentationV1&,
           const Phase6BcCandidateRepresentationV1&) {
            return ygo::phase6::Phase6BcCallbackResult<double>{
                std::optional<double>(std::numeric_limits<double>::quiet_NaN()),
                std::nullopt};
        };
    require_error(ygo::phase6::score_encoded_model_input_v1(input, non_finite),
                  Phase6BcScorerErrorCode::NonFiniteScore,
                  "non-finite pipeline score was accepted");
}

void test_identical_encoded_inputs_have_identical_reference_results() {
    const auto left = encoded_input(4);
    const auto right = left;
    std::vector<std::size_t> left_calls;
    std::vector<std::size_t> right_calls;
    const auto left_result = ygo::phase6::score_encoded_model_input_v1(
        left, reference_scorer(left_calls));
    const auto right_result = ygo::phase6::score_encoded_model_input_v1(
        right, reference_scorer(right_calls));
    require(left_result && right_result && left_result.value.has_value() &&
                right_result.value.has_value() && left_calls == right_calls &&
                left_result.value->scores == right_result.value->scores &&
                left_result.value->selected_candidate_ordinal ==
                    right_result.value->selected_candidate_ordinal &&
                left_result.value->selected_public_action_key ==
                    right_result.value->selected_public_action_key,
            "identical encoded public inputs produced different reference results");
}

}  // namespace

int main() {
    try {
        test_state_input_cannot_expose_candidate_domain();
        test_state_input_rebuilds_locator_table_without_candidate_locators();
        test_pipeline_preserves_order_and_returns_one_score_per_candidate();
        test_ties_use_bytewise_public_key_order();
        test_capacity_boundaries_are_exact_and_fail_closed();
        test_missing_and_failing_callbacks_fail_closed();
        test_invalid_encoded_input_and_scores_fail_closed();
        test_identical_encoded_inputs_have_identical_reference_results();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
