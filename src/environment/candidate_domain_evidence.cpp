#include "ygo/environment/candidate_domain_evidence.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "ygo/trace/sha256.hpp"

namespace ygo::environment {
namespace {

void append_u32be(std::vector<std::uint8_t>& bytes, const std::uint32_t integer) {
    bytes.push_back(static_cast<std::uint8_t>(integer >> 24));
    bytes.push_back(static_cast<std::uint8_t>(integer >> 16));
    bytes.push_back(static_cast<std::uint8_t>(integer >> 8));
    bytes.push_back(static_cast<std::uint8_t>(integer));
}

void append_count(std::vector<std::uint8_t>& bytes, const std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("candidate domain exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
    append_count(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

bool witness_precedes(const CandidateDomainWitness& left, const CandidateDomainWitness& right) {
    if (left.candidate_count != right.candidate_count) {
        return left.candidate_count > right.candidate_count;
    }
    if (left.episode_semantic_id != right.episode_semantic_id) {
        return left.episode_semantic_id < right.episode_semantic_id;
    }
    if (left.environment_decision_index != right.environment_decision_index) {
        return left.environment_decision_index < right.environment_decision_index;
    }
    if (left.engine_step_index != right.engine_step_index) {
        return left.engine_step_index < right.engine_step_index;
    }
    if (left.protocol_decision_id != right.protocol_decision_id) {
        return left.protocol_decision_id < right.protocol_decision_id;
    }
    return left.candidate_domain_digest < right.candidate_domain_digest;
}

}  // namespace

std::vector<std::uint8_t> canonical_candidate_domain_bytes(
    const std::string_view request_kind, const std::vector<std::string>& semantic_keys) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(64 + semantic_keys.size() * 16);
    append_string(bytes, kCandidateDomainSchemaId);
    append_string(bytes, request_kind);
    append_count(bytes, semantic_keys.size());
    for (const auto& key : semantic_keys) {
        append_string(bytes, key);
    }
    return bytes;
}

std::string candidate_domain_digest(const std::string_view request_kind,
                                    const std::vector<std::string>& semantic_keys) {
    return trace::sha256_bytes(canonical_candidate_domain_bytes(request_kind, semantic_keys));
}

std::uint64_t candidate_domain_max(const std::vector<CandidateDomainWitness>& witnesses) noexcept {
    std::uint64_t result = 0;
    for (const auto& witness : witnesses) {
        result = std::max(result, witness.candidate_count);
    }
    return result;
}

std::uint64_t candidate_max_total(const std::vector<std::uint64_t>& per_job_maxima) {
    std::uint64_t result = 0;
    for (const auto value : per_job_maxima) {
        if (std::numeric_limits<std::uint64_t>::max() - result < value) {
            throw std::overflow_error("candidate_max_total overflow");
        }
        result += value;
    }
    return result;
}

std::size_t select_g28_witness_index(const std::vector<CandidateDomainWitness>& witnesses) {
    if (witnesses.empty()) {
        throw std::invalid_argument("G28 witness corpus is empty");
    }
    return static_cast<std::size_t>(std::distance(
        witnesses.begin(), std::min_element(witnesses.begin(), witnesses.end(), witness_precedes)));
}

}  // namespace ygo::environment
