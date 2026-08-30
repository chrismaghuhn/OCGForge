#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ygo/trajectory/shard.hpp"

namespace ygo::trajectory {

class ProvenanceResolver;

inline constexpr std::string_view kRestrictedCollectionEvidenceBundleContractId =
    "ocgforge.restricted_collection_evidence_bundle.v1";

std::vector<std::uint8_t> canonical_restricted_collection_evidence_bundle_bytes(
    const RestrictedCollectionEvidenceBundle& value);
DecodeResult<RestrictedCollectionEvidenceBundle>
decode_restricted_collection_evidence_bundle(const std::vector<std::uint8_t>& bytes) noexcept;

std::string restricted_collection_evidence_artifact_sha256(
    const RestrictedCollectionEvidenceBundle& value);

bool validate_restricted_collection_evidence_bundle(
    const RestrictedCollectionEvidenceBundle& value,
    const CandidateTrajectoryShard& shard,
    std::string_view candidate_shard_artifact_sha256,
    std::string* error = nullptr);

bool validate_restricted_collection_evidence_bundle(
    const RestrictedCollectionEvidenceBundle& value,
    const CandidateTrajectoryShard& shard,
    std::string_view candidate_shard_artifact_sha256,
    const ProvenanceResolver& resolver,
    std::string* error = nullptr);

}  // namespace ygo::trajectory
