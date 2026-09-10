#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/trajectory/recorder.hpp"
#include "ygo/trajectory/types_v3.hpp"

namespace ygo::trajectory {

class TrajectoryRecorderV3 final {
public:
    TrajectoryRecorderV3(environment::CertifiedEnvironmentConfig config,
                         environment::EpisodeSpec spec,
                         PolicyProvenanceEnvelope policy_provenance);
    TrajectoryRecorderV3(environment::CertifiedEnvironmentConfig config,
                         environment::EpisodeSpec spec,
                         PolicyProvenanceEnvelope policy_provenance,
                         const ProvenanceResolver& resolver);

    TrajectoryRecorderV3(const TrajectoryRecorderV3&) = delete;
    TrajectoryRecorderV3& operator=(const TrajectoryRecorderV3&) = delete;
    TrajectoryRecorderV3(TrajectoryRecorderV3&&) = default;
    TrajectoryRecorderV3& operator=(TrajectoryRecorderV3&&) = default;

    bool on_reset_accepted(const environment::ResetAccepted& accepted,
                           const std::optional<TerminalViews>& terminal_views = std::nullopt,
                           std::string* error = nullptr);

    bool on_step_accepted(const environment::StepAccepted& accepted,
                          const PolicyRngDecisionProvenance& attribution,
                          const std::optional<TerminalViews>& terminal_views = std::nullopt,
                          std::string* error = nullptr);

    bool on_step_rejected(const environment::StepRejected& rejected, bool policy_origin,
                          std::string* error = nullptr);

    bool on_interrupt_accepted(const std::optional<environment::DecisionFrame>& pending_frame,
                               const environment::InterruptAccepted& accepted,
                               std::string* error = nullptr);

    bool on_failure(const environment::EpisodeFailure& failure,
                    std::string* error = nullptr);

    std::optional<EpisodeEnvelopeV3> seal(std::string* error = nullptr) const;

    RecorderLifecycle lifecycle() const noexcept { return lifecycle_; }
    const EpisodeManifestV3& manifest() const noexcept { return manifest_; }
    const std::vector<DecisionRecordV3>& records() const noexcept { return records_; }
    const std::optional<EpisodeClosureV3>& closure() const noexcept { return closure_; }

private:
    bool set_error(std::string* error, std::string message) const;
    bool fail_closed(environment::FailureCode code, environment::FailureStage stage,
                     bool mutation_may_have_occurred, std::string* error,
                     std::string message);
    bool capture_frame(const environment::DecisionFrame& frame,
                       std::optional<PublicFrameSnapshotV3>& output,
                       std::uint64_t expected_decision_index,
                       std::string* error) const;
    bool capture_terminal(const environment::EpisodeTerminal& terminal,
                          const TerminalViews& views, std::string* error);
    bool capture_interruption(const environment::EpisodeInterrupted& interruption,
                              std::string* error);
    bool capture_failure(const environment::EpisodeFailure& failure,
                         std::string* error);

    environment::CertifiedEnvironmentConfig config_;
    environment::EpisodeSpec spec_;
    ProvenanceResolver resolver_;
    EpisodeManifestV3 manifest_;
    RecorderLifecycle lifecycle_ = RecorderLifecycle::Empty;
    std::optional<PublicFrameSnapshotV3> current_frame_;
    std::vector<DecisionRecordV3> records_;
    std::optional<EpisodeClosureV3> closure_;
};

}  // namespace ygo::trajectory
