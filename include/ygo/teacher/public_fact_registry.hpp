#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/public_environment_observation.hpp"

namespace ygo::teacher {

enum class PublicFactValueKind : std::uint8_t {
    Boolean = 0,
    U64 = 1,
    I32 = 2,
    Token = 3,
};

enum class PublicFactValidityScope : std::uint8_t {
    CurrentReconciliation = 0,
    AcceptedPublicHistory = 1,
};

enum class PublicFactSourceClassification : std::uint8_t {
    Direct = 0,
    SafeDerivation = 1,
    Blocked = 2,
};

struct PublicFactValue final {
    std::string fact_id;
    PublicFactValueKind value_kind = PublicFactValueKind::Boolean;
    bool boolean_value = false;
    std::uint64_t u64_value = 0;
    std::int32_t i32_value = 0;
    std::string token_value;
    PublicFactValidityScope validity_scope =
        PublicFactValidityScope::CurrentReconciliation;

    bool operator==(const PublicFactValue& other) const noexcept {
        return fact_id == other.fact_id && value_kind == other.value_kind &&
               boolean_value == other.boolean_value && u64_value == other.u64_value &&
               i32_value == other.i32_value && token_value == other.token_value &&
               validity_scope == other.validity_scope;
    }
    bool operator!=(const PublicFactValue& other) const noexcept {
        return !(*this == other);
    }
};

struct PublicFactDefinition final {
    std::string fact_id;
    PublicFactValueKind value_kind = PublicFactValueKind::Boolean;
    std::vector<PublicFactValidityScope> allowed_scopes;
    PublicFactSourceClassification source_classification =
        PublicFactSourceClassification::Blocked;
    std::optional<std::uint64_t> u64_minimum;
    std::optional<std::uint64_t> u64_maximum;
    std::optional<std::int32_t> i32_minimum;
    std::optional<std::int32_t> i32_maximum;
    std::vector<std::string> token_domain;
    std::string source_rule;
};

std::string_view public_fact_value_kind_name(PublicFactValueKind value) noexcept;
std::string_view public_fact_scope_name(PublicFactValidityScope value) noexcept;
std::string_view public_fact_source_name(
    PublicFactSourceClassification value) noexcept;

bool validate_public_fact_value(const PublicFactValue& value) noexcept;
std::vector<std::uint8_t> canonical_public_fact_value_bytes(
    const PublicFactValue& value);

class PublicFactRegistry final {
public:
    static const PublicFactRegistry& canonical() noexcept;

    PublicFactRegistry(const PublicFactRegistry&) = delete;
    PublicFactRegistry& operator=(const PublicFactRegistry&) = delete;
    PublicFactRegistry(PublicFactRegistry&&) = delete;
    PublicFactRegistry& operator=(PublicFactRegistry&&) = delete;

    const std::vector<PublicFactDefinition>& definitions() const noexcept;
    bool is_registered(std::string_view fact_id) const noexcept;
    std::optional<PublicFactSourceClassification> source_classification(
        std::string_view fact_id) const noexcept;
    bool validate(const PublicFactValue& value) const noexcept;

private:
    PublicFactRegistry() = default;
};

struct PublicFactSnapshot final {
    std::vector<PublicFactValue> values;

    bool contains(std::string_view fact_id) const noexcept;
    std::optional<PublicFactValue> value(std::string_view fact_id) const;

    bool operator==(const PublicFactSnapshot& other) const noexcept {
        return values == other.values;
    }
    bool operator!=(const PublicFactSnapshot& other) const noexcept {
        return !(*this == other);
    }
};

struct PublicFactExtractionResult final {
    bool valid = false;
    PublicFactSnapshot snapshot;

    explicit operator bool() const noexcept { return valid; }
};

PublicFactExtractionResult extract_public_fact_snapshot(
    const environment::PublicEnvironmentObservation& observation) noexcept;

}  // namespace ygo::teacher
