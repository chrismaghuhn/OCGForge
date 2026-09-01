#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"

namespace ygo::phase6 {

enum class ModelInputInspectionErrorCode : std::uint8_t {
    InvalidLogicalModelInput,
    InvalidEncodedModelInput,
    ModelInputMismatch,
    InvalidSelectedOrdinal,
    InternalFailure,
};

struct ModelInputInspectionError final {
    ModelInputInspectionErrorCode code =
        ModelInputInspectionErrorCode::InternalFailure;
    std::string diagnostic;
};

struct ModelInputInspectionResult final {
    std::optional<std::string> value;
    std::optional<ModelInputInspectionError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

ModelInputInspectionResult inspect_public_model_input_v1(
    const model::LogicalModelInputV1& logical,
    const model::EncodedModelInputV1& encoded,
    std::optional<std::uint32_t> selected_candidate_ordinal = {}) noexcept;

}  // namespace ygo::phase6
