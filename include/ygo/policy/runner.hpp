#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/production.hpp"
#include "ygo/trajectory/admission.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/recorder.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"
#include "ygo/trajectory/shard.hpp"

namespace ygo::policy {

using PolicySelector = std::function<PolicySelection(const PolicyInput&)>;

struct PolicyRunnerConfig final {
    environment::CertifiedEnvironmentConfig environment_config;
    environment::EpisodeSpec episode_spec;
    environment::RunControl run_control;
    trajectory::PolicyProvenanceEnvelope policy_provenance;
    std::array<PolicySelector, 2> selectors;
    // Each element is indexed by acting player; its typed identities are
    // checked against the corresponding participant assignment.
    std::array<RandomLegalExecutionBinding, 2> execution_bindings;
};

enum class PolicyRunnerDisposition : std::uint8_t {
    CleanAdmitted = 0,
    Quarantined = 1,
    Failed = 2,
};

struct PolicyRunnerResult final {
    PolicyRunnerDisposition disposition = PolicyRunnerDisposition::Failed;
    std::string diagnostic;
    std::optional<PolicyError> policy_error;
    std::optional<trajectory::EpisodeEnvelope> envelope;
    std::optional<trajectory::CandidateTrajectoryShard> candidate_shard;
    std::optional<trajectory::RestrictedCollectionEvidenceBundle> restricted_evidence;
    std::optional<trajectory::admission::AdmissionVerification> admission_verification;
    std::optional<trajectory::VerifiedAdmissionReceipt> admission_receipt;
    std::optional<trajectory::DatasetManifest> dataset_manifest;
};

struct PolicyRunnerCreateResult;

class PolicyRunner final {
public:
    static PolicyRunnerCreateResult create(PolicyRunnerConfig config) noexcept;

    PolicyRunner(const PolicyRunner&) = delete;
    PolicyRunner& operator=(const PolicyRunner&) = delete;
    PolicyRunner(PolicyRunner&&) = default;
    PolicyRunner& operator=(PolicyRunner&&) = default;

    PolicyRunnerResult run() noexcept;

private:
    PolicyRunner(PolicyRunnerConfig config,
                 std::unique_ptr<environment::EpisodicEnvironment> environment,
                 std::unique_ptr<trajectory::TrajectoryRecorder> recorder,
                 trajectory::ProvenanceResolver resolver)
        : config_(std::move(config)),
          environment_(std::move(environment)),
          recorder_(std::move(recorder)),
          resolver_(std::move(resolver)) {}

    PolicyRunnerConfig config_;
    std::unique_ptr<environment::EpisodicEnvironment> environment_;
    std::unique_ptr<trajectory::TrajectoryRecorder> recorder_;
    trajectory::ProvenanceResolver resolver_;
    bool has_run_ = false;
};

struct PolicyRunnerCreateResult final {
    std::optional<PolicyRunner> value;
    std::optional<PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

}  // namespace ygo::policy
