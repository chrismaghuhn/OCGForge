#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ygo/trajectory/admission_v3.hpp"
#include "ygo/trajectory/codec_v3.hpp"

namespace ygo::trajectory {

inline constexpr char kAdmissionReceiptV3ContractId[] =
    "ocgforge.admission_receipt.v3";

struct AdmissionReceiptV3 final {
    std::string admission_contract_id = kAdmissionReceiptV3ContractId;
    std::string candidate_shard_artifact_sha256;
    std::string restricted_evidence_artifact_sha256;
    std::vector<AdmissionEntryCommitmentV3> entries;
};

std::vector<std::uint8_t> canonical_admission_receipt_bytes_v3(
    const AdmissionReceiptV3& value);
DecodeResult<AdmissionReceiptV3> decode_admission_receipt_v3(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::string admission_receipt_id_v3(const AdmissionReceiptV3& value);

class VerifiedAdmissionReceiptV3 final {
public:
    const AdmissionReceiptV3& receipt() const noexcept { return receipt_; }

private:
    explicit VerifiedAdmissionReceiptV3(AdmissionReceiptV3 receipt)
        : receipt_(std::move(receipt)) {}

    friend std::optional<VerifiedAdmissionReceiptV3> issue_admission_receipt_v3(
        const admission_v3::AdmissionVerification&, std::string*);

    AdmissionReceiptV3 receipt_;
};

std::optional<VerifiedAdmissionReceiptV3> issue_admission_receipt_v3(
    const admission_v3::AdmissionVerification& verification,
    std::string* error = nullptr);

}  // namespace ygo::trajectory
