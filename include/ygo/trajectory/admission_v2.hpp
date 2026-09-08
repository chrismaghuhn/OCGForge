#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "ygo/trajectory/policy_provenance.hpp"
#include "ygo/trajectory/replay_v2.hpp"

namespace ygo::trajectory::admission_v2 {

class AdmissionVerification final {
public:
    const std::string& public_gameplay_trajectory_id() const noexcept {
        return public_gameplay_trajectory_id_;
    }
    const std::string& trajectory_record_id() const noexcept {
        return trajectory_record_id_;
    }
    const std::string& environment_semantic_id() const noexcept {
        return environment_semantic_id_;
    }
    const std::string& episode_semantic_id() const noexcept {
        return episode_semantic_id_;
    }
    std::uint64_t final_engine_step_index() const noexcept {
        return final_engine_step_index_;
    }

private:
    AdmissionVerification(std::string public_gameplay_trajectory_id,
                          std::string trajectory_record_id,
                          std::string environment_semantic_id,
                          std::string episode_semantic_id,
                          const std::uint64_t final_engine_step_index)
        : public_gameplay_trajectory_id_(std::move(public_gameplay_trajectory_id)),
          trajectory_record_id_(std::move(trajectory_record_id)),
          environment_semantic_id_(std::move(environment_semantic_id)),
          episode_semantic_id_(std::move(episode_semantic_id)),
          final_engine_step_index_(final_engine_step_index) {}

    friend std::optional<AdmissionVerification> verify_episode_for_admission_v2(
        const EpisodeEnvelopeV2&, const std::optional<RestrictedReplayEvidenceV2>&,
        const replay_v2::ReplayOptions&, const ProvenanceResolver&, std::string*);

    std::string public_gameplay_trajectory_id_;
    std::string trajectory_record_id_;
    std::string environment_semantic_id_;
    std::string episode_semantic_id_;
    std::uint64_t final_engine_step_index_ = 0;
};

std::optional<AdmissionVerification> verify_episode_for_admission_v2(
    const EpisodeEnvelopeV2& envelope,
    const std::optional<RestrictedReplayEvidenceV2>& evidence,
    const replay_v2::ReplayOptions& options,
    const ProvenanceResolver& resolver,
    std::string* error = nullptr);

}  // namespace ygo::trajectory::admission_v2
