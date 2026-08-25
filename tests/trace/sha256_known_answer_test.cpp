#include "ygo/trace/sha256.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    require(ygo::trace::sha256_string("") ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 empty-string known answer failed");
    require(ygo::trace::sha256_string("abc") ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc known answer failed");

    const std::string million_a(1'000'000, 'a');
    const auto million_a_hash =
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    require(ygo::trace::sha256_string(million_a) == million_a_hash,
            "SHA-256 one-million-a string known answer failed");
    const std::vector<std::uint8_t> million_a_bytes(million_a.begin(), million_a.end());
    require(ygo::trace::sha256_bytes(million_a_bytes) == million_a_hash,
            "SHA-256 one-million-a byte known answer failed");

    std::cout << "sha256_known_answer_tests=passed\n";
    return 0;
}
