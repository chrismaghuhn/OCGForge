#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/trajectory/admission.hpp"

namespace ygo::trajectory {

inline constexpr char kAdmissionReceiptContractId[] = "ocgforge.admission_receipt.v1";

std::vector<std::uint8_t> canonical_admission_receipt_bytes(const AdmissionReceipt& value);
DecodeResult<AdmissionReceipt> decode_admission_receipt(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string admission_receipt_id(const AdmissionReceipt& value);

std::optional<AdmissionReceipt> issue_admission_receipt(
    const admission::AdmissionVerification& verification,
    std::string* error = nullptr);

}  // namespace ygo::trajectory
