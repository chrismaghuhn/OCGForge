#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ygo/policy/runner.hpp"
#include "ygo/policy/teacher.hpp"

namespace ygo::policy {

namespace detail {
enum class TeacherRunnerTestSelectorBehavior : std::uint8_t {
    InvalidPublicAction = 0,
    PolicyFailure = 1,
};

struct TeacherRunnerTestOverride final {
    std::uint8_t player = 0;
    TeacherRunnerTestSelectorBehavior behavior =
        TeacherRunnerTestSelectorBehavior::InvalidPublicAction;
    std::shared_ptr<std::size_t> selection_calls;
};

struct TeacherRunnerTestAccess;
}  // namespace detail

struct TeacherRunnerConfig final {
    environment::CertifiedEnvironmentConfig environment_config;
    environment::EpisodeSpec episode_spec;
    environment::RunControl run_control;
    trajectory::PolicyProvenanceEnvelope policy_provenance;
    std::array<std::optional<TeacherPolicySession>, 2> sessions;
};

struct TeacherRunnerCreateResult;

class TeacherRunner final {
public:
    static TeacherRunnerCreateResult create(TeacherRunnerConfig config) noexcept;

    TeacherRunner(const TeacherRunner&) = delete;
    TeacherRunner& operator=(const TeacherRunner&) = delete;
    TeacherRunner(TeacherRunner&&) = default;
    TeacherRunner& operator=(TeacherRunner&&) = default;

    PolicyRunnerResult run() noexcept;

private:
    friend struct detail::TeacherRunnerTestAccess;

    PolicyRunnerResult run_impl(const detail::TeacherRunnerTestOverride* test_override) noexcept;

    TeacherRunner(TeacherRunnerConfig config,
                  std::unique_ptr<environment::EpisodicEnvironment> environment,
                  std::unique_ptr<trajectory::TrajectoryRecorder> recorder,
                  trajectory::ProvenanceResolver resolver)
        : config_(std::move(config)),
          environment_(std::move(environment)),
          recorder_(std::move(recorder)),
          resolver_(std::move(resolver)) {}

    TeacherRunnerConfig config_;
    std::unique_ptr<environment::EpisodicEnvironment> environment_;
    std::unique_ptr<trajectory::TrajectoryRecorder> recorder_;
    trajectory::ProvenanceResolver resolver_;
    bool has_run_ = false;
};

struct TeacherRunnerCreateResult final {
    std::optional<TeacherRunner> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

namespace detail {

struct TeacherRunnerTestAccess final {
    static PolicyRunnerResult run_with_test_selector(
        TeacherRunner& runner,
        std::uint8_t player,
        TeacherRunnerTestSelectorBehavior behavior,
        std::shared_ptr<std::size_t> selection_calls = nullptr);
};

}  // namespace detail

}  // namespace ygo::policy
