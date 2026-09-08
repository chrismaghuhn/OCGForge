#pragma once

#include <string>
#include <vector>

#include "ygo/trajectory/shard_v2.hpp"

namespace ygo::trajectory {

std::vector<std::uint8_t> canonical_restricted_collection_evidence_bundle_bytes_v2(
    const RestrictedCollectionEvidenceBundleV2& value);
DecodeResult<RestrictedCollectionEvidenceBundleV2>
decode_restricted_collection_evidence_bundle_v2(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string restricted_collection_evidence_artifact_sha256_v2(
    const RestrictedCollectionEvidenceBundleV2& value);

}  // namespace ygo::trajectory
