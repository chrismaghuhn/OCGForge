#include "ygo/environment/public_environment_observation.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "ygo/trace/sha256.hpp"

namespace ygo::environment {
namespace {

void append_u8(std::vector<std::uint8_t>& bytes, const std::uint8_t value) {
    bytes.push_back(value);
}

void append_u32be(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_u64be(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_count(std::vector<std::uint8_t>& bytes, const std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("public observation field exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
    append_count(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

bool is_lower_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

bool is_locator(const std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return character >= 0x20 && character != 0x7f;
           });
}

void validate_input(const PublicEnvironmentObservationInput& input) {
    if (input.perspective_player > 1) {
        throw std::invalid_argument("public observation perspective is invalid");
    }
    if (input.canonical_public_state_bytes.empty()) {
        throw std::invalid_argument("public observation state bytes are empty");
    }
    if (input.decision_context.kind.has_value() &&
        !is_lower_token(*input.decision_context.kind)) {
        throw std::invalid_argument("public observation request kind is not canonical");
    }
    if (input.decision_context.player.has_value() && *input.decision_context.player > 1) {
        throw std::invalid_argument("public observation acting player is invalid");
    }
    for (std::size_t index = 0; index < input.decision_context.referenced_entities.size(); ++index) {
        const auto& locator = input.decision_context.referenced_entities[index].value;
        if (!is_locator(locator)) {
            throw std::invalid_argument("public observation contains an invalid locator");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (locator == input.decision_context.referenced_entities[previous].value) {
                throw std::invalid_argument("public observation contains a duplicate locator");
            }
        }
    }
}

}  // namespace

PublicEnvironmentObservationInput project_public_observation(
    const ygo::observation::PlayerObservation& observation,
    std::string canonical_public_state_bytes) {
    PublicEnvironmentObservationInput result;
    result.perspective_player = observation.perspective_player;
    result.decision_index = observation.decision_index;
    result.canonical_public_state_bytes = std::move(canonical_public_state_bytes);

    // This is the complete public decision-context projection. In particular,
    // decision_id, engine_step_index, engine message internals, continuation_id,
    // and the v1 observation_hash are intentionally not copied.
    result.decision_context.kind = observation.decision_context.kind;
    result.decision_context.player = observation.decision_context.player;
    result.decision_context.referenced_entities = observation.decision_context.referenced_entities;
    return result;
}

std::vector<std::uint8_t> canonical_public_environment_observation_bytes(
    const PublicEnvironmentObservationInput& input) {
    validate_input(input);

    auto references = input.decision_context.referenced_entities;
    std::sort(references.begin(), references.end(), [](const auto& left, const auto& right) {
        return left.value < right.value;
    });

    std::vector<std::uint8_t> bytes;
    bytes.reserve(128 + input.canonical_public_state_bytes.size() + references.size() * 32);
    append_string(bytes, kPublicEnvironmentObservationSchemaId);
    append_string(bytes, kPublicEnvironmentObservationSchemaId);
    append_u8(bytes, input.perspective_player);
    append_u64be(bytes, input.decision_index);
    append_string(bytes, input.canonical_public_state_bytes);
    append_u8(bytes, input.decision_context.kind.has_value() ? 1 : 0);
    if (input.decision_context.kind.has_value()) {
        append_string(bytes, *input.decision_context.kind);
    }
    append_u8(bytes, input.decision_context.player.has_value() ? 1 : 0);
    if (input.decision_context.player.has_value()) {
        append_u8(bytes, *input.decision_context.player);
    }
    append_count(bytes, references.size());
    for (const auto& locator : references) {
        append_string(bytes, locator.value);
    }
    return bytes;
}

std::string public_observation_digest(const PublicEnvironmentObservationInput& input) {
    return trace::sha256_bytes(canonical_public_environment_observation_bytes(input));
}

}  // namespace ygo::environment
