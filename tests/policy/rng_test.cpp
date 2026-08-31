#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/policy/rng.hpp"

namespace ygo::policy::detail {

struct PolicyRngTestAccess {
    static void set_cursor(Sha256CounterRng& rng, const std::uint64_t cursor) noexcept {
        rng.cursor_ = cursor;
    }
};

}  // namespace ygo::policy::detail

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> from_hex(const std::string& text) {
    require(text.size() % 2 == 0, "hex fixture has an odd length");
    std::vector<std::uint8_t> result;
    result.reserve(text.size() / 2);
    const auto nibble = [](const char character) -> std::uint8_t {
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<std::uint8_t>(character - 'a' + 10);
        }
        throw std::runtime_error("hex fixture contains a non-lowercase digit");
    };
    for (std::size_t index = 0; index < text.size(); index += 2) {
        result.push_back(static_cast<std::uint8_t>((nibble(text[index]) << 4) |
                                                   nibble(text[index + 1])));
    }
    return result;
}

ygo::policy::PolicyRngInitializationInput valid_input() {
    ygo::policy::PolicyRngInitializationInput input;
    input.policy_rng_root_seed = 0x0123456789abcdefULL;
    input.participant_policy_assignment_id =
        "participant_policy_assignment.v1." + std::string(64, 'b');
    input.policy_rng_stream_id = "player0";
    return input;
}

const std::array<std::uint64_t, 8> kExpectedWords = {
    0xdbee67ea088ae52cULL,
    0x3f8046c261d43721ULL,
    0x729cbc7e5b02b699ULL,
    0xc4b1d3993e798461ULL,
    0x6ff22fa491c37925ULL,
    0xf639bb5b6c2c576aULL,
    0x93ef2d8b4c912552ULL,
    0x687b832326118333ULL,
};

void test_canonical_initialization_and_blocks() {
    const auto input = valid_input();
    const auto initialized = ygo::policy::make_policy_rng_initialization(input);
    require(static_cast<bool>(initialized), "valid policy RNG initialization was rejected");
    require(initialized.value.has_value(), "initialization result has no value");
    const auto& identity = *initialized.value;
    require(identity.policy_rng_contract_identity ==
                "ocgforge.policy_rng.sha256_counter.v1",
            "initialization contract identity changed");
    require(identity.policy_rng_stream_id == "player0", "initialization stream changed");
    require(ygo::policy::canonical_policy_rng_initialization_material(input) ==
                identity.initialization_material,
            "initialization material helper disagrees with the identity");
    require(identity.initialization_material == from_hex(
                "0000002a6f6367666f7267652e706f6c6963795f726e672e7368613235365f636f756e7465722e696e69742e763100000025"
                "6f6367666f7267652e706f6c6963795f726e672e7368613235365f636f756e7465722e76310123456789abcdef0000006170"
                "61727469636970616e745f706f6c6963795f61737369676e6d656e742e76312e626262626262626262626262626262626262"
                "6262626262626262626262626262626262626262626262626262626262626262626262626262626262626262626200000007"
                "706c6179657230"),
            "canonical initialization material changed");
    const auto decoded_material =
        ygo::policy::decode_canonical_policy_rng_initialization_material(
            identity.initialization_material);
    require(static_cast<bool>(decoded_material),
            "canonical initialization material did not strictly decode");
    require(decoded_material.value->policy_rng_root_seed == input.policy_rng_root_seed &&
                decoded_material.value->participant_policy_assignment_id ==
                    input.participant_policy_assignment_id &&
                decoded_material.value->policy_rng_stream_id == input.policy_rng_stream_id,
            "decoded initialization material changed a policy-owned input");
    require(identity.policy_rng_initialization_identity ==
                "policy_rng_initialization.v1.5415d852e1fcafb64b67ddfe49a01413fca17d37f4b043cab67b9dc2b907cfa1",
            "initialization identity golden vector failed");
    require(ygo::policy::canonical_policy_rng_block_bytes(
                identity.policy_rng_initialization_identity, 0) == from_hex(
                "0000002b6f6367666f7267652e706f6c6963795f726e672e7368613235365f636f756e7465722e626c6f636b2e7631"
                "0000005d706f6c6963795f726e675f696e697469616c697a6174696f6e2e76312e353431356438353265316663616662"
                "36346236376464666534396130313431336663613137643337663462303433636162363762396463326239303763666131"
                "0000000000000000"),
            "block zero input golden vector failed");
    require(ygo::policy::canonical_policy_rng_block_bytes(
                identity.policy_rng_initialization_identity, 1) == from_hex(
                "0000002b6f6367666f7267652e706f6c6963795f726e672e7368613235365f636f756e7465722e626c6f636b2e7631"
                "0000005d706f6c6963795f726e675f696e697469616c697a6174696f6e2e76312e353431356438353265316663616662"
                "36346236376464666534396130313431336663613137643337663462303433636162363762396463326239303763666131"
                "0000000000000001"),
            "block one input golden vector failed");
}

