#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "direct_canonical_writer.hpp"

namespace {

using ygo::observation::detail::DirectCanonicalWriter;

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_primitives_and_integer_boundaries() {
    DirectCanonicalWriter writer;
    writer.append_literal("{");
    writer.append_literal("\"unsigned_zero\":");
    writer.append_uint(0);
    writer.append_literal(",\"unsigned_max\":");
    writer.append_uint(std::numeric_limits<std::uint64_t>::max());
    writer.append_literal(",\"signed_min\":");
    writer.append_int(std::numeric_limits<std::int64_t>::min());
    writer.append_literal(",\"signed_max\":");
    writer.append_int(std::numeric_limits<std::int64_t>::max());
    writer.append_literal(",\"true\":");
    writer.append_bool(true);
    writer.append_literal(",\"false\":");
    writer.append_bool(false);
    writer.append_literal(",\"null\":");
    writer.append_null();
    writer.append_char('}');

    require(
        writer.str() ==
            "{\"unsigned_zero\":0,\"unsigned_max\":18446744073709551615,"
            "\"signed_min\":-9223372036854775808,\"signed_max\":9223372036854775807,"
            "\"true\":true,\"false\":false,\"null\":null}",
        "direct writer primitive output diverged");
}

void test_exact_json_escaping_contract() {
    std::string input;
    input += "quote\" slash\\ newline\n carriage\r tab\t";
    input.push_back('\0');
    input.push_back('\x01');
    input.push_back('\x1f');
    input.push_back(' ');
    input.push_back('\x7f');
    input.push_back(static_cast<char>(0x80));
    input.push_back(static_cast<char>(0xff));
    input += " end";

    DirectCanonicalWriter writer;
    writer.append_escaped_string(input);

    require(
        [&writer]() {
            std::string expected =
                "\"quote\\\" slash\\\\ newline\\n carriage\\r tab\\t"
                "\\u0000\\u0001\\u001f";
            expected.push_back(' ');
            expected.push_back('\x7f');
            expected.push_back(static_cast<char>(0x80));
            expected.push_back(static_cast<char>(0xff));
            expected += " end\"";
            return writer.str() == expected;
        }(),
        "direct writer JSON escaping diverged");

    DirectCanonicalWriter empty;
    empty.append_escaped_string("");
    require(empty.str() == "\"\"", "empty string escaping diverged");
}

}  // namespace

int main() {
    try {
        test_primitives_and_integer_boundaries();
        test_exact_json_escaping_contract();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "m4_3_6_direct_writer_test=ok\n";
    return 0;
}
