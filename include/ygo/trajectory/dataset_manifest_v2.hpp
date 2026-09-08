#pragma once

#include <string>
#include <vector>

#include "ygo/trajectory/receipt_v2.hpp"

namespace ygo::trajectory::dataset_v2 {

std::vector<std::uint8_t> canonical_dataset_identity_bytes_v2(
    const std::vector<std::string>& trajectory_record_ids);
std::string dataset_semantic_id_v2(
    const std::vector<std::string>& trajectory_record_ids);

std::vector<std::uint8_t> canonical_dataset_manifest_bytes_v2(
    const DatasetManifestV2& value);
DecodeResult<DatasetManifestV2> decode_dataset_manifest_v2(
    const std::vector<std::uint8_t>& bytes) noexcept;

bool validate_dataset_manifest_v2(
    const DatasetManifestV2& value,
    const std::vector<VerifiedAdmissionReceiptV2>& receipts,
    std::string* error = nullptr);

}  // namespace ygo::trajectory::dataset_v2
