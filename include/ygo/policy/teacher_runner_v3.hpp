#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/teacher_v2.hpp"

namespace ygo::policy {

struct TeacherRunnerV3Config final {
    // This successor owns only the public policy-execution lifecycle. Trusted
    // trajectory recording/admission remains a separate V1-bound boundary.
    std::array<std::optional<TeacherPolicySessionV2>, 2> sessions;
};

struct TeacherRunnerV3CreateResult;

struct TeacherRunnerV3ActionResult final {
    std::optional<environment::ActionSelection> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

class TeacherRunnerV3 final {
public:
    static TeacherRunnerV3CreateResult create(TeacherRunnerV3Config config) noexcept;

    TeacherRunnerV3(const TeacherRunnerV3&) = delete;
    TeacherRunnerV3& operator=(const TeacherRunnerV3&) = delete;
    TeacherRunnerV3(TeacherRunnerV3&&) = default;
    TeacherRunnerV3& operator=(TeacherRunnerV3&&) = default;

    PolicySelection select(const environment::DecisionFrame& frame) noexcept;
    TeacherRunnerV3ActionResult select_action(
        const environment::DecisionFrame& frame) noexcept;
    bool commit(const environment::StepAccepted& accepted) noexcept;
    bool reject_pending_proposal() noexcept;

    bool has_pending_proposal() const noexcept { return pending_player_.has_value(); }
    const TeacherPolicySessionV2& session(std::uint8_t player) const noexcept {
        return *config_.sessions[player];
    }

private:
    explicit TeacherRunnerV3(TeacherRunnerV3Config config)
        : config_(std::move(config)) {}

    static PolicySelection failure(PolicyErrorCode code, std::string message) noexcept;

    TeacherRunnerV3Config config_;
    std::optional<std::uint8_t> pending_player_;
    std::optional<std::string> pending_episode_semantic_id_;
    std::optional<std::string> pending_public_semantic_decision_id_;
};

struct TeacherRunnerV3CreateResult final {
    std::optional<TeacherRunnerV3> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

}  // namespace ygo::policy
