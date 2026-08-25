#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace ygo::observation::detail {

class DirectCanonicalWriter final {
public:
    explicit DirectCanonicalWriter(std::size_t reserve_hint = 0);

    void append_literal(std::string_view value);
    void append_char(char value);
    void append_uint(std::uint64_t value);
    void append_int(std::int64_t value);
    void append_bool(bool value);
    void append_null();
    void append_escaped_string(std::string_view value);

    DirectCanonicalWriter& operator<<(const char* value);
    DirectCanonicalWriter& operator<<(const std::string& value);
    DirectCanonicalWriter& operator<<(std::string_view value);
    DirectCanonicalWriter& operator<<(char value);

    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    DirectCanonicalWriter& operator<<(T value) {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
            append_bool(value);
        } else if constexpr (std::is_signed_v<T>) {
            append_int(static_cast<std::int64_t>(value));
        } else {
            append_uint(static_cast<std::uint64_t>(value));
        }
        return *this;
    }

    [[nodiscard]] const std::string& str() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::string take();

private:
    std::string output_;
};

}  // namespace ygo::observation::detail
