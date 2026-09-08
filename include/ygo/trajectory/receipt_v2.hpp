#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ygo/trajectory/admission_v2.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::trajectory {

std::vector<std::uint8_t> canonical_admission_receipt_bytes_v2(
    const AdmissionReceiptV2& value);
DecodeResult<AdmissionReceiptV2> decode_admission_receipt_v2(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string admission_receipt_id_v2(const AdmissionReceiptV2& value);

class VerifiedAdmissionReceiptV2 final {
public:
    const AdmissionReceiptV2& receipt() const noexcept { return receipt_; }

private:
    explicit VerifiedAdmissionReceiptV2(AdmissionReceiptV2 receipt)
        : receipt_(std::move(receipt)) {}

    friend std::optional<VerifiedAdmissionReceiptV2> issue_admission_receipt_v2(
        const admission_v2::AdmissionVerification&, std::string*);

    AdmissionReceiptV2 receipt_;
};

std::optional<VerifiedAdmissionReceiptV2> issue_admission_receipt_v2(
    const admission_v2::AdmissionVerification& verification,
    std::string* error = nullptr);

}  // namespace ygo::trajectory
