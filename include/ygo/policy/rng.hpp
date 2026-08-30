#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/policy/policy.hpp"

namespace ygo::policy {

inline constexpr std::string_view kPolicyRngContractIdentity =
    "ocgforge.policy_rng.sha256_counter.v1";
inline constexpr std::string_view kPolicyRngInitializationDomain =
    "ocgforge.policy_rng.sha256_counter.init.v1";
inline constexpr std::string_view kPolicyRngBlockDomain =
    "ocgforge.policy_rng.sha256_counter.block.v1";

struct PolicyRngInitializationInput final {
    std::optional<std::uint64_t> policy_rng_root_seed;
    std::string participant_policy_assignment_id;
    std::string policy_rng_stream_id;
};

// Runtime descriptor for the policy RNG. The canonical trajectory identity
// remains owned and computed by ygo::trajectory.
struct PolicyRngInitialization final {
    std::string policy_rng_contract_identity;
    std::string policy_rng_stream_id;
    std::vector<std::uint8_t> initialization_material;
    std::string policy_rng_initialization_identity;
};

struct PolicyRngInitializationResult final {
    std::optional<PolicyRngInitialization> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

struct PolicyRngWordResult final {
    std::optional<std::uint64_t> value;
    std::optional<PolicyError> error;
    std::uint64_t pre_cursor = 0;
    std::uint64_t post_cursor = 0;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

struct PolicyRngCreateResult;

namespace detail {
struct PolicyRngTestAccess;
}

class Sha256CounterRng final {
public:
    const PolicyRngInitialization& initialization() const noexcept {
        return initialization_;
    }

    std::uint64_t cursor() const noexcept { return cursor_; }

    PolicyRngWordResult next_raw_u64() noexcept;
    PolicyRngWordResult uniform_below_u64(std::uint64_t n) noexcept;

private:
    explicit Sha256CounterRng(PolicyRngInitialization identity)
        : initialization_(std::move(identity)) {}

    PolicyRngInitialization initialization_;
    std::uint64_t cursor_ = 0;

    friend PolicyRngCreateResult create_sha256_counter_rng(
        const PolicyRngInitializationInput& input) noexcept;
    friend struct detail::PolicyRngTestAccess;
};

struct PolicyRngCreateResult final {
    std::optional<Sha256CounterRng> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

PolicyRngInitializationResult make_policy_rng_initialization(
    const PolicyRngInitializationInput& input) noexcept;

PolicyRngCreateResult create_sha256_counter_rng(
    const PolicyRngInitializationInput& input) noexcept;

std::vector<std::uint8_t> canonical_policy_rng_initialization_material(
    const PolicyRngInitializationInput& input);

std::vector<std::uint8_t> canonical_policy_rng_block_bytes(
    std::string_view policy_rng_initialization_identity, std::uint64_t block_index);

}  // namespace ygo::policy
