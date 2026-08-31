#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"

namespace ygo::policy {

enum class PolicyErrorCode : std::uint8_t {
    InvalidConfiguration,
    EmptyCandidateDomain,
    InvalidCandidateDomain,
    RngExhausted,
    LifecycleFailure,
};

struct PolicyError final {
    PolicyErrorCode code = PolicyErrorCode::InvalidConfiguration;
    std::string message;
};

struct PolicyInput final {
    const environment::PublicEnvironmentObservation& observation;
    const std::vector<environment::EnvironmentActionCandidate>& candidates;
};

struct PolicyRngCursorTransition final {
    std::uint64_t pre_cursor = 0;
    std::uint64_t post_cursor = 0;
};

struct PolicyExecutionBinding final {
    std::string policy_artifact_id;
    std::string participant_policy_assignment_id;
    std::string policy_rng_contract_identity;
    std::string policy_rng_stream_id;
    std::string policy_rng_initialization_identity;
    std::string policy_rng_identity;
};

struct PolicySelectionResult final {
    std::string public_action_key;
    std::optional<PolicyRngCursorTransition> rng_cursor;
};

struct PolicySelection final {
    std::optional<PolicySelectionResult> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

}  // namespace ygo::policy
