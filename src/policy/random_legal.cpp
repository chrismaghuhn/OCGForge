#include "ygo/policy/random_legal.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"

namespace ygo::policy {
namespace {

PolicySelection failure(const PolicyErrorCode code, const char* message) {
    PolicySelection result;
    result.error = PolicyError{code, message};
    return result;
}

}  // namespace

RandomLegalPolicyCreateResult create_random_legal_policy(
    const PolicyRngInitializationInput& input) noexcept {
    try {
        const auto rng = create_sha256_counter_rng(input);
        if (!rng) {
            return {std::nullopt, rng.error};
        }
        RandomLegalPolicy policy(std::move(*rng.value));
        return {std::optional<RandomLegalPolicy>(std::move(policy)), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "RandomLegal policy construction failed"}};
    }
}

PolicySelection RandomLegalPolicy::select(const PolicyInput& input) noexcept {
    try {
        (void)input.observation;
        const auto& candidates = input.candidates;
        if (candidates.empty()) {
            return failure(PolicyErrorCode::EmptyCandidateDomain,
                           "RandomLegal received an empty candidate domain");
        }
        if (candidates.size() > std::numeric_limits<std::uint64_t>::max()) {
            return failure(PolicyErrorCode::InvalidCandidateDomain,
                           "RandomLegal candidate domain exceeds u64 addressing");
        }
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (!environment::is_public_action_key(candidates[index].public_action_key)) {
                return failure(PolicyErrorCode::InvalidCandidateDomain,
                               "RandomLegal received a noncanonical public action key");
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (candidates[previous].public_action_key ==
                    candidates[index].public_action_key) {
                    return failure(PolicyErrorCode::InvalidCandidateDomain,
                                   "RandomLegal received duplicate public action keys");
                }
            }
        }

        const auto sampled = rng_.uniform_below_u64(
            static_cast<std::uint64_t>(candidates.size()));
        if (!sampled) {
            PolicySelection result;
            result.error = sampled.error;
            return result;
        }
        const auto selected_index = static_cast<std::size_t>(*sampled.value);
        if (selected_index >= candidates.size()) {
            return failure(PolicyErrorCode::InvalidCandidateDomain,
                           "RandomLegal sampler returned an invalid domain position");
        }

        PolicySelection result;
        PolicySelectionResult selection;
        selection.public_action_key = candidates[selected_index].public_action_key;
        selection.rng_cursor = PolicyRngCursorTransition{sampled.pre_cursor,
                                                          sampled.post_cursor};
        result.value = std::move(selection);
        return result;
    } catch (const std::exception& error) {
        return failure(PolicyErrorCode::InvalidConfiguration, error.what());
    } catch (...) {
        return failure(PolicyErrorCode::InvalidConfiguration,
                       "RandomLegal selection failed");
    }
}

}  // namespace ygo::policy
