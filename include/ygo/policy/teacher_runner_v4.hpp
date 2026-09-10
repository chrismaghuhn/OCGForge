#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/teacher_v3.hpp"

namespace ygo::policy {

struct TeacherRunnerV4Config final {
    std::array<std::optional<TeacherPolicySessionV3>, 2> sessions;
};

struct TeacherRunnerV4CreateResult;

struct TeacherRunnerV4ActionResult final {
    std::optional<environment::ActionSelection> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

class TeacherRunnerV4 final {
public:
    static TeacherRunnerV4CreateResult create(TeacherRunnerV4Config config) noexcept;

    TeacherRunnerV4(const TeacherRunnerV4&) = delete;
    TeacherRunnerV4& operator=(const TeacherRunnerV4&) = delete;
    TeacherRunnerV4(TeacherRunnerV4&&) = default;
    TeacherRunnerV4& operator=(TeacherRunnerV4&&) = default;

    PolicySelection select(const environment::DecisionFrame& frame) noexcept;
    PolicySelection select_with_diagnostics(
        const environment::DecisionFrame& frame,
        teacher::TeacherRankingDiagnosticsV3& diagnostics) noexcept;
    TeacherRunnerV4ActionResult select_action(
        const environment::DecisionFrame& frame) noexcept;
    bool commit(const environment::StepAccepted& accepted) noexcept;
    bool reject_pending_proposal() noexcept;

    bool has_pending_proposal() const noexcept { return pending_player_.has_value(); }
    const TeacherPolicySessionV3* session(std::uint8_t player) const noexcept {
        if (player >= 2 || !config_.sessions[player].has_value()) {
            return nullptr;
        }
        return &*config_.sessions[player];
    }

private:
    explicit TeacherRunnerV4(TeacherRunnerV4Config config)
        : config_(std::move(config)) {}

    static PolicySelection failure(PolicyErrorCode code, std::string message) noexcept;
    PolicySelection select_impl(
        const environment::DecisionFrame& frame,
        teacher::TeacherRankingDiagnosticsV3* diagnostics) noexcept;

    TeacherRunnerV4Config config_;
    std::optional<std::uint8_t> pending_player_;
    std::optional<std::string> pending_episode_semantic_id_;
    std::optional<std::string> pending_public_semantic_decision_id_;
};

struct TeacherRunnerV4CreateResult final {
    std::optional<TeacherRunnerV4> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

}  // namespace ygo::policy
