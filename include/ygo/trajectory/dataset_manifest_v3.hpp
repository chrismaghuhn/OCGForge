#pragma once

#include <string>
#include <vector>

#include "ygo/trajectory/receipt_v3.hpp"

namespace ygo::trajectory::dataset_v3 {

inline constexpr char kDatasetManifestV3ContractId[] =
    "ocgforge.dataset_manifest.v3";
inline constexpr char kDatasetIdentityV3ContractId[] =
    "ocgforge.dataset_identity.v3";

struct DatasetManifestMemberV3 final {
    std::string trajectory_record_id;
    std::string public_gameplay_trajectory_id;
    std::string admission_receipt_id;
    std::string candidate_shard_artifact_sha256;
    std::string episode_envelope_sha256;
};

struct DatasetManifestV3 final {
    std::string dataset_manifest_schema_id = kDatasetManifestV3ContractId;
    std::string dataset_identity_schema_id = kDatasetIdentityV3ContractId;
    std::string trusted_trajectory_contract_id = kTrustedTrajectoryV3ContractId;
    std::string dataset_semantic_id;
    std::vector<DatasetManifestMemberV3> members;
};

std::vector<std::uint8_t> canonical_dataset_identity_bytes_v3(
    const std::vector<std::string>& trajectory_record_ids);
std::string dataset_semantic_id_v3(
    const std::vector<std::string>& trajectory_record_ids);

std::vector<std::uint8_t> canonical_dataset_manifest_bytes_v3(
    const DatasetManifestV3& value);
DecodeResult<DatasetManifestV3> decode_dataset_manifest_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

bool validate_dataset_manifest_v3(
    const DatasetManifestV3& value,
    const std::vector<VerifiedAdmissionReceiptV3>& receipts,
    std::string* error = nullptr);

}  // namespace ygo::trajectory::dataset_v3
