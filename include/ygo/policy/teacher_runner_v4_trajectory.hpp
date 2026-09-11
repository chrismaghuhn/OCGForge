#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ygo/diagnostics/task7_observer.hpp"
#include "ygo/policy/teacher_runner_v4.hpp"
#include "ygo/trajectory/recorder_v3.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"

namespace ygo::policy {

struct TeacherRunnerV4TrajectoryConfig final {
    environment::CertifiedEnvironmentConfig environment_config;
    environment::EpisodeSpec episode_spec;
    environment::RunControl run_control;
    trajectory::PolicyProvenanceEnvelope policy_provenance;
    TeacherRunnerV4Config runner_config;
    diagnostics::Task7DiagnosticObserver diagnostic_observer;
};

struct TeacherRunnerV4TrajectoryRunResult final {
    std::optional<trajectory::EpisodeEnvelopeV3> envelope;
    std::optional<PolicyError> error;
    std::string diagnostic;
    bool quarantined = false;

    explicit operator bool() const noexcept {
        return envelope.has_value() && !error.has_value();
    }
};

struct TeacherRunnerV4TrajectoryBoundedDiagnosticResult final {
    TeacherRunnerV4TrajectoryRunResult run;
    std::optional<trajectory::RestrictedReplayEvidenceV3>
        restricted_replay_evidence;
};

struct TeacherRunnerV4TrajectoryCreateResult;

namespace detail {
struct TeacherRunnerV4TrajectoryTestAccess;
}

class TeacherRunnerV4TrajectoryRunner final {
public:
    static TeacherRunnerV4TrajectoryCreateResult create(
        TeacherRunnerV4TrajectoryConfig config) noexcept;

    TeacherRunnerV4TrajectoryRunner(const TeacherRunnerV4TrajectoryRunner&) = delete;
    TeacherRunnerV4TrajectoryRunner& operator=(
        const TeacherRunnerV4TrajectoryRunner&) = delete;
    TeacherRunnerV4TrajectoryRunner(TeacherRunnerV4TrajectoryRunner&&) = default;
    TeacherRunnerV4TrajectoryRunner& operator=(TeacherRunnerV4TrajectoryRunner&&) = default;

    TeacherRunnerV4TrajectoryRunResult run() noexcept;

private:
    friend struct detail::TeacherRunnerV4TrajectoryTestAccess;

    explicit TeacherRunnerV4TrajectoryRunner(
        TeacherRunnerV4TrajectoryConfig config,
        TeacherRunnerV4 runner,
        std::unique_ptr<environment::EpisodicEnvironment> environment,
        std::unique_ptr<trajectory::TrajectoryRecorderV3> recorder)
        : config_(std::move(config)),
          runner_(std::move(runner)),
          environment_(std::move(environment)),
          recorder_(std::move(recorder)) {}

    static TeacherRunnerV4TrajectoryRunResult failure(
        std::string message,
        std::optional<PolicyError> policy_error = std::nullopt) noexcept;
    TeacherRunnerV4TrajectoryRunResult run_impl(
        std::optional<std::uint64_t> decision_limit,
        std::optional<trajectory::RestrictedReplayEvidenceV3>*
            restricted_replay_evidence) noexcept;

    TeacherRunnerV4TrajectoryConfig config_;
    TeacherRunnerV4 runner_;
    std::unique_ptr<environment::EpisodicEnvironment> environment_;
    std::unique_ptr<trajectory::TrajectoryRecorderV3> recorder_;
    bool has_run_ = false;
};

struct TeacherRunnerV4TrajectoryCreateResult final {
    std::optional<TeacherRunnerV4TrajectoryRunner> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

namespace detail {

struct TeacherRunnerV4TrajectoryTestAccess final {
    static TeacherRunnerV4TrajectoryRunResult run_until_decision(
        TeacherRunnerV4TrajectoryRunner& runner,
        const std::uint64_t decision_limit) {
        return runner.run_impl(decision_limit, nullptr);
    }

    static TeacherRunnerV4TrajectoryBoundedDiagnosticResult
    run_until_decision_with_replay_evidence(
        TeacherRunnerV4TrajectoryRunner& runner,
        const std::uint64_t decision_limit) {
        TeacherRunnerV4TrajectoryBoundedDiagnosticResult result;
        result.run = runner.run_impl(
            decision_limit, &result.restricted_replay_evidence);
        return result;
    }
};

}  // namespace detail

}  // namespace ygo::policy
