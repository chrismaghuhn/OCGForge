#include "ygo/environment/public_environment_observation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

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
        if (index + width > value.size()) {
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
            (width == 4 && code_point < 0x10000) ||
            code_point > 0x10ffff ||
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

    bool optional_u8(std::uint8_t& value, bool& present) noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        present = flag == 1;
        return !present || u8(value);
    }

    bool optional_u32(std::uint32_t& value, bool& present) noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        present = flag == 1;
        return !present || u32(value);
    }

    bool optional_u64(std::uint64_t& value, bool& present) noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        present = flag == 1;
        return !present || u64(value);
    }

    bool optional_i32(std::int32_t& value, bool& present) noexcept {
        std::uint32_t bits = 0;
        if (!optional_u32(bits, present)) {
            return false;
        }
        value = static_cast<std::int32_t>(bits);
        return true;
    }

    bool boolean(bool& value) noexcept {
        std::uint8_t encoded = 0;
        if (!u8(encoded) || encoded > 1) {
            return false;
        }
        value = encoded == 1;
        return true;
    }

    bool end() const noexcept { return position_ == bytes_.size(); }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool read_optional_locator(Cursor& cursor) noexcept {
    std::uint8_t flag = 0;
    if (!cursor.u8(flag) || flag > 1) {
        return false;
    }
    if (flag == 0) {
        return true;
    }
    std::string value;
    return cursor.string(value) && locator(value);
}

bool read_optional_zone(Cursor& cursor) noexcept {
    std::uint8_t flag = 0;
    if (!cursor.u8(flag) || flag > 1) {
        return false;
    }
    if (flag == 0) {
        return true;
    }
    std::uint8_t zone = 0;
    return cursor.u8(zone) && zone <= 10;
}

bool read_optional_player(Cursor& cursor) noexcept {
    std::uint8_t value = 0;
    bool present = false;
    return cursor.optional_u8(value, present) && (!present || value <= 1);
}

bool read_optional_u32_checked(Cursor& cursor) noexcept {
    std::uint32_t value = 0;
    bool present = false;
    return cursor.optional_u32(value, present);
}

bool read_optional_u64_checked(Cursor& cursor) noexcept {
    std::uint64_t value = 0;
    bool present = false;
    return cursor.optional_u64(value, present);
}

bool read_optional_i32_checked(Cursor& cursor) noexcept {
    std::int32_t value = 0;
    bool present = false;
    return cursor.optional_i32(value, present);
}

bool read_properties(Cursor& cursor, bool& present_value) noexcept {
    std::uint8_t flag = 0;
    if (!cursor.u8(flag) || flag > 1) {
        return false;
    }
    present_value = flag == 1;
    if (flag == 0) {
        return true;
    }
    if (!read_optional_u32_checked(cursor) || !read_optional_u32_checked(cursor) ||
        !read_optional_u64_checked(cursor) || !read_optional_i32_checked(cursor) ||
        !read_optional_i32_checked(cursor) || !read_optional_i32_checked(cursor) ||
        !read_optional_i32_checked(cursor) || !read_optional_u32_checked(cursor) ||
        !read_optional_u32_checked(cursor) || !read_optional_u32_checked(cursor)) {
        return false;
    }
    std::uint32_t marker_count = 0;
    if (!cursor.u32(marker_count)) {
        return false;
    }
    std::uint8_t previous_marker = 0;
    bool have_marker = false;
    for (std::uint32_t index = 0; index < marker_count; ++index) {
        std::uint8_t marker = 0;
        if (!cursor.u8(marker) || marker > 7 || (have_marker && marker < previous_marker)) {
            return false;
        }
        previous_marker = marker;
        have_marker = true;
    }
    if (!read_optional_u32_checked(cursor) || !read_optional_u32_checked(cursor) ||
        !read_optional_u32_checked(cursor)) {
        return false;
    }
    std::uint32_t counter_count = 0;
    if (!cursor.u32(counter_count)) {
        return false;
    }
    std::uint32_t previous_type = 0;
    std::uint32_t previous_count = 0;
    bool have_counter = false;
    for (std::uint32_t index = 0; index < counter_count; ++index) {
        std::uint32_t type = 0;
        std::uint32_t count = 0;
        if (!cursor.u32(type) || !cursor.u32(count) ||
            (have_counter && std::tie(type, count) < std::tie(previous_type, previous_count))) {
            return false;
        }
        previous_type = type;
        previous_count = count;
        have_counter = true;
    }
    return true;
}

