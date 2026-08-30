#include "ygo/environment/public_environment_observation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/public_safe_state.hpp"

namespace ygo::environment {
namespace {

bool valid_utf8(const std::string_view value) noexcept {
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        std::size_t width = 0;
        std::uint32_t code_point = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
            code_point = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            code_point = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            code_point = first & 0x07;
        } else {
            return false;
        }
        if (width > value.size() - index) {
            return false;
        }
        for (std::size_t continuation = 1; continuation < width; ++continuation) {
            const auto byte = static_cast<unsigned char>(value[index + continuation]);
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (byte & 0x3f);
        }
        if ((width == 3 && code_point < 0x800) ||
            (width == 4 && code_point < 0x10000) || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        index += width;
    }
    return true;
}

bool lower_token(const std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return (character >= 'a' && character <= 'z') ||
                      (character >= '0' && character <= '9') || character == '_';
           });
}

bool locator(const std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return character >= 0x20 && character != 0x7f;
           });
}

class Cursor final {
public:
    explicit Cursor(const std::vector<std::uint8_t>& bytes) noexcept : bytes_(bytes) {}

    bool u8(std::uint8_t& value) noexcept {
        if (position_ >= bytes_.size()) {
            return false;
        }
        value = bytes_[position_++];
        return true;
    }

    bool u64(std::uint64_t& value) noexcept {
        if (bytes_.size() - position_ < 8) {
            return false;
        }
        value = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
        }
        return true;
    }

    bool u32(std::uint32_t& value) noexcept {
        if (bytes_.size() - position_ < 4) {
            return false;
        }
        value = (static_cast<std::uint32_t>(bytes_[position_]) << 24) |
                (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 16) |
                (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 8) |
                static_cast<std::uint32_t>(bytes_[position_ + 3]);
        position_ += 4;
        return true;
    }

    bool string(std::string& value) noexcept {
        std::uint32_t length = 0;
        if (!u32(length) || length > bytes_.size() - position_) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + position_), length);
        position_ += length;
        return valid_utf8(value);
    }

    bool raw(std::vector<std::uint8_t>& value) noexcept {
        std::uint32_t length = 0;
        if (!u32(length) || length > bytes_.size() - position_) {
            return false;
        }
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + length));
        position_ += length;
        return true;
    }

    bool end() const noexcept { return position_ == bytes_.size(); }
    std::size_t remaining() const noexcept { return bytes_.size() - position_; }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

}  // namespace

bool decode_canonical_public_environment_observation(
    const std::vector<std::uint8_t>& bytes,
    PublicEnvironmentObservationInput& output) noexcept {
    try {
        Cursor cursor(bytes);
        std::string schema;
        if (!cursor.string(schema) || schema != kPublicEnvironmentObservationSchemaId ||
            !cursor.string(schema) || schema != kPublicEnvironmentObservationSchemaId) {
            return false;
        }
        std::uint8_t perspective = 0;
        std::uint64_t decision_index = 0;
        std::vector<std::uint8_t> safe_state;
        if (!cursor.u8(perspective) || perspective > 1 || !cursor.u64(decision_index) ||
            !cursor.raw(safe_state)) {
            return false;
        }
        const auto decoded_safe_state = decode_canonical_public_safe_state(safe_state);
        if (!decoded_safe_state ||
            decoded_safe_state.value->match_context().perspective_player != perspective) {
            return false;
        }

        PublicObservationDecisionContext context;
        std::uint8_t present = 0;
        if (!cursor.u8(present) || present > 1) {
            return false;
        }
        if (present == 1) {
            std::string kind;
            if (!cursor.string(kind) || !lower_token(kind)) {
                return false;
            }
            context.kind = std::move(kind);
        }
        if (!cursor.u8(present) || present > 1) {
            return false;
        }
        if (present == 1) {
            std::uint8_t player = 0;
            if (!cursor.u8(player) || player > 1) {
                return false;
            }
            context.player = player;
        }
        std::uint32_t count = 0;
        if (!cursor.u32(count) || count > cursor.remaining() / 4) {
            return false;
        }
        std::string previous;
        bool have_previous = false;
        context.referenced_entities.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            std::string reference;
            if (!cursor.string(reference) || !locator(reference) ||
                (have_previous && reference <= previous)) {
                return false;
            }
            previous = reference;
            have_previous = true;
            context.referenced_entities.push_back({std::move(reference)});
        }
        if (!cursor.end()) {
            return false;
        }
        output = PublicEnvironmentObservationInput{};
        output.perspective_player = perspective;
        output.decision_index = decision_index;
        output.decision_context = std::move(context);
        output.canonical_safe_state_bytes_ = std::move(safe_state);
        return canonical_public_environment_observation_bytes(output) == bytes;
    } catch (...) {
        return false;
    }
}

}  // namespace ygo::environment
