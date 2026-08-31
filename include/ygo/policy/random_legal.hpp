#pragma once

#include <optional>
#include <utility>

#include "ygo/policy/policy.hpp"
#include "ygo/policy/rng.hpp"

namespace ygo::policy {

struct RandomLegalPolicyCreateResult;

class RandomLegalPolicy final {
public:
    PolicySelection select(const PolicyInput& input) noexcept;

    const Sha256CounterRng& rng() const noexcept { return rng_; }

private:
    explicit RandomLegalPolicy(Sha256CounterRng rng) : rng_(std::move(rng)) {}

    Sha256CounterRng rng_;

    friend RandomLegalPolicyCreateResult create_random_legal_policy(
        const PolicyRngInitializationInput& input) noexcept;
};

struct RandomLegalPolicyCreateResult final {
    std::optional<RandomLegalPolicy> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

RandomLegalPolicyCreateResult create_random_legal_policy(
    const PolicyRngInitializationInput& input) noexcept;

}  // namespace ygo::policy
