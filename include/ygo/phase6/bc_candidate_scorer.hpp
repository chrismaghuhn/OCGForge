#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/encoded_model_input.hpp"

namespace ygo::phase6 {

inline constexpr std::string_view kPhase6BcCandidateScorerContractId =
    "ocgforge.phase6.bc_candidate_scorer.v1";
inline constexpr std::string_view kPhase6BcInferenceTiebreakContractId =
    "ocgforge.phase6.bc.inference_tiebreak.v1";

// State-only encoded input. Candidate rows, routing keys, the candidate-domain
// digest, and candidate ordinals are intentionally not representable here.
inline constexpr std::string_view kPhase6BcStateInputSchemaId =
    "ocgforge.phase6.bc_state_input.v1";

struct Phase6BcStateInputV1 final {
    std::string schema_id = std::string(kPhase6BcStateInputSchemaId);
    std::string card_vocabulary_identity;
    std::string public_observation_digest;
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::vector<std::string> public_locator_table;
    std::optional<std::uint16_t> public_observation_context_kind_code;
    std::optional<std::uint8_t> public_observation_context_player;
    std::vector<std::uint32_t> observation_context_reference_ordinals;
    model::EncodedGlobals globals;
    std::vector<model::EncodedZone> zones;
    std::vector<model::EncodedEntity> entities;
    std::vector<model::EncodedRelationship> relationships;
    model::EncodedChainState chain;
    std::vector<model::EncodedVisibleEvent> visible_events;
    model::EncodedMatchContext match_context;
};

// These are callback-owned reference execution values. They have no
// canonical semantic identity and do not prescribe a neural architecture.
struct Phase6BcStateRepresentationV1 final {
    std::vector<std::uint64_t> values;
};

struct Phase6BcCandidateRepresentationV1 final {
    std::vector<std::uint64_t> values;
};

struct Phase6BcCallbackError final {
    std::string diagnostic;
};

template <typename T>
struct Phase6BcCallbackResult final {
    std::optional<T> value;
    std::optional<Phase6BcCallbackError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

using Phase6BcStateEncoderV1 =
    std::function<Phase6BcCallbackResult<Phase6BcStateRepresentationV1>(
        const Phase6BcStateInputV1&)>;
using Phase6BcCandidateEncoderV1 =
    std::function<Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>(
        const Phase6BcStateRepresentationV1&, const model::EncodedCandidate&)>;
using Phase6BcCandidateScoringFunctionV1 =
    std::function<Phase6BcCallbackResult<double>(
        const Phase6BcStateRepresentationV1&,
        const Phase6BcCandidateRepresentationV1&)>;

struct Phase6BcReferenceScorerV1 final {
    Phase6BcStateEncoderV1 state_encoder;
    Phase6BcCandidateEncoderV1 candidate_encoder;
    Phase6BcCandidateScoringFunctionV1 candidate_scoring_function;

    // This is a physical execution capacity only. An absent value means that
    // this reference boundary imposes no additional physical-width limit.
    std::optional<std::uint64_t> physical_candidate_capacity;
};

enum class Phase6BcScorerErrorCode : std::uint8_t {
    InvalidEncodedModelInput,
    CandidateCapacityTooSmall,
    MissingStateEncoder,
    StateEncoderFailure,
    MissingCandidateEncoder,
    CandidateEncoderFailure,
    MissingCandidateScoringFunction,
    CandidateScoringFailure,
    ScoreCountMismatch,
    NonFiniteScore,
    SelectionFailure,
    InternalFailure,
};

struct Phase6BcScorerError final {
    Phase6BcScorerErrorCode code = Phase6BcScorerErrorCode::InternalFailure;
    std::string diagnostic;
};

template <typename T>
struct Phase6BcResult final {
    std::optional<T> value;
    std::optional<Phase6BcScorerError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

struct Phase6BcSelectionV1 final {
    std::uint32_t selected_candidate_ordinal = 0;
    std::string selected_public_action_key;
};

struct Phase6BcInferenceV1 final {
    // Reference execution scores only. Their numeric representation is not a
    // canonical response/identity codec and remains subject to the later
    // versioned Phase-6 numeric contract.
    std::vector<double> scores;
    std::uint32_t selected_candidate_ordinal = 0;
    std::string selected_public_action_key;
};

using Phase6BcSelectionResult = Phase6BcResult<Phase6BcSelectionV1>;
using Phase6BcInferenceResult = Phase6BcResult<Phase6BcInferenceV1>;

std::string_view phase6_bc_scorer_error_code_name(
    Phase6BcScorerErrorCode code) noexcept;

// Validates the encoded input, invokes state_encoder once, then invokes the
// candidate encoder and scoring function once for each exact source-order
// candidate. No public action key is supplied to either candidate callback.
Phase6BcInferenceResult score_encoded_model_input_v1(
    const model::EncodedModelInputV1& encoded,
    const Phase6BcReferenceScorerV1& scorer) noexcept;

// Validates an exact source-order score vector and resolves one existing key
// from the encoded routing sidecar. Scores are execution/diagnostic values;
// this function does not create an inference response identity.
Phase6BcSelectionResult select_phase6_candidate_v1(
    const model::EncodedModelInputV1& encoded,
    const std::vector<double>& scores) noexcept;

}  // namespace ygo::phase6
