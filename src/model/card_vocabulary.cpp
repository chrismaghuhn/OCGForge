#include "ygo/model/card_vocabulary.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"

namespace ygo::model {
namespace {

constexpr std::string_view kMappingRule =
    "ascending_public_passcode_rank_plus_two";

void append_u32be(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("card vocabulary string exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void validate_passcodes(const std::vector<std::uint32_t>& passcodes) {
    if (passcodes.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("card vocabulary passcode count exceeds u32 length");
    }
    std::uint32_t previous = 0;
    bool have_previous = false;
    for (const auto passcode : passcodes) {
        if (passcode == 0 || (have_previous && passcode <= previous)) {
            throw std::invalid_argument("card vocabulary passcode list is not strictly ascending");
        }
        previous = passcode;
        have_previous = true;
    }
}

CardVocabularyError error(const CardVocabularyErrorCode code,
                           const char* diagnostic) {
    return CardVocabularyError{code, diagnostic};
}

}  // namespace

CardVocabularyResult CardVocabularyV1::from_ascending_passcodes(
    std::vector<std::uint32_t> ascending_passcodes) noexcept {
    try {
        validate_passcodes(ascending_passcodes);
        return {std::optional<CardVocabularyV1>(
                    CardVocabularyV1(std::move(ascending_passcodes))),
                std::nullopt};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, error(CardVocabularyErrorCode::InternalFailure,
                                    "card vocabulary construction failed")};
    } catch (...) {
        return {std::nullopt, error(CardVocabularyErrorCode::InvalidPasscodeList,
                                    "card vocabulary passcode list is invalid")};
    }
}

std::optional<std::uint32_t> CardVocabularyV1::id_for_public_passcode(
    const std::uint32_t public_passcode) const noexcept {
    const auto found = std::lower_bound(ascending_passcodes_.begin(),
                                        ascending_passcodes_.end(), public_passcode);
    if (found == ascending_passcodes_.end() || *found != public_passcode) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(found - ascending_passcodes_.begin());
    if (index > std::numeric_limits<std::uint32_t>::max() - 2) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(index + 2);
}

std::vector<std::uint8_t> canonical_card_vocabulary_bytes(
    const CardVocabularyV1& vocabulary) {
    validate_passcodes(vocabulary.ascending_passcodes());
    std::vector<std::uint8_t> bytes;
    bytes.reserve(128 + vocabulary.ascending_passcodes().size() * 4);
    append_string(bytes, kCardVocabularySchemaId);
    append_string(bytes, kCardVocabularySchemaId);
    append_string(bytes, kMappingRule);
    append_u32be(bytes, static_cast<std::uint32_t>(vocabulary.ascending_passcodes().size()));
    for (const auto passcode : vocabulary.ascending_passcodes()) {
        append_u32be(bytes, passcode);
    }
    return bytes;
}

std::vector<std::uint8_t> CardVocabularyV1::canonical_bytes() const {
    return canonical_card_vocabulary_bytes(*this);
}

std::string card_vocabulary_identity(const CardVocabularyV1& vocabulary) {
    return std::string(kCardVocabularyIdentityPrefix) +
           ygo::trace::sha256_bytes(canonical_card_vocabulary_bytes(vocabulary));
}

std::string CardVocabularyV1::identity() const {
    return card_vocabulary_identity(*this);
}

std::string_view card_vocabulary_error_code_name(
    const CardVocabularyErrorCode code) noexcept {
    switch (code) {
    case CardVocabularyErrorCode::InvalidPasscodeList:
        return "invalid_passcode_list";
    case CardVocabularyErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

}  // namespace ygo::model
