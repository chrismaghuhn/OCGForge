#include "ygo/model/card_vocabulary.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/trace/sha256.hpp"

namespace {

using ygo::model::CardVocabularyErrorCode;
using ygo::model::CardVocabularyV1;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u32be(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string& value) {
    require(value.size() <= std::numeric_limits<std::uint32_t>::max(),
            "test vocabulary string exceeds u32");
    append_u32be(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void require_invalid(const std::vector<std::uint32_t>& passcodes,
                     const std::string& context) {
    const auto result = CardVocabularyV1::from_ascending_passcodes(passcodes);
    require(!result, context + " unexpectedly accepted an invalid list");
    require(!result.value.has_value(), context + " returned a value on failure");
    require(result.error.has_value(), context + " returned no error");
    require(result.error->code == CardVocabularyErrorCode::InvalidPasscodeList,
            context + " returned the wrong error code");
    require(!result.error->diagnostic.empty(), context + " returned no diagnostic");
}

void test_explicit_ascending_manifest_owns_mapping() {
    const auto result = CardVocabularyV1::from_ascending_passcodes({1001, 2002, 3003});
    require(static_cast<bool>(result), "valid vocabulary was rejected");
    require(result.value.has_value(), "valid vocabulary returned no value");
    const auto& vocabulary = *result.value;

    require(vocabulary.ascending_passcodes() ==
                std::vector<std::uint32_t>{1001, 2002, 3003},
            "vocabulary did not retain the explicit passcode list");
    require(vocabulary.id_for_public_passcode(1001) == 2 &&
                vocabulary.id_for_public_passcode(2002) == 3 &&
                vocabulary.id_for_public_passcode(3003) == 4,
            "vocabulary IDs did not follow rank plus two");
    require(!vocabulary.id_for_public_passcode(9999).has_value(),
            "unknown public passcode acquired a replacement ID");
    require(vocabulary.pad_id() == 0 && vocabulary.unknown_or_redacted_id() == 1,
            "reserved vocabulary IDs changed");

    std::vector<std::uint8_t> expected;
    append_string(expected, "ocgforge.model_card_vocabulary.v1");
    append_string(expected, "ocgforge.model_card_vocabulary.v1");
    append_string(expected, "ascending_public_passcode_rank_plus_two");
    append_u32be(expected, 3);
    append_u32be(expected, 1001);
    append_u32be(expected, 2002);
    append_u32be(expected, 3003);
    require(vocabulary.canonical_bytes() == expected,
            "vocabulary canonical bytes do not match the frozen recipe");
    require(vocabulary.identity() ==
                "model_card_vocabulary.v1." + ygo::trace::sha256_bytes(expected),
            "vocabulary identity does not hash the complete manifest");
}

void test_invalid_lists_fail_without_repair() {
    require_invalid({0}, "zero passcode");
    require_invalid({2002, 1001}, "unsorted passcodes");
    require_invalid({1001, 1001}, "duplicate passcodes");
    require_invalid({std::numeric_limits<std::uint32_t>::max(), 1001},
                    "descending maximum passcode");
}

void test_empty_manifest_is_deterministic_and_does_not_grow() {
    const auto first = CardVocabularyV1::from_ascending_passcodes({});
    const auto second = CardVocabularyV1::from_ascending_passcodes({});
    require(static_cast<bool>(first) && static_cast<bool>(second),
            "empty explicit vocabulary was rejected");
    require(first.value->canonical_bytes() == second.value->canonical_bytes() &&
                first.value->identity() == second.value->identity(),
            "empty vocabulary identity was not deterministic");
    require(!first.value->id_for_public_passcode(1001).has_value(),
            "empty vocabulary dynamically accepted a passcode");
}

}  // namespace

int main() {
    try {
        test_explicit_ascending_manifest_owns_mapping();
        test_invalid_lists_fail_without_repair();
        test_empty_manifest_is_deterministic_and_does_not_grow();
        std::cout << "card_vocabulary_tests=passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