bool read_sorted_targets(Cursor& cursor) noexcept {
    std::uint32_t count = 0;
    if (!cursor.u32(count)) {
        return false;
    }
    std::string previous;
    bool have_previous = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string target;
        if (!cursor.string(target) || !locator(target) ||
            (have_previous && target < previous)) {
            return false;
        }
        previous = std::move(target);
        have_previous = true;
    }
    return true;
}

bool read_safe_state(const std::vector<std::uint8_t>& bytes) noexcept {
    Cursor cursor(bytes);
    std::string schema;
    if (!cursor.string(schema) || schema != kPublicSafeStateSchemaId ||
        !cursor.string(schema) || schema != kPublicSafeStateSchemaId) {
        return false;
    }

    std::uint64_t ignored_u64 = 0;
    std::uint32_t count = 0;
    if (!cursor.u64(ignored_u64) || !cursor.u32(count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t ignored = 0;
        if (!cursor.u32(ignored)) {
            return false;
        }
    }
    if (!read_optional_player(cursor) || !read_optional_player(cursor) ||
        !read_optional_u32_checked(cursor) || !read_optional_u32_checked(cursor)) {
        return false;
    }
    std::uint32_t ignored_u32 = 0;
    if (!cursor.u32(ignored_u32) || !read_optional_player(cursor) ||
        !read_optional_player(cursor)) {
        return false;
    }
    bool ignored_bool = false;
    if (!cursor.boolean(ignored_bool)) {
        return false;
    }

    // Zones are canonically sorted by their complete tuple.
    if (!cursor.u32(count)) {
        return false;
    }
    std::tuple<std::uint8_t, std::uint8_t, std::uint32_t, std::uint32_t, std::uint32_t, bool>
        previous_zone{};
    bool have_zone = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint8_t player = 0;
        std::uint8_t kind = 0;
        std::uint32_t total = 0;
        std::uint32_t public_count = 0;
        std::uint32_t hidden = 0;
        bool observable_order = false;
        if (!cursor.u8(player) || !cursor.u8(kind) || !cursor.u32(total) ||
            !cursor.u32(public_count) || !cursor.u32(hidden) ||
            !cursor.boolean(observable_order) || player > 1 || kind > 10) {
            return false;
        }
        const auto current = std::make_tuple(player, kind, total, public_count, hidden,
                                             observable_order);
        if (have_zone && current < previous_zone) {
            return false;
        }
        previous_zone = current;
        have_zone = true;
    }

    // Entities are sorted by locator and retain all fixed-width public fields.
    if (!cursor.u32(count)) {
        return false;
    }
    std::string previous_locator;
    bool have_locator = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string current_locator;
        if (!cursor.string(current_locator) || !locator(current_locator) ||
            (have_locator && current_locator <= previous_locator)) {
            return false;
        }
        previous_locator = current_locator;
        have_locator = true;
        bool identity_known = false;
        if (!cursor.boolean(identity_known)) {
            return false;
        }
        std::uint32_t passcode = 0;
        bool passcode_present = false;
        if (!cursor.optional_u32(passcode, passcode_present) || !read_optional_player(cursor) ||
            !read_optional_player(cursor)) {
            return false;
        }
        std::uint8_t zone = 0;
        if (!cursor.u8(zone) || zone > 10 ||
            !read_optional_u32_checked(cursor) || !read_optional_u32_checked(cursor)) {
            return false;
        }
        std::uint8_t position = 0;
        if (!cursor.u8(position) ||
            !(position == 0 || position == 1 || position == 2 || position == 4 || position == 8)) {
            return false;
        }
        bool face_up = false;
        bool face_down = false;
        if (!cursor.boolean(face_up) || !cursor.boolean(face_down) || (face_up && face_down)) {
            return false;
        }
        bool printed_present = false;
        bool current_present = false;
        if (!read_properties(cursor, printed_present) ||
            !read_properties(cursor, current_present)) {
            return false;
        }
        if (!identity_known && (passcode_present || printed_present || current_present)) {
            return false;
        }
    }

    // Relationships are sorted by kind/source/target.
    if (!cursor.u32(count)) {
        return false;
    }
    std::tuple<std::uint8_t, std::string, std::string> previous_relationship;
    bool have_relationship = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint8_t kind = 0;
        std::string source;
        std::string target;
        if (!cursor.u8(kind) || kind > 2 || !cursor.string(source) || !locator(source) ||
            !cursor.string(target) || !locator(target)) {
            return false;
        }
        const auto current = std::make_tuple(kind, source, target);
        if (have_relationship && current < previous_relationship) {
            return false;
        }
        previous_relationship = current;
        have_relationship = true;
    }

    // Chain links retain source order; their target vectors are sorted.
    if (!cursor.u32(ignored_u32) || !cursor.u32(count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        if (!cursor.u32(ignored_u32) || !read_optional_player(cursor) ||
            !read_optional_locator(cursor) || !read_optional_zone(cursor) ||
            !read_optional_u64_checked(cursor) || !read_sorted_targets(cursor)) {
            return false;
        }
    }

    // Visible events are sorted by public event index.
    if (!cursor.u32(count)) {
        return false;
    }
    std::uint64_t previous_event_index = 0;
    bool have_event = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint64_t event_index = 0;
        std::uint8_t kind = 0;
        if (!cursor.u64(event_index) || !cursor.u8(kind) || kind > 22 ||
            (have_event && event_index <= previous_event_index) ||
            !read_optional_player(cursor) || !read_optional_locator(cursor) ||
            !read_optional_u32_checked(cursor) || !read_optional_zone(cursor) ||
            !read_optional_zone(cursor) || !read_optional_u32_checked(cursor) ||
            !read_optional_i32_checked(cursor) || !read_optional_u32_checked(cursor) ||
            !read_optional_u32_checked(cursor) || !read_optional_player(cursor) ||
            !read_optional_player(cursor) || !read_optional_u64_checked(cursor) ||
            !read_sorted_targets(cursor)) {
            return false;
        }
        previous_event_index = event_index;
        have_event = true;
    }

    // Match context and two canonical sorted static decks.
    std::uint8_t perspective = 0;
    if (!cursor.u8(perspective) || perspective > 1 || !cursor.u64(ignored_u64)) {
        return false;
    }
    if (!cursor.boolean(ignored_bool) || !cursor.boolean(ignored_bool)) {
        return false;
    }
    for (int deck = 0; deck < 2; ++deck) {
        bool known = false;
        if (!cursor.boolean(known) || !cursor.u32(count)) {
            return false;
        }
        const auto main_count = count;
        std::uint32_t previous_code = 0;
        bool have_code = false;
        for (std::uint32_t index = 0; index < count; ++index) {
            std::uint32_t code = 0;
            if (!cursor.u32(code) || (have_code && code < previous_code)) {
                return false;
            }
            previous_code = code;
            have_code = true;
        }
        if (!cursor.u32(count)) {
            return false;
        }
        previous_code = 0;
        have_code = false;
        for (std::uint32_t index = 0; index < count; ++index) {
            std::uint32_t code = 0;
            if (!cursor.u32(code) || (have_code && code < previous_code)) {
                return false;
            }
            previous_code = code;
            have_code = true;
        }
        const auto extra_count = count;
        if (!known && (main_count != 0 || extra_count != 0)) {
            return false;
        }
    }
    return cursor.end();
}

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
            !cursor.raw(safe_state) || !read_safe_state(safe_state)) {
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
        if (!cursor.u32(count)) {
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
