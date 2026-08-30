#include "ygo/policy/rng.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace ygo::policy {
namespace {

constexpr std::string_view kParticipantPolicyAssignmentIdentityPrefix =
    "participant_policy_assignment.v1.";
constexpr std::string_view kPolicyRngInitializationIdentityPrefix =
    "policy_rng_initialization.v1.";

PolicyError make_error(const PolicyErrorCode code, const char* message) {
    return PolicyError{code, message};
}

bool valid_initialization_input(const PolicyRngInitializationInput& input) noexcept {
    return input.policy_rng_root_seed.has_value() &&
           ygo::trajectory::is_canonical_identity(
               input.participant_policy_assignment_id,
               kParticipantPolicyAssignmentIdentityPrefix) &&
           ygo::trajectory::is_canonical_policy_rng_stream_token(input.policy_rng_stream_id);
}

std::vector<std::uint8_t> build_initialization_material(
    const PolicyRngInitializationInput& input) {
    ygo::trajectory::ByteWriter writer;
    writer.string(kPolicyRngInitializationDomain);
    writer.string(kPolicyRngContractIdentity);
    writer.u64be(*input.policy_rng_root_seed);
    writer.string(input.participant_policy_assignment_id);
    writer.string(input.policy_rng_stream_id);
    return std::move(writer).take();
}

std::string compute_initialization_identity(
    const std::string& contract_identity,
    const std::string& stream_id,
    const std::vector<std::uint8_t>& material) {
    ygo::trajectory::PolicyRngInitializationIdentity input;
    input.policy_rng_contract_identity = contract_identity;
    input.policy_rng_stream_id = stream_id;
    input.initialization_material = material;
    return ygo::trajectory::compute_policy_rng_initialization_id(input);
}

std::uint8_t hex_nibble(const char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    throw std::invalid_argument("policy RNG block digest is not lowercase hexadecimal");
}

std::array<std::uint64_t, 4> digest_words(const std::string& digest) {
    if (digest.size() != 64) {
        throw std::invalid_argument("policy RNG block digest has an invalid length");
    }
    std::array<std::uint64_t, 4> words{};
    for (std::size_t word_index = 0; word_index < words.size(); ++word_index) {
        std::uint64_t word = 0;
        for (std::size_t nibble = 0; nibble < 16; ++nibble) {
            word = (word << 4) | hex_nibble(digest[word_index * 16 + nibble]);
        }
        words[word_index] = word;
    }
    return words;
}

PolicyRngWordResult success(const std::uint64_t value,
                            const std::uint64_t pre_cursor,
                            const std::uint64_t post_cursor) {
    PolicyRngWordResult result;
    result.value = value;
    result.pre_cursor = pre_cursor;
    result.post_cursor = post_cursor;
    return result;
}

PolicyRngWordResult failure(const PolicyErrorCode code,
                            const char* message,
                            const std::uint64_t pre_cursor,
                            const std::uint64_t post_cursor) {
    PolicyRngWordResult result;
    result.error = make_error(code, message);
    result.pre_cursor = pre_cursor;
    result.post_cursor = post_cursor;
    return result;
}

}  // namespace

std::vector<std::uint8_t> canonical_policy_rng_initialization_material(
    const PolicyRngInitializationInput& input) {
    if (!valid_initialization_input(input)) {
        throw std::invalid_argument("policy RNG initialization input is not canonical");
    }
    return build_initialization_material(input);
}

std::vector<std::uint8_t> canonical_policy_rng_block_bytes(
    const std::string_view policy_rng_initialization_identity,
    const std::uint64_t block_index) {
    if (!ygo::trajectory::is_canonical_identity(
            policy_rng_initialization_identity, kPolicyRngInitializationIdentityPrefix)) {
        throw std::invalid_argument("policy RNG initialization identity is not canonical");
    }
    ygo::trajectory::ByteWriter writer;
    writer.string(kPolicyRngBlockDomain);
    writer.string(policy_rng_initialization_identity);
    writer.u64be(block_index);
    return std::move(writer).take();
}

PolicyRngInitializationResult make_policy_rng_initialization(
    const PolicyRngInitializationInput& input) noexcept {
    try {
        if (!valid_initialization_input(input)) {
            return {std::nullopt,
                    make_error(PolicyErrorCode::InvalidConfiguration,
                               "policy RNG initialization input is not canonical")};
        }

        PolicyRngInitialization result;
        result.policy_rng_contract_identity = std::string(kPolicyRngContractIdentity);
        result.policy_rng_stream_id = input.policy_rng_stream_id;
        result.initialization_material = build_initialization_material(input);
        result.policy_rng_initialization_identity = compute_initialization_identity(
            result.policy_rng_contract_identity, result.policy_rng_stream_id,
            result.initialization_material);
        return {std::move(result), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                make_error(PolicyErrorCode::InvalidConfiguration,
                           "policy RNG initialization failed")};
    }
}

PolicyRngCreateResult create_sha256_counter_rng(
    const PolicyRngInitializationInput& input) noexcept {
    try {
        const auto initialization = make_policy_rng_initialization(input);
        if (!initialization) {
            return {std::nullopt, initialization.error};
        }
        Sha256CounterRng rng(std::move(*initialization.value));
        return {std::optional<Sha256CounterRng>(std::move(rng)), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                make_error(PolicyErrorCode::InvalidConfiguration,
                           "policy RNG construction failed")};
    }
}

PolicyRngWordResult Sha256CounterRng::next_raw_u64() noexcept {
    const auto pre_cursor = cursor_;
    if (cursor_ == std::numeric_limits<std::uint64_t>::max()) {
        return failure(PolicyErrorCode::RngExhausted,
                       "policy RNG raw-word cursor is exhausted", pre_cursor, pre_cursor);
    }
    try {
        const auto block_index = cursor_ / 4;
        const auto lane = static_cast<std::size_t>(cursor_ % 4);
        const auto block = canonical_policy_rng_block_bytes(
            initialization_.policy_rng_initialization_identity, block_index);
        const auto words = digest_words(ygo::trace::sha256_bytes(block));
        ++cursor_;
        return success(words[lane], pre_cursor, cursor_);
    } catch (const std::exception& error) {
        return failure(PolicyErrorCode::InvalidConfiguration, error.what(), pre_cursor, cursor_);
    } catch (...) {
        return failure(PolicyErrorCode::InvalidConfiguration,
                       "policy RNG raw-word generation failed", pre_cursor, cursor_);
    }
}

PolicyRngWordResult Sha256CounterRng::uniform_below_u64(const std::uint64_t n) noexcept {
    const auto pre_cursor = cursor_;
    if (n == 0) {
        return failure(PolicyErrorCode::EmptyCandidateDomain,
                       "policy RNG bound must be nonzero", pre_cursor, pre_cursor);
    }
    if (n == 1) {
        return success(0, pre_cursor, pre_cursor);
    }

    const auto threshold = static_cast<std::uint64_t>(-n) % n;
    for (;;) {
        const auto raw = next_raw_u64();
        if (!raw) {
            PolicyRngWordResult result;
            result.error = raw.error;
            result.pre_cursor = pre_cursor;
            result.post_cursor = raw.post_cursor;
            return result;
        }
        if (*raw.value >= threshold) {
            return success(*raw.value % n, pre_cursor, raw.post_cursor);
        }
    }
}

}  // namespace ygo::policy
