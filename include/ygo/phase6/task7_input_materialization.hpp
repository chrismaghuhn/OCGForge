#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/model_batch_layout.hpp"

namespace ygo::phase6 {

inline constexpr std::string_view kTask7MaterializationSchemaId =
    "ocgforge.phase6.task7.input_materialization.v1";
inline constexpr std::string_view kTask7MaterializationConfigSchemaId =
    "ocgforge.phase6.task7.input_materialization_config.v1";
inline constexpr std::string_view kTask7MaterializationConfigIdentityPrefix =
    "phase6_task7_input_materialization_config.v1.";

struct Task7MaterializationSourceSampleV1 final {
    // These references are in-process mechanics only.  They are never
    // serialized, hashed, or emitted in diagnostics as identity.
    const model::LogicalModelInputV1* logical = nullptr;
    const model::EncodedModelInputV1* encoded = nullptr;
    const model::CardVocabularyV1* vocabulary = nullptr;
    std::string expected_model_input_identity;
    std::string expected_card_vocabulary_identity;
};

struct Task7MaterializationSourceBatchV1 final {
    // The ragged layout is an execution view; the associated logical/encoded
    // values remain the semantic source authority.
    const model::RaggedModelBatchV1* ragged = nullptr;
    std::vector<Task7MaterializationSourceSampleV1> samples;
};

struct Task7MaterializedSampleV1 final {
    std::string model_input_identity;
    std::string card_vocabulary_identity;
    std::string public_observation_digest;
    std::optional<std::string> public_candidate_domain_digest;
    std::uint32_t candidate_count = 0;
    std::vector<std::uint8_t> canonical_bytes;
};

struct Task7MaterializedBatchV1 final {
    std::string schema_id = std::string(kTask7MaterializationSchemaId);
    std::string configuration_identity;
    std::vector<Task7MaterializedSampleV1> samples;
};

enum class Task7MaterializationErrorCode : std::uint8_t {
    UnknownSchema,
    SourceAssociationMismatch,
    ModelInputIdentityMismatch,
    CardVocabularyMismatch,
    RaggedReconstructionMismatch,
    InvalidOffset,
    OffsetOverflow,
    CandidateCountMismatch,
    RoutingSidecarMismatch,
    OptionalPresenceMismatch,
    ReferenceTypeMismatch,
    ChainStateMismatch,
    InvalidLimb,
    InvalidBoolean,
    InvalidPadding,
    PadOnRealRow,
    ForbiddenSource,
    CanonicalizationFailure,
    InternalFailure,
};

struct Task7MaterializationError final {
    Task7MaterializationErrorCode code =
        Task7MaterializationErrorCode::InternalFailure;
    std::string diagnostic;
};

struct Task7MaterializationResult final {
    std::optional<Task7MaterializedBatchV1> value;
    std::optional<Task7MaterializationError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view task7_materialization_error_code_name(
    Task7MaterializationErrorCode code) noexcept;

std::vector<std::uint8_t> canonical_task7_materialization_config_bytes();
std::string task7_materialization_config_identity();

std::array<std::uint16_t, 1> task7_u8_limbs(std::uint8_t value) noexcept;
std::array<std::uint16_t, 1> task7_u16_limbs(std::uint16_t value) noexcept;
std::array<std::uint16_t, 2> task7_u32_limbs(std::uint32_t value) noexcept;
std::array<std::uint16_t, 4> task7_u64_limbs(std::uint64_t value) noexcept;
std::array<std::uint16_t, 2> task7_i32_limbs(std::int32_t value) noexcept;

Task7MaterializationResult materialize_task7_input_v1(
    const Task7MaterializationSourceBatchV1& source) noexcept;

inline Task7MaterializationResult materialize_task7_input(
    const Task7MaterializationSourceBatchV1& source) noexcept {
    return materialize_task7_input_v1(source);
}

}  // namespace ygo::phase6