void test_all_lanes_and_cursor_progression() {
    auto created = ygo::policy::create_sha256_counter_rng(valid_input());
    require(static_cast<bool>(created), "valid policy RNG was rejected");
    auto& rng = *created.value;
    for (std::size_t index = 0; index < kExpectedWords.size(); ++index) {
        const auto word = rng.next_raw_u64();
        require(static_cast<bool>(word), "raw-word generation failed before exhaustion");
        require(word.value.value() == kExpectedWords[index], "raw-word lane golden failed");
        require(word.pre_cursor == index && word.post_cursor == index + 1,
                "raw-word cursor span was not exactly one word");
        require(rng.cursor() == index + 1, "raw-word cursor did not advance exactly once");
    }
}

void test_initialization_validation_and_episode_seed_negative_control() {
    auto empty_assignment = valid_input();
    empty_assignment.participant_policy_assignment_id.clear();
    const auto empty_assignment_result =
        ygo::policy::make_policy_rng_initialization(empty_assignment);
    require(!empty_assignment_result && empty_assignment_result.error.has_value(),
            "empty participant assignment was accepted");
    require(empty_assignment_result.error->code == ygo::policy::PolicyErrorCode::InvalidConfiguration,
            "empty participant assignment returned the wrong error");

    auto missing_root = valid_input();
    missing_root.policy_rng_root_seed.reset();
    const auto missing_root_result =
        ygo::policy::make_policy_rng_initialization(missing_root);
    require(!missing_root_result && missing_root_result.error.has_value(),
            "missing policy-owned root was accepted");

    auto invalid_stream = valid_input();
    invalid_stream.policy_rng_stream_id = "Player.0";
    const auto invalid_stream_result =
        ygo::policy::make_policy_rng_initialization(invalid_stream);
    require(!invalid_stream_result && invalid_stream_result.error.has_value(),
            "noncanonical stream token was accepted");

    auto empty_stream = valid_input();
    empty_stream.policy_rng_stream_id.clear();
    const auto empty_stream_result =
        ygo::policy::make_policy_rng_initialization(empty_stream);
    require(!empty_stream_result && empty_stream_result.error.has_value(),
            "empty stream token was accepted");

    auto changed_assignment = valid_input();
    changed_assignment.participant_policy_assignment_id.back() = 'c';
    auto changed_stream = valid_input();
    changed_stream.policy_rng_stream_id = "player1";
    auto changed_root = valid_input();
    ++*changed_root.policy_rng_root_seed;
    const auto baseline = ygo::policy::make_policy_rng_initialization(valid_input());
    const auto assignment_identity =
        ygo::policy::make_policy_rng_initialization(changed_assignment);
    const auto stream_identity = ygo::policy::make_policy_rng_initialization(changed_stream);
    const auto root_identity = ygo::policy::make_policy_rng_initialization(changed_root);
    require(baseline && assignment_identity && stream_identity && root_identity,
            "policy-owned identity separation fixture was rejected");
    require(baseline.value->policy_rng_initialization_identity !=
                assignment_identity.value->policy_rng_initialization_identity,
            "changed participant assignment did not change initialization identity");
    require(baseline.value->policy_rng_initialization_identity !=
                stream_identity.value->policy_rng_initialization_identity,
            "changed stream did not change initialization identity");
    require(baseline.value->policy_rng_initialization_identity !=
                root_identity.value->policy_rng_initialization_identity,
            "changed policy-owned root did not change initialization identity");

    const auto first_word = [](const ygo::policy::PolicyRngInitializationInput& value) {
        auto rng = ygo::policy::create_sha256_counter_rng(value);
        require(static_cast<bool>(rng), "policy-owned separation RNG creation failed");
        const auto word = rng.value->next_raw_u64();
        require(static_cast<bool>(word), "policy-owned separation raw word failed");
        return word.value.value();
    };
    const auto baseline_word = first_word(valid_input());
    require(baseline_word != first_word(changed_assignment),
            "changed participant assignment did not change the first raw word");
    require(baseline_word != first_word(changed_stream),
            "changed stream did not change the first raw word");
    require(baseline_word != first_word(changed_root),
            "changed policy-owned root did not change the first raw word");

    const auto run_for_episode = [](const std::string& episode_semantic_id) {
        (void)episode_semantic_id;
        return ygo::policy::create_sha256_counter_rng(valid_input());
    };
    auto episode_a = run_for_episode("episode-a");
    auto episode_b = run_for_episode("episode-b");
    require(episode_a && episode_b, "episode negative-control RNG creation failed");
    require(episode_a.value->initialization().policy_rng_initialization_identity ==
                episode_b.value->initialization().policy_rng_initialization_identity,
            "episode identity changed policy RNG initialization identity");
    const auto word_a = episode_a.value->next_raw_u64();
    const auto word_b = episode_b.value->next_raw_u64();
    require(word_a && word_b && word_a.value == word_b.value,
            "episode identity changed the first policy RNG raw word");
}

