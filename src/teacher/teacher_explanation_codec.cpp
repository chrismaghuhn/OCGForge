#include "ygo/teacher/teacher_explanation_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::teacher {
namespace {

template <typename T>
trajectory::DecodeResult<T> failure(std::string message) noexcept {
    trajectory::DecodeResult<T> result;
    result.error = trajectory::DecodeError{std::move(message)};
    return result;
}

template <typename T>
trajectory::DecodeResult<T> success(T value) noexcept {
    trajectory::DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

bool canonical_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '_' || byte == '-')) {
            return false;
        }
    }
    return value.front() != '.' && value.back() != '.' &&
           value.find("..") == std::string_view::npos;
}

bool valid_confidence(const ConfidenceClass value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(ConfidenceClass::Fallback);
}

bool valid_fallback_level(const TeacherFallbackLevel value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(TeacherFallbackLevel::F4);
}

bool valid_id_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_fact_vector(const std::vector<PublicFactValue>& values) {
    const auto& registry = PublicFactRegistry::canonical();
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!registry.validate(values[index])) {
            return false;
        }
        if (index > 0 &&
            (values[index - 1].fact_id == values[index].fact_id ||
             !(canonical_public_fact_value_bytes(values[index - 1]) <
               canonical_public_fact_value_bytes(values[index])))) {
            return false;
        }
    }
    return true;
}

std::int64_t signed_from_bits(const std::uint64_t bits) noexcept {
    if (bits <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(bits);
    }
    const auto magnitude = (~bits) + 1;
    if (magnitude == (std::uint64_t{1} << 63)) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return -static_cast<std::int64_t>(magnitude);
}

void write_score(trajectory::ByteWriter& writer, const ScoreVector& value) {
    for (const auto component : value.values) {
        writer.u64be(static_cast<std::uint64_t>(component));
    }
}

bool read_score(trajectory::ByteReader& reader, ScoreVector& value) noexcept {
    for (auto& component : value.values) {
        std::uint64_t bits = 0;
        if (!reader.u64be(bits)) {
            return false;
        }
        component = signed_from_bits(bits);
    }
    return true;
}

void write_optional_score(trajectory::ByteWriter& writer,
                          const std::optional<ScoreVector>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        write_score(writer, *value);
    }
}

bool read_optional_score(trajectory::ByteReader& reader,
                         std::optional<ScoreVector>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    ScoreVector score;
    if (!read_score(reader, score)) {
        return false;
    }
    value = score;
    return true;
}

void write_optional_string(trajectory::ByteWriter& writer,
                           const std::optional<std::string>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        writer.string(*value);
    }
}

bool read_optional_string(trajectory::ByteReader& reader,
                          std::optional<std::string>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::string decoded;
    if (!reader.string(decoded)) {
        return false;
    }
    value = std::move(decoded);
    return true;
}

void write_string_vector(trajectory::ByteWriter& writer,
                         const std::vector<std::string>& values) {
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("teacher explanation string vector exceeds u32 count");
    }
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value);
    }
}

