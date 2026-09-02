#include "ygo/phase6/bc_candidate_scorer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ygo::phase6 {
namespace {

class ScorerFailure final {
public:
    explicit ScorerFailure(const Phase6BcScorerErrorCode code) : code_(code) {}

    Phase6BcScorerErrorCode code() const noexcept { return code_; }

private:
    Phase6BcScorerErrorCode code_;
};

[[noreturn]] void fail(const Phase6BcScorerErrorCode code) {
    throw ScorerFailure(code);
}

bool byte_less(const std::string_view left, const std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](const char left_byte, const char right_byte) {
            return static_cast<unsigned char>(left_byte) <
                   static_cast<unsigned char>(right_byte);
        });
}

void validate_encoded_input(const model::EncodedModelInputV1& encoded) {
    try {
        (void)model::canonical_encoded_model_input_bytes(encoded);
    } catch (const std::invalid_argument&) {
        fail(Phase6BcScorerErrorCode::InvalidEncodedModelInput);
    }
}

void validate_capacity(const model::EncodedModelInputV1& encoded,
                       const std::optional<std::uint64_t> capacity) {
    if (capacity.has_value() &&
        *capacity < static_cast<std::uint64_t>(encoded.candidate_count())) {
        fail(Phase6BcScorerErrorCode::CandidateCapacityTooSmall);
    }
}

Phase6BcScorerError make_error(const Phase6BcScorerErrorCode code) {
    return {code, std::string(phase6_bc_scorer_error_code_name(code))};
}

template <typename T>
Phase6BcResult<T> failure(const Phase6BcScorerErrorCode code) {
    return {std::nullopt, make_error(code)};
}

Phase6BcSelectionV1 select_unchecked(const model::EncodedModelInputV1& encoded,
                                     const std::vector<double>& scores) {
    if (scores.size() != encoded.candidate_count()) {
        fail(Phase6BcScorerErrorCode::ScoreCountMismatch);
    }
    for (const double score : scores) {
        if (!std::isfinite(score)) fail(Phase6BcScorerErrorCode::NonFiniteScore);
    }

    std::size_t best = 0;
    for (std::size_t index = 1; index < scores.size(); ++index) {
        const bool higher = scores[index] > scores[best];
        const bool equal_and_lower_key =
            scores[index] == scores[best] &&
            byte_less(encoded.routing_keys[index], encoded.routing_keys[best]);
        if (higher || equal_and_lower_key) best = index;
    }

    if (best > std::numeric_limits<std::uint32_t>::max()) {
        fail(Phase6BcScorerErrorCode::SelectionFailure);
    }
    return {static_cast<std::uint32_t>(best), encoded.routing_keys[best]};
}

}  // namespace