void test_bounded_sampling_and_forced_rejection() {
    auto empty_domain = ygo::policy::create_sha256_counter_rng(valid_input());
    require(static_cast<bool>(empty_domain), "valid policy RNG was rejected");
    const auto empty = empty_domain.value->uniform_below_u64(0);
    require(!empty && empty.error.has_value(), "zero bound was accepted");
    require(empty.error->code == ygo::policy::PolicyErrorCode::EmptyCandidateDomain,
            "zero bound returned the wrong error");
    require(empty.pre_cursor == 0 && empty.post_cursor == 0 && empty_domain.value->cursor() == 0,
            "zero bound consumed a raw word");

    const auto singleton = empty_domain.value->uniform_below_u64(1);
    require(singleton && singleton.value.value() == 0, "unit bound did not return zero");
    require(singleton.pre_cursor == 0 && singleton.post_cursor == 0 &&
                empty_domain.value->cursor() == 0,
            "unit bound consumed a raw word");

    constexpr std::uint64_t bound = 0x8000000000000001ULL;
    const auto threshold = static_cast<std::uint64_t>(-bound) % bound;
    require(kExpectedWords[2] < threshold && kExpectedWords[3] >= threshold,
            "forced-rejection golden fixture does not straddle the threshold");
    auto forced = ygo::policy::create_sha256_counter_rng(valid_input());
    require(static_cast<bool>(forced), "valid forced-rejection RNG was rejected");
    ygo::policy::detail::PolicyRngTestAccess::set_cursor(*forced.value, 2);
    const auto sampled = forced.value->uniform_below_u64(bound);
    require(static_cast<bool>(sampled), "forced-rejection sampling failed");
    require(sampled.value.value() == kExpectedWords[3] % bound,
            "forced-rejection result did not use the accepted raw word");
    require(sampled.pre_cursor == 2 && sampled.post_cursor == 4 && forced.value->cursor() == 4,
            "forced-rejection cursor span did not consume exactly two words");
}

void test_exhaustion_fails_closed_without_wraparound() {
    auto created = ygo::policy::create_sha256_counter_rng(valid_input());
    require(static_cast<bool>(created), "valid exhaustion RNG was rejected");
    ygo::policy::detail::PolicyRngTestAccess::set_cursor(
        *created.value, std::numeric_limits<std::uint64_t>::max());
    const auto raw = created.value->next_raw_u64();
    require(!raw && raw.error.has_value(), "exhausted raw-word cursor was accepted");
    require(raw.error->code == ygo::policy::PolicyErrorCode::RngExhausted,
            "exhausted raw-word cursor returned the wrong error");
    require(raw.pre_cursor == std::numeric_limits<std::uint64_t>::max() &&
                raw.post_cursor == std::numeric_limits<std::uint64_t>::max() &&
                created.value->cursor() == std::numeric_limits<std::uint64_t>::max(),
            "exhaustion wrapped or changed the cursor");

    const auto bounded = created.value->uniform_below_u64(2);
    require(!bounded && bounded.error.has_value(), "exhausted bounded sampling was accepted");
    require(bounded.error->code == ygo::policy::PolicyErrorCode::RngExhausted,
            "exhausted bounded sampling returned the wrong error");
    require(bounded.pre_cursor == std::numeric_limits<std::uint64_t>::max() &&
                bounded.post_cursor == std::numeric_limits<std::uint64_t>::max(),
            "exhausted bounded sampling changed the cursor");
}

int run() {
    test_canonical_initialization_and_blocks();
    test_all_lanes_and_cursor_progression();
    test_initialization_validation_and_episode_seed_negative_control();
    test_bounded_sampling_and_forced_rejection();
    test_exhaustion_fails_closed_without_wraparound();
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
