#include "ygo/environment/public_action_identity.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

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
        throw std::length_error("public identity field exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
    append_count(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void append_optional_u32(std::vector<std::uint8_t>& bytes,
                         const std::optional<std::uint32_t>& value) {
    append_u8(bytes, value.has_value() ? 1 : 0);
    if (value.has_value()) {
        append_u32be(bytes, *value);
    }
}

void append_optional_u8(std::vector<std::uint8_t>& bytes,
                        const std::optional<std::uint8_t>& value) {
    append_u8(bytes, value.has_value() ? 1 : 0);
    if (value.has_value()) {
        append_u8(bytes, *value);
    }
}

void append_optional_i32(std::vector<std::uint8_t>& bytes,
                         const std::optional<std::int32_t>& value) {
    append_u8(bytes, value.has_value() ? 1 : 0);
    if (value.has_value()) {
        append_u32be(bytes, static_cast<std::uint32_t>(*value));
    }
}

bool is_valid_choice(const std::uint8_t kind,
                     const std::uint64_t value,
                     const bool has_response_index) noexcept {
    switch (static_cast<PublicChoiceKind>(kind)) {
    case PublicChoiceKind::YesNo:
    case PublicChoiceKind::EffectYesNo:
        return value <= 1 && !has_response_index;
    case PublicChoiceKind::EffectChoice:
        return value <= std::numeric_limits<std::uint32_t>::max() && !has_response_index;
    case PublicChoiceKind::OptionValue:
    case PublicChoiceKind::AnnouncementNumber:
        return has_response_index;
    }
    return false;
}

void validate_choice(const PublicChoice& choice) {
    if (!is_valid_choice(static_cast<std::uint8_t>(choice.kind), choice.value,
                         choice.response_index.has_value())) {
        throw std::invalid_argument("public action choice is not a valid typed choice");
    }
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

bool is_observation_locator(const std::string_view value) noexcept {
    // Visibility and locator membership are proven by the projection against
    // PlayerObservation. The codec only rejects empty/control-bearing values
    // so it does not invent a narrower grammar than ObservationLocator.
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return character >= 0x20 && character != 0x7f;
           });
}

bool decode_lower_hex(const std::string_view encoded,
                      std::vector<std::uint8_t>& bytes) noexcept {
    if (encoded.empty() || encoded.size() % 2 != 0) {
        return false;
    }

    try {
        bytes.clear();
        bytes.reserve(encoded.size() / 2);
        for (std::size_t index = 0; index < encoded.size(); index += 2) {
            const auto high = encoded[index];
            const auto low = encoded[index + 1];
            const auto hex_digit = [](const char character) -> int {
                if (character >= '0' && character <= '9') {
                    return character - '0';
                }
                if (character >= 'a' && character <= 'f') {
                    return character - 'a' + 10;
                }
                return -1;
            };
            const auto high_digit = hex_digit(high);
            const auto low_digit = hex_digit(low);
            if (high_digit < 0 || low_digit < 0) {
                return false;
            }
            bytes.push_back(static_cast<std::uint8_t>((high_digit << 4) | low_digit));
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool read_u8(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
             std::uint8_t& value) noexcept {
    if (offset >= bytes.size()) {
        return false;
    }
    value = bytes[offset++];
    return true;
}

bool read_u32be(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                std::uint32_t& value) noexcept {
    if (bytes.size() - offset < 4) {
        return false;
    }
    value = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
            static_cast<std::uint32_t>(bytes[offset + 3]);
    offset += 4;
    return true;
}

bool read_u64be(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                std::uint64_t& value) noexcept {
    if (bytes.size() - offset < 8) {
        return false;
    }
    value = 0;
    for (int shift = 56; shift >= 0; shift -= 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::string_view& value) noexcept {
    std::uint32_t length = 0;
    if (!read_u32be(bytes, offset, length) ||
        length > bytes.size() - offset) {
        return false;
    }
    value = std::string_view(reinterpret_cast<const char*>(bytes.data() + offset), length);
    offset += length;
    return true;
}

bool read_optional_u32(const std::vector<std::uint8_t>& bytes,
                       std::size_t& offset) noexcept {
    std::uint8_t present = 0;
    if (!read_u8(bytes, offset, present) || present > 1) {
        return false;
    }
    if (present == 1) {
        std::uint32_t ignored = 0;
        return read_u32be(bytes, offset, ignored);
    }
    return true;
}

bool read_optional_u8(const std::vector<std::uint8_t>& bytes,
                      std::size_t& offset) noexcept {
    std::uint8_t present = 0;
    if (!read_u8(bytes, offset, present) || present > 1) {
        return false;
    }
    if (present == 1) {
        std::uint8_t ignored = 0;
        return read_u8(bytes, offset, ignored);
    }
    return true;
}

bool read_optional_choice(const std::vector<std::uint8_t>& bytes,
                          std::size_t& offset) noexcept {
    std::uint8_t present = 0;
    if (!read_u8(bytes, offset, present) || present > 1) {
        return false;
    }
    if (present == 0) {
        return true;
    }
    std::uint8_t kind = 0;
    std::uint64_t value = 0;
    if (!read_u8(bytes, offset, kind) || !read_u64be(bytes, offset, value)) {
        return false;
    }
    std::uint8_t response_index_present = 0;
    if (!read_u8(bytes, offset, response_index_present) || response_index_present > 1) {
        return false;
    }
    if (response_index_present == 1) {
        std::uint32_t response_index = 0;
        if (!read_u32be(bytes, offset, response_index)) {
            return false;
        }
    }
    return is_valid_choice(kind, value, response_index_present == 1);
}

bool is_canonical_public_action_bytes(const std::vector<std::uint8_t>& bytes) noexcept {
    std::size_t offset = 0;
    std::string_view value;
    if (!read_string(bytes, offset, value) || value != kPublicActionIdentitySchemaId ||
        !read_string(bytes, offset, value) || value != kPublicActionIdentitySchemaId ||
        !read_string(bytes, offset, value) || !is_lower_token(value)) {
        return false;
    }

    if (!read_optional_choice(bytes, offset)) {
        return false;
    }

    for (int reference_index = 0; reference_index < 2; ++reference_index) {
        std::uint8_t present = 0;
        if (!read_u8(bytes, offset, present) || present > 1) {
            return false;
        }
        if (present == 0) {
            continue;
        }
        std::uint8_t kind = 0;
        if (!read_u8(bytes, offset, kind) || kind > 1 ||
            !read_string(bytes, offset, value) || !is_observation_locator(value)) {
            return false;
        }
    }

    if (!read_optional_u32(bytes, offset) ||
        !read_optional_u8(bytes, offset) ||
        !read_optional_u32(bytes, offset) ||
        !read_optional_u32(bytes, offset)) {
        return false;
    }
    std::string_view continuation_operation;
    if (!read_string(bytes, offset, continuation_operation) ||
        (!continuation_operation.empty() && !is_lower_token(continuation_operation))) {
        return false;
    }
    return offset == bytes.size();
}

void validate_reference(const PublicCardReference& reference) {
    if (reference.kind != PublicCardReferenceKind::VisibleCard &&
        reference.kind != PublicCardReferenceKind::RedactedSlot) {
        throw std::invalid_argument("public card reference has an unknown kind");
    }
    if (!is_observation_locator(reference.observation_locator)) {
        throw std::invalid_argument("public card reference is not an observation locator");
    }
}

void append_reference(std::vector<std::uint8_t>& bytes,
                      const std::optional<PublicCardReference>& reference) {
    append_u8(bytes, reference.has_value() ? 1 : 0);
    if (reference.has_value()) {
        append_u8(bytes, static_cast<std::uint8_t>(reference->kind));
        append_string(bytes, reference->observation_locator);
    }
}

void append_choice(std::vector<std::uint8_t>& bytes,
                   const std::optional<PublicChoice>& choice) {
    append_u8(bytes, choice.has_value() ? 1 : 0);
    if (!choice.has_value()) {
        return;
    }
    validate_choice(*choice);
    append_u8(bytes, static_cast<std::uint8_t>(choice->kind));
    append_u64be(bytes, choice->value);
    append_u8(bytes, choice->response_index.has_value() ? 1 : 0);
    if (choice->response_index.has_value()) {
        append_u32be(bytes, *choice->response_index);
    }
}

bool is_lower_hex_digest(const std::string_view value) noexcept {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

std::string lower_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

}  // namespace

std::vector<std::uint8_t> canonical_public_action_key_bytes(
    const PublicActionKeyInput& input) {
    if (!is_lower_token(input.action_kind)) {
        throw std::invalid_argument("public action kind is not a canonical token");
    }
    if (!input.continuation_operation.empty() &&
        !is_lower_token(input.continuation_operation)) {
        throw std::invalid_argument("public continuation operation is not a canonical token");
    }
    if (input.source_reference.has_value()) {
        validate_reference(*input.source_reference);
    }
    if (input.target_reference.has_value()) {
        validate_reference(*input.target_reference);
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(224);
    append_string(bytes, kPublicActionIdentitySchemaId);
    append_string(bytes, kPublicActionIdentitySchemaId);
    append_string(bytes, input.action_kind);
    append_choice(bytes, input.choice);
    append_reference(bytes, input.source_reference);
    append_reference(bytes, input.target_reference);
    append_optional_u32(bytes, input.phase);
    append_optional_u8(bytes, input.position);
    append_optional_u32(bytes, input.source_index);
    append_optional_i32(bytes, input.amount);
    append_string(bytes, input.continuation_operation);
    return bytes;
}

std::string public_action_key(const PublicActionKeyInput& input) {
    return std::string(kPublicActionKeyPrefix) + lower_hex(canonical_public_action_key_bytes(input));
}

bool is_public_action_key(const std::string_view key) noexcept {
    if (key.size() <= kPublicActionKeyPrefix.size() ||
        key.substr(0, kPublicActionKeyPrefix.size()) != kPublicActionKeyPrefix) {
        return false;
    }
    const auto encoded = key.substr(kPublicActionKeyPrefix.size());
    std::vector<std::uint8_t> bytes;
    return decode_lower_hex(encoded, bytes) && is_canonical_public_action_bytes(bytes);
}

std::vector<std::uint8_t> canonical_public_candidate_domain_bytes(
    const std::string_view request_kind, const std::vector<std::string>& public_action_keys) {
    if (!is_lower_token(request_kind)) {
        throw std::invalid_argument("public request kind is not a canonical token");
    }
    if (public_action_keys.empty()) {
        throw std::invalid_argument("public candidate domain is empty");
    }

    for (std::size_t index = 0; index < public_action_keys.size(); ++index) {
        if (!is_public_action_key(public_action_keys[index])) {
            throw std::invalid_argument("public candidate domain contains an invalid key");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (public_action_keys[index] == public_action_keys[previous]) {
                throw std::invalid_argument("public candidate domain contains an ambiguous key");
            }
        }
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(64 + public_action_keys.size() * 96);
    append_string(bytes, kPublicCandidateDomainSchemaId);
    append_string(bytes, request_kind);
    append_count(bytes, public_action_keys.size());
    for (const auto& key : public_action_keys) {
        append_string(bytes, key);
    }
    return bytes;
}

std::string public_candidate_domain_digest(
    const std::string_view request_kind, const std::vector<std::string>& public_action_keys) {
    return trace::sha256_bytes(
        canonical_public_candidate_domain_bytes(request_kind, public_action_keys));
}

std::vector<std::uint8_t> canonical_public_semantic_decision_identity_bytes(
    const PublicSemanticDecisionIdentityInput& input) {
    if (!is_lower_hex_digest(input.episode_semantic_id) || input.acting_player > 1 ||
        !is_lower_token(input.request_kind) ||
        !is_lower_hex_digest(input.public_observation_digest) ||
        !is_lower_hex_digest(input.public_candidate_domain_digest)) {
        throw std::invalid_argument("public semantic decision identity contains an invalid field");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(320);
    append_string(bytes, kPublicSemanticDecisionIdentitySchemaId);
    append_string(bytes, kPublicSemanticDecisionIdentitySchemaId);
    append_string(bytes, input.episode_semantic_id);
    append_u64be(bytes, input.decision_index);
    append_u8(bytes, input.acting_player);
    append_string(bytes, input.request_kind);
    append_string(bytes, input.public_observation_digest);
    append_string(bytes, input.public_candidate_domain_digest);
    return bytes;
}

std::string public_semantic_decision_id(const PublicSemanticDecisionIdentityInput& input) {
    return trace::sha256_bytes(canonical_public_semantic_decision_identity_bytes(input));
}

namespace detail {

std::optional<std::string> resolve_public_action_key(
    const std::vector<PublicActionBinding>& bindings, const std::string_view public_key) {
    if (!is_public_action_key(public_key)) {
        return std::nullopt;
    }

    std::optional<std::string> resolved;
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const auto& binding = bindings[index];
        if (!is_public_action_key(binding.public_action_key) ||
            binding.internal_semantic_key.empty()) {
            return std::nullopt;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (binding.public_action_key == bindings[previous].public_action_key) {
                return std::nullopt;
            }
        }
        if (binding.public_action_key != public_key) {
            continue;
        }
        resolved = binding.internal_semantic_key;
    }
    return resolved;
}

}  // namespace detail

}  // namespace ygo::environment
