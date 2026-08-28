#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ygo/trajectory/admission.hpp"

namespace ygo::trajectory {

inline constexpr char kAdmissionReceiptContractId[] = "ocgforge.admission_receipt.v1";

std::vector<std::uint8_t> canonical_admission_receipt_bytes(const AdmissionReceipt& value);
DecodeResult<AdmissionReceipt> decode_admission_receipt(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string admission_receipt_id(const AdmissionReceipt& value);

class VerifiedAdmissionReceipt final {
public:
    const AdmissionReceipt& receipt() const noexcept { return receipt_; }

private:
    explicit VerifiedAdmissionReceipt(AdmissionReceipt receipt)
        : receipt_(std::move(receipt)) {}

    friend std::optional<VerifiedAdmissionReceipt> issue_admission_receipt(
        const admission::AdmissionVerification&, std::string*);

    AdmissionReceipt receipt_;
};

// Decoding a canonical receipt yields a value for codec inspection. Dataset
// membership requires this separate capability, which can only be issued from
// a completed AdmissionVerification.
std::optional<VerifiedAdmissionReceipt> issue_admission_receipt(
    const admission::AdmissionVerification& verification,
    std::string* error = nullptr);

}  // namespace ygo::trajectory
