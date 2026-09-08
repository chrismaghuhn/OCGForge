#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/trajectory/policy_provenance.hpp"
#include "ygo/trajectory/replay_v2.hpp"

namespace ygo::trajectory::admission_v2 {

class AdmissionVerification final {
public:
    const std::string& public_gameplay_trajectory_id() const noexcept {
        return public_gameplay_trajectory_id_;
    }
    const std::string& trajectory_record_id() const noexcept {
        return entries_.empty() ? trajectory_record_id_ : entries_.front().trajectory_record_id;
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
    const std::string& shard_artifact_sha256() const noexcept {
        return shard_artifact_sha256_;
    }
    const std::string& restricted_evidence_artifact_sha256() const noexcept {
        return restricted_evidence_artifact_sha256_;
    }
    const std::vector<AdmissionEntryCommitmentV2>& entries() const noexcept {
        return entries_;
    }

private:
    AdmissionVerification(std::string public_gameplay_trajectory_id,
                          std::string trajectory_record_id,
                          std::string environment_semantic_id,
                          std::string episode_semantic_id,
                          const std::uint64_t final_engine_step_index,
                          std::string shard_artifact_sha256 = {},
                          std::string restricted_evidence_artifact_sha256 = {},
                          std::vector<AdmissionEntryCommitmentV2> entries = {})
        : public_gameplay_trajectory_id_(std::move(public_gameplay_trajectory_id)),
          trajectory_record_id_(std::move(trajectory_record_id)),
          environment_semantic_id_(std::move(environment_semantic_id)),
          episode_semantic_id_(std::move(episode_semantic_id)),
          final_engine_step_index_(final_engine_step_index),
          shard_artifact_sha256_(std::move(shard_artifact_sha256)),
          restricted_evidence_artifact_sha256_(std::move(restricted_evidence_artifact_sha256)),
          entries_(std::move(entries)) {}

    friend std::optional<AdmissionVerification> verify_episode_for_admission_v2(
        const EpisodeEnvelopeV2&, const std::optional<RestrictedReplayEvidenceV2>&,
        const replay_v2::ReplayOptions&, const ProvenanceResolver&, std::string*);
    friend std::optional<AdmissionVerification> verify_collection_for_admission_v2(
        const CandidateTrajectoryShardV2&, const RestrictedCollectionEvidenceBundleV2&,
        std::string_view, std::string_view, const replay_v2::ReplayOptions&,
        const ProvenanceResolver&, std::string*);

    std::string public_gameplay_trajectory_id_;
    std::string trajectory_record_id_;
    std::string environment_semantic_id_;
    std::string episode_semantic_id_;
    std::uint64_t final_engine_step_index_ = 0;
    std::string shard_artifact_sha256_;
    std::string restricted_evidence_artifact_sha256_;
    std::vector<AdmissionEntryCommitmentV2> entries_;
};

std::optional<AdmissionVerification> verify_episode_for_admission_v2(
    const EpisodeEnvelopeV2& envelope,
    const std::optional<RestrictedReplayEvidenceV2>& evidence,
    const replay_v2::ReplayOptions& options,
    const ProvenanceResolver& resolver,
    std::string* error = nullptr);

std::optional<AdmissionVerification> verify_collection_for_admission_v2(
    const CandidateTrajectoryShardV2& shard,
    const RestrictedCollectionEvidenceBundleV2& restricted_evidence,
    std::string_view candidate_shard_artifact_sha256,
    std::string_view restricted_evidence_artifact_sha256,
    const replay_v2::ReplayOptions& options,
    const ProvenanceResolver& resolver,
    std::string* error = nullptr);

}  // namespace ygo::trajectory::admission_v2