bool read_string_vector(trajectory::ByteReader& reader,
                        std::vector<std::string>& values) noexcept {
    std::uint32_t count = 0;
    if (!reader.u32be(count) || count > reader.remaining()) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string value;
        if (!reader.string(value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

void write_fact(trajectory::ByteWriter& writer, const PublicFactValue& value) {
    writer.string(value.fact_id);
    writer.u8(static_cast<std::uint8_t>(value.value_kind));
    switch (value.value_kind) {
    case PublicFactValueKind::Boolean:
        writer.boolean(value.boolean_value);
        break;
    case PublicFactValueKind::U64:
        writer.u64be(value.u64_value);
        break;
    case PublicFactValueKind::I32:
        writer.i32(value.i32_value);
        break;
    case PublicFactValueKind::Token:
        writer.string(value.token_value);
        break;
    }
    writer.u8(static_cast<std::uint8_t>(value.validity_scope));
}

bool read_fact(trajectory::ByteReader& reader, PublicFactValue& value) noexcept {
    std::uint8_t kind = 0;
    if (!reader.string(value.fact_id) || !reader.u8(kind) || kind > 3) {
        return false;
    }
    value.value_kind = static_cast<PublicFactValueKind>(kind);
    switch (value.value_kind) {
    case PublicFactValueKind::Boolean:
        if (!reader.boolean(value.boolean_value)) {
            return false;
        }
        break;
    case PublicFactValueKind::U64:
        if (!reader.u64be(value.u64_value)) {
            return false;
        }
        break;
    case PublicFactValueKind::I32:
        if (!reader.i32(value.i32_value)) {
            return false;
        }
        break;
    case PublicFactValueKind::Token:
        if (!reader.string(value.token_value)) {
            return false;
        }
        break;
    }
    std::uint8_t scope = 0;
    if (!reader.u8(scope) || scope > 1) {
        return false;
    }
    value.validity_scope = static_cast<PublicFactValidityScope>(scope);
    return true;
}

void write_fact_vector(trajectory::ByteWriter& writer,
                       const std::vector<PublicFactValue>& values) {
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("teacher explanation fact vector exceeds u32 count");
    }
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        write_fact(writer, value);
    }
}

bool read_fact_vector(trajectory::ByteReader& reader,
                      std::vector<PublicFactValue>& values) noexcept {
    std::uint32_t count = 0;
    if (!reader.u32be(count) || count > reader.remaining()) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        PublicFactValue value;
        if (!read_fact(reader, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

}  // namespace

bool validate_teacher_decision_explanation(
    const TeacherDecisionExplanation& value) noexcept {
    try {
        return environment::is_public_action_key(value.selected_public_action_key) &&
               (!value.active_goal_id.has_value() ||
                canonical_token(*value.active_goal_id)) &&
               (!value.active_line_id.has_value() ||
                canonical_token(*value.active_line_id)) &&
               (!value.active_line_node_id.has_value() ||
                canonical_token(*value.active_line_node_id)) &&
               valid_id_vector(value.matched_intent_ids) &&
               valid_id_vector(value.invalidation_reason_ids) &&
               std::all_of(value.invalidation_reason_ids.begin(),
                           value.invalidation_reason_ids.end(), [](const auto& reason) {
                               return is_registered_invalidation_reason(reason);
                           }) &&
               valid_fact_vector(value.relevant_public_feature_values) &&
               valid_confidence(value.confidence_class) &&
               valid_fallback_level(value.fallback_level) &&
               (value.fallback_level != TeacherFallbackLevel::F4 ||
                value.confidence_class == ConfidenceClass::Fallback) &&
               value.explanation_schema_id == kTeacherDiagnosticContractId;
    } catch (...) {
        return false;
    }
}

std::vector<std::uint8_t> canonical_teacher_decision_explanation_bytes(
    const TeacherDecisionExplanation& value) {
    if (!validate_teacher_decision_explanation(value)) {
        throw std::invalid_argument("teacher decision explanation is not canonical");
    }
    trajectory::ByteWriter writer;
    writer.string(kTeacherDiagnosticContractId);
    writer.string(kTeacherDiagnosticContractId);
    writer.string(value.selected_public_action_key);
    write_score(writer, value.selected_score_vector);
    write_optional_score(writer, value.runner_up_score_vector);
    writer.u8(static_cast<std::uint8_t>(value.confidence_class));
    writer.u8(static_cast<std::uint8_t>(value.fallback_level));
    write_optional_string(writer, value.active_goal_id);
    write_optional_string(writer, value.active_line_id);
    write_optional_string(writer, value.active_line_node_id);
    write_string_vector(writer, value.matched_intent_ids);
    write_string_vector(writer, value.invalidation_reason_ids);
    write_fact_vector(writer, value.relevant_public_feature_values);
    writer.string(value.explanation_schema_id);
    return std::move(writer).take();
}

trajectory::DecodeResult<TeacherDecisionExplanation>
decode_teacher_decision_explanation(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        TeacherDecisionExplanation value;
        std::string domain;
        std::string schema;
        std::uint8_t confidence = 0;
        std::uint8_t fallback = 0;
        if (!reader.string(domain) || domain != kTeacherDiagnosticContractId ||
            !reader.string(schema) || schema != kTeacherDiagnosticContractId ||
            !reader.string(value.selected_public_action_key) ||
            !read_score(reader, value.selected_score_vector) ||
            !read_optional_score(reader, value.runner_up_score_vector) ||
            !reader.u8(confidence) || confidence > 3 || !reader.u8(fallback) || fallback > 4 ||
            !read_optional_string(reader, value.active_goal_id) ||
            !read_optional_string(reader, value.active_line_id) ||
            !read_optional_string(reader, value.active_line_node_id) ||
            !read_string_vector(reader, value.matched_intent_ids) ||
            !read_string_vector(reader, value.invalidation_reason_ids) ||
            !read_fact_vector(reader, value.relevant_public_feature_values) ||
            !reader.string(value.explanation_schema_id) || !reader.at_end()) {
            return failure<TeacherDecisionExplanation>(
                "malformed teacher decision explanation");
        }
        value.confidence_class = static_cast<ConfidenceClass>(confidence);
        value.fallback_level = static_cast<TeacherFallbackLevel>(fallback);
        if (!validate_teacher_decision_explanation(value)) {
            return failure<TeacherDecisionExplanation>(
                "invalid teacher decision explanation");
        }
        if (canonical_teacher_decision_explanation_bytes(value) != bytes) {
            return failure<TeacherDecisionExplanation>(
                "noncanonical teacher decision explanation");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<TeacherDecisionExplanation>(error.what());
    } catch (...) {
        return failure<TeacherDecisionExplanation>(
            "teacher decision explanation decode threw");
    }
}

}  // namespace ygo::teacher
