#pragma once

#include <string_view>

#include "ygo/trajectory/policy_provenance.hpp"

namespace ygo::policy {

inline constexpr std::string_view kRandomLegalProducerImplementationIdentity =
    "ocgforge.policy.random_legal.v1";
inline constexpr std::string_view kDirectExecutionInferenceAdapterIdentity =
    "ocgforge.policy.direct_execution.v1";
inline constexpr std::string_view kPublicObservationAdapterIdentity =
    "ocgforge.policy.public_observation.v1";
inline constexpr std::string_view kPublicActionKeyAdapterIdentity =
    "ocgforge.policy.public_action_key.v1";
inline constexpr std::string_view kUniformBelowU64SamplingContractIdentity =
    "ocgforge.policy.uniform_below_u64.v1";
inline constexpr std::string_view kSha256CounterPolicyRngContractIdentity =
    "ocgforge.policy_rng.sha256_counter.v1";

trajectory::ProvenanceResolver make_production_policy_provenance_resolver();

}  // namespace ygo::policy
