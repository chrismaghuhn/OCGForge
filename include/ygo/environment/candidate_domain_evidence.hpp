#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ygo::environment {

inline constexpr std::string_view kCandidateDomainSchemaId = "ocgforge.candidate_domain.v1";
inline constexpr std::string_view kCandidateDomainEvidenceSchemaId =
    "ocgforge.candidate_domain_evidence.v1";

std::vector<std::uint8_t> canonical_candidate_domain_bytes(
    std::string_view request_kind, const std::vector<std::string>& semantic_keys);

std::string candidate_domain_digest(std::string_view request_kind,
                                    const std::vector<std::string>& semantic_keys);

struct CandidateDomainWitness final {
    std::uint64_t candidate_count = 0;
    std::string request_kind;
    std::string episode_semantic_id;
    std::uint64_t environment_decision_index = 0;
    std::uint64_t engine_step_index = 0;
    std::string protocol_decision_id;
    std::string candidate_domain_digest;
    std::vector<std::string> ordered_semantic_keys;
};

std::uint64_t candidate_domain_max(const std::vector<CandidateDomainWitness>& witnesses) noexcept;

std::uint64_t candidate_max_total(const std::vector<std::uint64_t>& per_job_maxima);

std::size_t select_g28_witness_index(const std::vector<CandidateDomainWitness>& witnesses);

}  // namespace ygo::environment
