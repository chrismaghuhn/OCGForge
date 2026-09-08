#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ygo/policy/teacher_runner_v3.hpp"
#include "ygo/trajectory/recorder_v2.hpp"

namespace ygo::policy {

namespace detail {
enum class TeacherRunnerV3TrajectoryTestScenario : std::uint8_t {
    StepRejected = 0,
    Continuation = 1,
    Terminal = 2,
    Failure = 3,
};

struct TeacherRunnerV3TrajectoryTestAccess;
}  // namespace detail

struct TeacherRunnerV3TrajectoryConfig final {
    environment::CertifiedEnvironmentConfig environment_config;
    environment::EpisodeSpec episode_spec;
    environment::RunControl run_control;
    trajectory::PolicyProvenanceEnvelope policy_provenance;
    TeacherRunnerV3Config runner_config;
};

struct TeacherRunnerV3TrajectoryRunResult final {
    std::optional<trajectory::EpisodeEnvelopeV2> envelope;
    std::optional<trajectory::RestrictedReplayEvidenceV2> replay_evidence;
    std::optional<PolicyError> error;
    std::string diagnostic;
    bool quarantined = false;

    explicit operator bool() const noexcept {
        return envelope.has_value() && !error.has_value();
    }
};

struct TeacherRunnerV3TrajectoryCreateResult;

class TeacherRunnerV3TrajectoryRunner final {
public:
    static TeacherRunnerV3TrajectoryCreateResult create(
        TeacherRunnerV3TrajectoryConfig config) noexcept;

    TeacherRunnerV3TrajectoryRunner(const TeacherRunnerV3TrajectoryRunner&) = delete;
    TeacherRunnerV3TrajectoryRunner& operator=(const TeacherRunnerV3TrajectoryRunner&) = delete;
    TeacherRunnerV3TrajectoryRunner(TeacherRunnerV3TrajectoryRunner&&) = default;
    TeacherRunnerV3TrajectoryRunner& operator=(TeacherRunnerV3TrajectoryRunner&&) = default;

    TeacherRunnerV3TrajectoryRunResult run() noexcept;

private:
    friend struct detail::TeacherRunnerV3TrajectoryTestAccess;

    TeacherRunnerV3TrajectoryRunner(
        TeacherRunnerV3TrajectoryConfig config,
        TeacherRunnerV3 runner,
        std::unique_ptr<environment::EpisodicEnvironment> environment,
        std::unique_ptr<trajectory::TrajectoryRecorderV2> recorder)
        : config_(std::move(config)),
          runner_(std::move(runner)),
          environment_(std::move(environment)),
          recorder_(std::move(recorder)) {}

    static TeacherRunnerV3TrajectoryRunResult failure(
        std::string message, std::optional<PolicyError> policy_error = std::nullopt) noexcept;
    TeacherRunnerV3TrajectoryRunResult run_impl(
        std::optional<detail::TeacherRunnerV3TrajectoryTestScenario> scenario) noexcept;

    TeacherRunnerV3TrajectoryConfig config_;
    TeacherRunnerV3 runner_;
    std::unique_ptr<environment::EpisodicEnvironment> environment_;
    std::unique_ptr<trajectory::TrajectoryRecorderV2> recorder_;
    bool has_run_ = false;
};

struct TeacherRunnerV3TrajectoryCreateResult final {
    std::optional<TeacherRunnerV3TrajectoryRunner> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

namespace detail {

struct TeacherRunnerV3TrajectoryTestAccess final {
    static TeacherRunnerV3TrajectoryRunResult run_with_scenario(
        TeacherRunnerV3TrajectoryRunner& runner,
        const TeacherRunnerV3TrajectoryTestScenario scenario) {
        return runner.run_impl(scenario);
    }
};

}  // namespace detail

}  // namespace ygo::policy