std::string_view phase6_bc_scorer_error_code_name(
    const Phase6BcScorerErrorCode code) noexcept {
    switch (code) {
    case Phase6BcScorerErrorCode::InvalidEncodedModelInput:
        return "invalid_encoded_model_input";
    case Phase6BcScorerErrorCode::CandidateCapacityTooSmall:
        return "candidate_capacity_too_small";
    case Phase6BcScorerErrorCode::MissingStateEncoder:
        return "missing_state_encoder";
    case Phase6BcScorerErrorCode::StateEncoderFailure:
        return "state_encoder_failure";
    case Phase6BcScorerErrorCode::MissingCandidateEncoder:
        return "missing_candidate_encoder";
    case Phase6BcScorerErrorCode::CandidateEncoderFailure:
        return "candidate_encoder_failure";
    case Phase6BcScorerErrorCode::MissingCandidateScoringFunction:
        return "missing_candidate_scoring_function";
    case Phase6BcScorerErrorCode::CandidateScoringFailure:
        return "candidate_scoring_failure";
    case Phase6BcScorerErrorCode::ScoreCountMismatch:
        return "score_count_mismatch";
    case Phase6BcScorerErrorCode::NonFiniteScore:
        return "non_finite_score";
    case Phase6BcScorerErrorCode::SelectionFailure:
        return "selection_failure";
    case Phase6BcScorerErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

Phase6BcSelectionResult select_phase6_candidate_v1(
    const model::EncodedModelInputV1& encoded,
    const std::vector<double>& scores) noexcept {
    try {
        validate_encoded_input(encoded);
        return {std::optional<Phase6BcSelectionV1>(select_unchecked(encoded, scores)),
                std::nullopt};
    } catch (const ScorerFailure& error) {
        return failure<Phase6BcSelectionV1>(error.code());
    } catch (const std::bad_alloc&) {
        return failure<Phase6BcSelectionV1>(Phase6BcScorerErrorCode::InternalFailure);
    } catch (...) {
        return failure<Phase6BcSelectionV1>(Phase6BcScorerErrorCode::SelectionFailure);
    }
}

Phase6BcInferenceResult score_encoded_model_input_v1(
    const model::EncodedModelInputV1& encoded,
    const Phase6BcReferenceScorerV1& scorer) noexcept {
    try {
        validate_encoded_input(encoded);
        validate_capacity(encoded, scorer.physical_candidate_capacity);
        if (!scorer.state_encoder) fail(Phase6BcScorerErrorCode::MissingStateEncoder);
        if (!scorer.candidate_encoder) {
            fail(Phase6BcScorerErrorCode::MissingCandidateEncoder);
        }
        if (!scorer.candidate_scoring_function) {
            fail(Phase6BcScorerErrorCode::MissingCandidateScoringFunction);
        }

        Phase6BcStateRepresentationV1 state;
        try {
            const auto result = scorer.state_encoder(encoded);
            if (!result || !result.value.has_value()) {
                fail(Phase6BcScorerErrorCode::StateEncoderFailure);
            }
            state = *result.value;
        } catch (const ScorerFailure&) {
            throw;
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            fail(Phase6BcScorerErrorCode::StateEncoderFailure);
        }

        Phase6BcInferenceV1 output;
        output.scores.reserve(encoded.candidate_count());
        for (std::size_t index = 0; index < encoded.candidate_count(); ++index) {
            Phase6BcCandidateRepresentationV1 candidate;
            try {
                const auto result = scorer.candidate_encoder(
                    state, encoded.candidate_features[index]);
                if (!result || !result.value.has_value()) {
                    fail(Phase6BcScorerErrorCode::CandidateEncoderFailure);
                }
                candidate = *result.value;
            } catch (const ScorerFailure&) {
                throw;
            } catch (const std::bad_alloc&) {
                throw;
            } catch (...) {
                fail(Phase6BcScorerErrorCode::CandidateEncoderFailure);
            }

            try {
                const auto result = scorer.candidate_scoring_function(state, candidate);
                if (!result || !result.value.has_value()) {
                    fail(Phase6BcScorerErrorCode::CandidateScoringFailure);
                }
                output.scores.push_back(*result.value);
            } catch (const ScorerFailure&) {
                throw;
            } catch (const std::bad_alloc&) {
                throw;
            } catch (...) {
                fail(Phase6BcScorerErrorCode::CandidateScoringFailure);
            }
        }

        const auto selection = select_unchecked(encoded, output.scores);
        output.selected_candidate_ordinal = selection.selected_candidate_ordinal;
        output.selected_public_action_key = std::move(selection.selected_public_action_key);
        return {std::optional<Phase6BcInferenceV1>(std::move(output)), std::nullopt};
    } catch (const ScorerFailure& error) {
        return failure<Phase6BcInferenceV1>(error.code());
    } catch (const std::bad_alloc&) {
        return failure<Phase6BcInferenceV1>(Phase6BcScorerErrorCode::InternalFailure);
    } catch (...) {
        return failure<Phase6BcInferenceV1>(Phase6BcScorerErrorCode::InternalFailure);
    }
}

}  // namespace ygo::phase6
