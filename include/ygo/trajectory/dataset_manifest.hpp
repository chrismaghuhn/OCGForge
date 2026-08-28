#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ygo/trajectory/receipt.hpp"

namespace ygo::trajectory::dataset {

inline constexpr std::string_view kDatasetManifestContractId =
    "ocgforge.dataset_manifest.v1";
inline constexpr std::string_view kDatasetIdentityContractId =
    "ocgforge.dataset_identity.v1";

std::vector<std::uint8_t> canonical_dataset_identity_bytes(
    const std::vector<std::string>& trajectory_record_ids);
std::string dataset_semantic_id(
    const std::vector<std::string>& trajectory_record_ids);

std::vector<std::uint8_t> canonical_dataset_manifest_bytes(const DatasetManifest& value);
DecodeResult<DatasetManifest> decode_dataset_manifest(
    const std::vector<std::uint8_t>& bytes) noexcept;

bool validate_dataset_manifest(const DatasetManifest& value,
                               const std::vector<AdmissionReceipt>& verified_receipts,
                               std::string* error = nullptr);

}  // namespace ygo::trajectory::dataset
