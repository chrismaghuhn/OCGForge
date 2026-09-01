#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ygo::model {

inline constexpr std::string_view kCardVocabularySchemaId =
    "ocgforge.model_card_vocabulary.v1";
inline constexpr std::string_view kCardVocabularyIdentityPrefix =
    "model_card_vocabulary.v1.";
inline constexpr std::uint32_t kCardVocabularyPadId = 0;
inline constexpr std::uint32_t kCardVocabularyUnknownOrRedactedId = 1;

enum class CardVocabularyErrorCode : std::uint8_t {
    InvalidPasscodeList,
    InternalFailure,
};

struct CardVocabularyError final {
    CardVocabularyErrorCode code = CardVocabularyErrorCode::InternalFailure;
    std::string diagnostic;
};

struct CardVocabularyResult;

class CardVocabularyV1 final {
public:
    static CardVocabularyResult from_ascending_passcodes(
        std::vector<std::uint32_t> ascending_passcodes) noexcept;

    const std::vector<std::uint32_t>& ascending_passcodes() const noexcept {
        return ascending_passcodes_;
    }

    std::optional<std::uint32_t> id_for_public_passcode(
        std::uint32_t public_passcode) const noexcept;

    constexpr std::uint32_t pad_id() const noexcept { return kCardVocabularyPadId; }
    constexpr std::uint32_t unknown_or_redacted_id() const noexcept {
        return kCardVocabularyUnknownOrRedactedId;
    }

    std::vector<std::uint8_t> canonical_bytes() const;
    std::string identity() const;

private:
    explicit CardVocabularyV1(std::vector<std::uint32_t> ascending_passcodes)
        : ascending_passcodes_(std::move(ascending_passcodes)) {}

    std::vector<std::uint32_t> ascending_passcodes_;
};

struct CardVocabularyResult final {
    std::optional<CardVocabularyV1> value;
    std::optional<CardVocabularyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view card_vocabulary_error_code_name(
    CardVocabularyErrorCode code) noexcept;

std::vector<std::uint8_t> canonical_card_vocabulary_bytes(
    const CardVocabularyV1& vocabulary);
std::string card_vocabulary_identity(const CardVocabularyV1& vocabulary);

}  // namespace ygo::model
