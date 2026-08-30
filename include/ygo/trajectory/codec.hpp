#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/trajectory/types.hpp"

namespace ygo::trajectory {

struct DecodeError final {
    std::string message;
};

template <typename T>
struct DecodeResult final {
    std::optional<T> value;
    std::optional<DecodeError> error;

    explicit operator bool() const noexcept { return value.has_value() && !error.has_value(); }
};

class ByteWriter final {
public:
    void u8(std::uint8_t value);
    void u16be(std::uint16_t value);
    void u32be(std::uint32_t value);
    void u64be(std::uint64_t value);
    void i32(std::int32_t value);
    void boolean(bool value);
    void string(std::string_view value);
    void bytes(const std::vector<std::uint8_t>& value);
    void raw(const std::vector<std::uint8_t>& value);

    const std::vector<std::uint8_t>& data() const noexcept { return data_; }
    std::vector<std::uint8_t> take() && noexcept { return std::move(data_); }

private:
    std::vector<std::uint8_t> data_;
};

class ByteReader final {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& data) noexcept : data_(data) {}

    bool u8(std::uint8_t& value) noexcept;
    bool u16be(std::uint16_t& value) noexcept;
    bool u32be(std::uint32_t& value) noexcept;
    bool u64be(std::uint64_t& value) noexcept;
    bool i32(std::int32_t& value) noexcept;
    bool boolean(bool& value) noexcept;
    bool string(std::string& value) noexcept;
    bool bytes(std::vector<std::uint8_t>& value) noexcept;
    bool raw(std::size_t length, std::vector<std::uint8_t>& value) noexcept;
    bool at_end() const noexcept { return offset_ == data_.size(); }
    std::size_t remaining() const noexcept { return data_.size() - offset_; }
    std::size_t position() const noexcept { return offset_; }
    const std::vector<std::uint8_t>& data() const noexcept { return data_; }
    bool set_position(std::size_t position) noexcept {
        if (position > data_.size()) {
            return false;
        }
        offset_ = position;
        return true;
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::size_t offset_ = 0;
};

bool is_valid_utf8(std::string_view value) noexcept;
bool is_lower_hex_digest(std::string_view value) noexcept;
bool is_canonical_identity(std::string_view value, std::string_view prefix) noexcept;

std::string compute_policy_artifact_id(const PolicyArtifact& value);
std::string compute_participant_policy_assignment_id(
    const ParticipantPolicyAssignment& value);
std::string compute_policy_rng_initialization_id(
    const PolicyRngInitializationIdentity& value);
std::string compute_policy_rng_stream_id(const PolicyRngStreamIdentity& value);

std::vector<std::uint8_t> canonical_policy_artifact_bytes(const PolicyArtifact& value);
DecodeResult<PolicyArtifact> decode_policy_artifact(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_participant_policy_assignment_bytes(
    const ParticipantPolicyAssignment& value);
DecodeResult<ParticipantPolicyAssignment> decode_participant_policy_assignment(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_policy_rng_initialization_identity_bytes(
    const PolicyRngInitializationIdentity& value);
DecodeResult<PolicyRngInitializationIdentity> decode_policy_rng_initialization_identity(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_policy_rng_stream_identity_bytes(
    const PolicyRngStreamIdentity& value);
DecodeResult<PolicyRngStreamIdentity> decode_policy_rng_stream_identity(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_policy_rng_decision_provenance_bytes(
    const PolicyRngDecisionProvenance& value);
DecodeResult<PolicyRngDecisionProvenance> decode_policy_rng_decision_provenance(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_policy_provenance_envelope_bytes(
    const PolicyProvenanceEnvelope& value);
DecodeResult<PolicyProvenanceEnvelope> decode_policy_provenance_envelope(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_collection_disposition_bytes(
    const CollectionDisposition& value);
DecodeResult<CollectionDisposition> decode_collection_disposition(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_environment_action_candidate_bytes(
    const environment::EnvironmentActionCandidate& value);
DecodeResult<environment::EnvironmentActionCandidate>
decode_public_environment_action_candidate(const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_environment_continuation_bytes(
    const environment::EnvironmentContinuationView& value);
DecodeResult<environment::EnvironmentContinuationView> decode_public_environment_continuation(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_environment_decision_request_bytes(
    const environment::EnvironmentDecisionRequest& value);
DecodeResult<environment::EnvironmentDecisionRequest> decode_public_environment_decision_request(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_frame_snapshot_bytes(const PublicFrameSnapshot& value);
DecodeResult<PublicFrameSnapshot> decode_public_frame_snapshot(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_decision_record_bytes(const DecisionRecord& value);
DecodeResult<DecisionRecord> decode_public_decision_record(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_policy_decision_attribution_bytes(
    const DecisionRecord& value);

std::vector<std::uint8_t> canonical_episode_closure_bytes(const EpisodeClosure& value);
std::vector<std::uint8_t> canonical_public_episode_closure_bytes(const EpisodeClosure& value);
DecodeResult<EpisodeClosure> decode_episode_closure(const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_episode_manifest_bytes(const EpisodeManifest& value);
DecodeResult<EpisodeManifest> decode_episode_manifest(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_collection_decision_record_bytes(
    const DecisionRecord& value);

std::vector<std::uint8_t> canonical_episode_envelope_bytes(const EpisodeEnvelope& value);
DecodeResult<EpisodeEnvelope> decode_episode_envelope(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_restricted_replay_evidence_bytes(
    const RestrictedReplayEvidence& value);
DecodeResult<RestrictedReplayEvidence> decode_restricted_replay_evidence(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string public_gameplay_trajectory_id(const EpisodeEnvelope& value);
std::string trajectory_record_id(const EpisodeEnvelope& value);

}  // namespace ygo::trajectory
