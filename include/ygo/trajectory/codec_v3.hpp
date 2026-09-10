#pragma once

#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/types_v3.hpp"

namespace ygo::trajectory {

std::vector<std::uint8_t> canonical_public_environment_action_candidate_bytes_v3(
    const environment::EnvironmentActionCandidate& value);
DecodeResult<environment::EnvironmentActionCandidate>
decode_public_environment_action_candidate_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_environment_decision_request_bytes_v3(
    const environment::EnvironmentDecisionRequest& value);
DecodeResult<environment::EnvironmentDecisionRequest>
decode_public_environment_decision_request_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_frame_snapshot_bytes_v3(
    const PublicFrameSnapshotV3& value);
DecodeResult<PublicFrameSnapshotV3> decode_public_frame_snapshot_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_decision_record_bytes_v3(
    const DecisionRecordV3& value);
DecodeResult<DecisionRecordV3> decode_public_decision_record_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_policy_decision_attribution_bytes_v3(
    const DecisionRecordV3& value);
std::vector<std::uint8_t> canonical_collection_decision_record_bytes_v3(
    const DecisionRecordV3& value);
DecodeResult<DecisionRecordV3> decode_collection_decision_record_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_episode_closure_bytes_v3(
    const EpisodeClosureV3& value);
DecodeResult<EpisodeClosureV3> decode_episode_closure_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_episode_manifest_bytes_v3(
    const EpisodeManifestV3& value);
DecodeResult<EpisodeManifestV3> decode_episode_manifest_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_episode_envelope_bytes_v3(
    const EpisodeEnvelopeV3& value);
DecodeResult<EpisodeEnvelopeV3> decode_episode_envelope_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

}  // namespace ygo::trajectory
