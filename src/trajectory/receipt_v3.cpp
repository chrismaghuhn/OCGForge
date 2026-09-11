#include "ygo/trajectory/receipt_v3.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory {
namespace {

template <typename T>
DecodeResult<T> failure(std::string message) noexcept {
    DecodeResult<T> result;
    result.error = DecodeError{std::move(message)};
    return result;
}

template <typename T>
DecodeResult<T> success(T value) noexcept {
    DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

void require_length(const std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("V3 admission receipt entry count exceeds u32");
    }
}

void require_identity(const std::string& value, const std::string_view prefix,
                      const char* field) {
    if (!is_canonical_identity(value, prefix)) {
        throw std::invalid_argument(std::string("V3 receipt ") + field +
                                    " has invalid identity");
    }
}

void validate_receipt(const AdmissionReceiptV3& value) {
    if (value.admission_contract_id != kAdmissionReceiptV3ContractId ||
        !is_lower_hex_digest(value.candidate_shard_artifact_sha256) ||
        !is_lower_hex_digest(value.restricted_evidence_artifact_sha256)) {
        throw std::invalid_argument("V3 admission receipt header is invalid");
    }
    require_length(value.entries.size());
    std::string previous;
    for (const auto& entry : value.entries) {
        require_identity(entry.trajectory_record_id, "trajectory_record.v3.", "record ID");
        require_identity(entry.public_gameplay_trajectory_id,
                         "public_gameplay_trajectory.v3.", "gameplay ID");
        if (!is_lower_hex_digest(entry.environment_semantic_id) ||
            !is_lower_hex_digest(entry.episode_semantic_id) ||
            !is_lower_hex_digest(entry.episode_envelope_sha256) ||
            entry.closure_kind > 2 ||
            (!previous.empty() && entry.trajectory_record_id <= previous)) {
            throw std::invalid_argument("V3 admission receipt commitment is invalid");
        }
        previous = entry.trajectory_record_id;
    }
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_admission_receipt_bytes_v3(
    const AdmissionReceiptV3& value) {
    validate_receipt(value);
    ByteWriter writer;
    writer.string(kAdmissionReceiptV3ContractId);
    writer.string(kAdmissionReceiptV3ContractId);
    writer.string(value.admission_contract_id);
    writer.string(value.candidate_shard_artifact_sha256);
    writer.string(value.restricted_evidence_artifact_sha256);
    writer.u32be(static_cast<std::uint32_t>(value.entries.size()));
    for (const auto& entry : value.entries) {
        writer.string(entry.trajectory_record_id);
        writer.string(entry.public_gameplay_trajectory_id);
        writer.string(entry.environment_semantic_id);
        writer.string(entry.episode_semantic_id);
        writer.string(entry.episode_envelope_sha256);
        writer.u8(entry.closure_kind);
    }
    return std::move(writer).take();
}

DecodeResult<AdmissionReceiptV3> decode_admission_receipt_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        AdmissionReceiptV3 value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kAdmissionReceiptV3ContractId ||
            !reader.string(schema) || schema != kAdmissionReceiptV3ContractId ||
            !reader.string(value.admission_contract_id) ||
            !reader.string(value.candidate_shard_artifact_sha256) ||
            !reader.string(value.restricted_evidence_artifact_sha256) ||
            !reader.u32be(count) || count > reader.remaining() / 6) {
            return failure<AdmissionReceiptV3>("malformed V3 receipt header");
        }
        value.entries.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            AdmissionEntryCommitmentV3 entry;
            if (!reader.string(entry.trajectory_record_id) ||
                !reader.string(entry.public_gameplay_trajectory_id) ||
                !reader.string(entry.environment_semantic_id) ||
                !reader.string(entry.episode_semantic_id) ||
                !reader.string(entry.episode_envelope_sha256) ||
                !reader.u8(entry.closure_kind)) {
                return failure<AdmissionReceiptV3>("truncated V3 receipt commitment");
            }
            value.entries.push_back(std::move(entry));
        }
        if (!reader.at_end() || canonical_admission_receipt_bytes_v3(value) != bytes) {
            return failure<AdmissionReceiptV3>("noncanonical V3 admission receipt");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<AdmissionReceiptV3>(error.what());
    } catch (...) {
        return failure<AdmissionReceiptV3>("V3 admission receipt decode threw");
    }
}

std::string admission_receipt_id_v3(const AdmissionReceiptV3& value) {
    return "admission_receipt.v3." +
           trace::sha256_bytes(canonical_admission_receipt_bytes_v3(value));
}

std::optional<VerifiedAdmissionReceiptV3> issue_admission_receipt_v3(
    const admission_v3::AdmissionVerification& verification,
    std::string* error) {
    try {
        AdmissionReceiptV3 receipt;
        receipt.candidate_shard_artifact_sha256 = verification.shard_artifact_sha256();
        receipt.restricted_evidence_artifact_sha256 =
            verification.restricted_evidence_artifact_sha256();
        receipt.entries = verification.entries();
        (void)canonical_admission_receipt_bytes_v3(receipt);
        return VerifiedAdmissionReceiptV3(std::move(receipt));
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return std::nullopt;
    } catch (...) {
        set_error(error, "V3 admission receipt issuance threw");
        return std::nullopt;
    }
}

}  // namespace ygo::trajectory
