#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace ygo::trajectory {

struct TerminalViews final {
    environment::PublicEnvironmentObservation player_0;
    environment::PublicEnvironmentObservation player_1;
};

struct RecorderError final {
    std::string message;
};

enum class RecorderLifecycle : std::uint8_t {
    Empty = 0,
    AwaitingAction = 1,
    Closed = 2,
};

class TrajectoryRecorder final {
public:
    TrajectoryRecorder(environment::CertifiedEnvironmentConfig config,
                       environment::EpisodeSpec spec,
                       PolicyProvenanceEnvelope policy_provenance);
    TrajectoryRecorder(environment::CertifiedEnvironmentConfig config,
                       environment::EpisodeSpec spec,
                       PolicyProvenanceEnvelope policy_provenance,
                       const ProvenanceResolver& resolver);

    TrajectoryRecorder(const TrajectoryRecorder&) = delete;
    TrajectoryRecorder& operator=(const TrajectoryRecorder&) = delete;
    TrajectoryRecorder(TrajectoryRecorder&&) = default;
    TrajectoryRecorder& operator=(TrajectoryRecorder&&) = default;

    bool on_reset_accepted(const environment::ResetAccepted& accepted,
                           const std::optional<TerminalViews>& terminal_views = std::nullopt,
                           std::string* error = nullptr);

    bool on_step_accepted(const environment::StepAccepted& accepted,
                          const PolicyRngDecisionProvenance& attribution,
                          const std::optional<TerminalViews>& terminal_views = std::nullopt,
                          std::string* error = nullptr);

    // `policy_origin` is an observation made by the collector, not a value
    // inferred from the rejection payload. Rejected submissions are never
    // persisted; only the public rejection-code classification may quarantine
    // the manifest.
    bool on_step_rejected(const environment::StepRejected& rejected, bool policy_origin,
                          std::string* error = nullptr);

    bool on_interrupt_accepted(const std::optional<environment::DecisionFrame>& pending_frame,
                               const environment::InterruptAccepted& accepted,
                               std::string* error = nullptr);

    bool on_failure(const environment::EpisodeFailure& failure,
                    std::string* error = nullptr);

    std::optional<EpisodeEnvelope> seal(std::string* error = nullptr) const;

    RecorderLifecycle lifecycle() const noexcept { return lifecycle_; }
    const EpisodeManifest& manifest() const noexcept { return manifest_; }
    const std::vector<DecisionRecord>& records() const noexcept { return records_; }
    const std::optional<EpisodeClosure>& closure() const noexcept { return closure_; }

private:
    bool set_error(std::string* error, std::string message) const;
    bool fail_closed(environment::FailureCode code, environment::FailureStage stage,
                     bool mutation_may_have_occurred, std::string* error,
                     std::string message);
    bool capture_frame(const environment::DecisionFrame& frame,
                       std::optional<PublicFrameSnapshot>& output,
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
    EpisodeManifest manifest_;
    RecorderLifecycle lifecycle_ = RecorderLifecycle::Empty;
    std::optional<PublicFrameSnapshot> current_frame_;
    std::vector<DecisionRecord> records_;
    std::optional<EpisodeClosure> closure_;
};

}  // namespace ygo::trajectory
