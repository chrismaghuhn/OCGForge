#include "ygo/trajectory/receipt.hpp"

#include <cstddef>
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
        throw std::length_error("admission receipt entry count exceeds u32");
    }
}

void require_identity(const std::string& value, const std::string_view prefix,
                      const char* field) {
    if (!is_canonical_identity(value, prefix)) {
        throw std::invalid_argument(std::string("admission receipt ") + field +
                                    " has invalid identity");
    }
}

void validate_receipt(const AdmissionReceipt& value) {
    if (value.admission_contract_id != kAdmissionReceiptContractId) {
        throw std::invalid_argument("admission receipt has an unknown contract");
    }
    if (!is_lower_hex_digest(value.candidate_shard_artifact_sha256) ||
        !is_lower_hex_digest(value.restricted_evidence_artifact_sha256)) {
        throw std::invalid_argument("admission receipt artifact binding is invalid");
    }
    require_length(value.entries.size());
    std::string previous_record_id;
    for (const auto& entry : value.entries) {
        require_identity(entry.trajectory_record_id, "trajectory_record.v1.", "record ID");
        require_identity(entry.public_gameplay_trajectory_id,
                         "public_gameplay_trajectory.v1.", "gameplay ID");
        if (!is_lower_hex_digest(entry.environment_semantic_id) ||
            !is_lower_hex_digest(entry.episode_semantic_id) ||
            !is_lower_hex_digest(entry.episode_envelope_sha256) || entry.closure_kind > 1) {
            throw std::invalid_argument("admission receipt commitment is invalid");
        }
        if (!previous_record_id.empty() && entry.trajectory_record_id <= previous_record_id) {
            throw std::invalid_argument("admission receipt commitments are not strictly sorted");
        }
        previous_record_id = entry.trajectory_record_id;
    }
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_admission_receipt_bytes(const AdmissionReceipt& value) {
    validate_receipt(value);
    ByteWriter writer;
    writer.string(kAdmissionReceiptContractId);
    writer.string(kAdmissionReceiptContractId);
    writer.string(value.admission_contract_id);
    writer.string(value.candidate_shard_artifact_sha256);
    writer.string(value.restricted_evidence_artifact_sha256);
    require_length(value.entries.size());
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

DecodeResult<AdmissionReceipt> decode_admission_receipt(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        AdmissionReceipt value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kAdmissionReceiptContractId ||
            !reader.string(schema) || schema != kAdmissionReceiptContractId ||
            !reader.string(value.admission_contract_id) ||
            !reader.string(value.candidate_shard_artifact_sha256) ||
            !reader.string(value.restricted_evidence_artifact_sha256) ||
            !reader.u32be(count)) {
            return failure<AdmissionReceipt>("malformed admission receipt header");
        }
        if (count > reader.remaining() / 5) {
            return failure<AdmissionReceipt>("admission receipt entry count exceeds input");
        }
        value.entries.reserve(count);
        std::string previous_record_id;
        for (std::uint32_t index = 0; index < count; ++index) {
            AdmissionEntryCommitment entry;
            if (!reader.string(entry.trajectory_record_id) ||
                !reader.string(entry.public_gameplay_trajectory_id) ||
                !reader.string(entry.environment_semantic_id) ||
                !reader.string(entry.episode_semantic_id) ||
                !reader.string(entry.episode_envelope_sha256) ||
                !reader.u8(entry.closure_kind)) {
                return failure<AdmissionReceipt>("truncated admission receipt commitment");
            }
            if (!previous_record_id.empty() && entry.trajectory_record_id <= previous_record_id) {
                return failure<AdmissionReceipt>("unsorted or duplicate admission commitment");
            }
            previous_record_id = entry.trajectory_record_id;
            value.entries.push_back(std::move(entry));
        }
        if (!reader.at_end()) {
            return failure<AdmissionReceipt>("admission receipt has trailing bytes");
        }
        validate_receipt(value);
        if (canonical_admission_receipt_bytes(value) != bytes) {
            return failure<AdmissionReceipt>("noncanonical admission receipt");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<AdmissionReceipt>(error.what());
    } catch (...) {
        return failure<AdmissionReceipt>("admission receipt decode threw");
    }
}

std::string admission_receipt_id(const AdmissionReceipt& value) {
    return "admission_receipt.v1." +
           trace::sha256_bytes(canonical_admission_receipt_bytes(value));
}

std::optional<VerifiedAdmissionReceipt> issue_admission_receipt(
    const admission::AdmissionVerification& verification,
    std::string* error) {
    try {
        AdmissionReceipt receipt;
        receipt.candidate_shard_artifact_sha256 = verification.candidate_shard_artifact_sha256();
        receipt.restricted_evidence_artifact_sha256 =
            verification.restricted_evidence_artifact_sha256();
        receipt.entries = verification.entries();
        (void)canonical_admission_receipt_bytes(receipt);
        return VerifiedAdmissionReceipt(std::move(receipt));
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return std::nullopt;
    } catch (...) {
        set_error(error, "admission receipt issuance threw");
        return std::nullopt;
    }
}

}  // namespace ygo::trajectory
