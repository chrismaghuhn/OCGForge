#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ygo/trajectory/shard_v3.hpp"

namespace ygo::trajectory {

inline constexpr char kRestrictedReplayEvidenceV3SchemaId[] =
    "ocgforge.restricted_replay_evidence.v3";
inline constexpr char kRestrictedCollectionEvidenceBundleV3ContractId[] =
    "ocgforge.restricted_collection_evidence_bundle.v3";

struct RestrictedReplayEvidenceV3 final {
    std::string restricted_replay_evidence_contract_id =
        kRestrictedReplayEvidenceV3SchemaId;
    std::string trusted_trajectory_contract_id = kTrustedTrajectoryV3ContractId;
    std::string episodic_environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV4ContractId);
    std::string episode_semantic_id;
    std::uint8_t closure_kind = 1;
    environment::InterruptionReason interruption_reason =
        environment::InterruptionReason::AdministrativeCancel;
    std::uint64_t engine_process_budget = 0;
    std::uint64_t semantic_action_budget = 0;
    std::uint64_t observed_engine_process_count = 0;
    std::uint64_t observed_semantic_action_count = 0;
    std::uint64_t final_engine_step_index = 0;
};

struct InterruptedEvidenceEntryV3 final {
    std::string episode_envelope_sha256;
    RestrictedReplayEvidenceV3 evidence;
};

struct RestrictedCollectionEvidenceBundleV3 final {
    std::string candidate_shard_artifact_sha256;
    std::vector<InterruptedEvidenceEntryV3> interrupted_episodes;
};

std::vector<std::uint8_t> canonical_restricted_replay_evidence_bytes_v3(
    const RestrictedReplayEvidenceV3& value);
DecodeResult<RestrictedReplayEvidenceV3> decode_restricted_replay_evidence_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;
std::string restricted_replay_evidence_artifact_sha256_v3(
    const RestrictedReplayEvidenceV3& value);

std::vector<std::uint8_t> canonical_restricted_collection_evidence_bundle_bytes_v3(
    const RestrictedCollectionEvidenceBundleV3& value);
DecodeResult<RestrictedCollectionEvidenceBundleV3>
decode_restricted_collection_evidence_bundle_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;
std::string restricted_collection_evidence_artifact_sha256_v3(
    const RestrictedCollectionEvidenceBundleV3& value);

}  // namespace ygo::trajectory
