#include "direct_canonical_writer.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace ygo::observation::detail {
namespace {

template <typename Integer>
void append_integer(std::string& output, const Integer value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc()) {
        throw std::runtime_error("direct canonical integer formatting failed");
    }
    output.append(buffer.data(), result.ptr);
}

}  // namespace

DirectCanonicalWriter::DirectCanonicalWriter(const std::size_t reserve_hint) {
    if (reserve_hint != 0) {
        output_.reserve(reserve_hint);
    }
}

void DirectCanonicalWriter::append_literal(const std::string_view value) {
    output_.append(value.data(), value.size());
}

void DirectCanonicalWriter::append_char(const char value) {
    output_.push_back(value);
}

void DirectCanonicalWriter::append_uint(const std::uint64_t value) {
    append_integer(output_, value);
}

void DirectCanonicalWriter::append_int(const std::int64_t value) {
    append_integer(output_, value);
}

void DirectCanonicalWriter::append_bool(const bool value) {
    append_literal(value ? "true" : "false");
}

void DirectCanonicalWriter::append_null() {
    append_literal("null");
}

void DirectCanonicalWriter::append_escaped_string(const std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";

    append_char('"');
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            append_literal("\\\"");
            break;
        case '\\':
            append_literal("\\\\");
            break;
        case '\n':
            append_literal("\\n");
            break;
        case '\r':
            append_literal("\\r");
            break;
        case '\t':
            append_literal("\\t");
            break;
        default:
            if (byte < 0x20) {
                append_literal("\\u00");
                append_char(kHex[byte >> 4]);
                append_char(kHex[byte & 0x0f]);
            } else {
                append_char(character);
            }
            break;
        }
    }
    append_char('"');
}

DirectCanonicalWriter& DirectCanonicalWriter::operator<<(const char* value) {
    append_literal(value);
    return *this;
}

DirectCanonicalWriter& DirectCanonicalWriter::operator<<(const std::string& value) {
    append_literal(value);
    return *this;
}

DirectCanonicalWriter& DirectCanonicalWriter::operator<<(const std::string_view value) {
    append_literal(value);
    return *this;
}

DirectCanonicalWriter& DirectCanonicalWriter::operator<<(const char value) {
    append_char(value);
    return *this;
}

const std::string& DirectCanonicalWriter::str() const noexcept {
    return output_;
}

std::size_t DirectCanonicalWriter::size() const noexcept {
    return output_.size();
}

std::string DirectCanonicalWriter::take() {
    return std::move(output_);
}

}  // namespace ygo::observation::detail
