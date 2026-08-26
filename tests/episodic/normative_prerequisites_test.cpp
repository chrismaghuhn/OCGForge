#include "ygo/core/rules_bundle.hpp"
#include "ygo/core/seed_bundle.hpp"
#include "ygo/environment/candidate_domain_evidence.hpp"
#include "ygo/environment/identity_contract.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/trace/sha256.hpp"

static_assert(ygo::environment::kDecisionContractId == "ocgforge.decision_protocol.v1");
static_assert(ygo::environment::kActionIdentitySchemaId == "ocgforge.action_identity.v1");
static_assert(ygo::environment::kSeedDerivationId == "ocgforge.seed_derivation.v1");
static_assert(ygo::environment::kScriptResolutionContractId == "ocgforge.script_resolution.v1");
static_assert(ygo::environment::kRequiredScriptClosureSchemaId == "ocgforge.required_script_closure.v1");
static_assert(ygo::environment::kRequiredScriptClosureDomain ==
              "ocgforge.required_script_closure_identity.v1");
static_assert(ygo::environment::kCandidateDomainSchemaId == "ocgforge.candidate_domain.v1");
static_assert(ygo::environment::kCandidateDomainEvidenceSchemaId ==
              "ocgforge.candidate_domain_evidence.v1");

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> from_hex(const std::string& value) {
    require(value.size() % 2 == 0, "hex vector has odd length");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const auto high = value[index];
        const auto low = value[index + 1];
        const auto digit = [](const char character) -> std::uint8_t {
            if (character >= '0' && character <= '9') {
                return static_cast<std::uint8_t>(character - '0');
            }
            if (character >= 'a' && character <= 'f') {
                return static_cast<std::uint8_t>(character - 'a' + 10);
            }
            throw std::runtime_error("invalid lowercase hex digit");
        };
        bytes.push_back(static_cast<std::uint8_t>((digit(high) << 4) | digit(low)));
    }
    return bytes;
}

void require_seed(std::uint64_t root_seed, const std::array<std::uint64_t, 4>& expected) {
    require(ygo::core::derive_seed_bundle(root_seed).words == expected,
            "seed derivation known-answer vector failed");
}

ygo::environment::RequiredScriptClosureInput closure_input() {
    ygo::environment::RequiredScriptClosureInput input;
    input.card_scripts_commit = "0123456789abcdef0123456789abcdef01234567";
    input.card_scripts_tree_sha256 =
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    input.script_resolution_contract_id = std::string(ygo::environment::kScriptResolutionContractId);
    input.required_global_script_names = {"constant.lua", "utility.lua", "proc_normal.lua"};
    input.required_script_codes = {9, 3, 9, 1};
    return input;
}

ygo::environment::CandidateDomainWitness witness(std::uint64_t count,
                                                  std::string episode,
                                                  std::uint64_t environment_index,
                                                  std::uint64_t engine_index,
                                                  std::string protocol_id,
                                                  std::string digest) {
    ygo::environment::CandidateDomainWitness value;
    value.candidate_count = count;
    value.request_kind = "CARD_SELECTION";
    value.episode_semantic_id = std::move(episode);
    value.environment_decision_index = environment_index;
    value.engine_step_index = engine_index;
    value.protocol_decision_id = std::move(protocol_id);
    value.candidate_domain_digest = std::move(digest);
    value.ordered_semantic_keys = {"card.0.1"};
    return value;
}

void test_seed_vectors() {
    require_seed(0, {0x0000000000000000ULL, 0x9e3779b97f4a7c15ULL, 0x6a09e667f3bcc909ULL,
                     0xbb67ae8584caa73bULL});
    require_seed(1, {0x0000000000000001ULL, 0x9e3779b97f4a7c14ULL, 0x6a09e667f3bcc90aULL,
                     0xbb67ae8584caa739ULL});
    require_seed(std::numeric_limits<std::uint64_t>::max(),
                 {0xffffffffffffffffULL, 0x61c8864680b583eaULL, 0x6a09e667f3bcc908ULL,
                  0x4498517a7b3558c5ULL});
    require_seed(0x8000000000000000ULL,
                 {0x8000000000000000ULL, 0x1e3779b97f4a7c15ULL, 0xea09e667f3bcc909ULL,
                  0xbb67ae8584caa73bULL});
    require_seed(0x0123456789abcdefULL,
                 {0x0123456789abcdefULL, 0x9f143cdef6e1b1faULL, 0x6b2d2bcf7d6896f8ULL,
                  0xb921244a979d3ce5ULL});
}

void test_required_card_seed_set() {
    ygo::core::FixtureDeck deck_a;
    deck_a.main_deck = {9, 2, 9};
    deck_a.extra_deck = {5};
    ygo::core::FixtureDeck deck_b;
    deck_b.main_deck = {3, 2};
    deck_b.extra_deck = {5, 1};
    require(ygo::core::canonical_required_script_codes(deck_a, deck_b) ==
                std::vector<std::uint32_t>{1, 2, 3, 5, 9},
            "required-card code seed set was not sorted and deduplicated canonically");
}

void test_closure_golden_vector() {
    const auto input = closure_input();
    const auto expected_bytes = from_hex(
        "0000002c6f6367666f7267652e72657175697265645f7363726970745f636c6f737572655f696465"
        "6e746974792e7631000000236f6367666f7267652e72657175697265645f7363726970745f636c6f"
        "737572652e7631000000283031323334353637383961626364656630313233343536373839616263"
        "64656630313233343536370000004061626364656630313233343536373839616263646566303132"
        "33343536373839616263646566303132333435363738396162636465663031323334353637383900"
        "00001d6f6367666f7267652e7363726970745f7265736f6c7574696f6e2e7631000000030000000c"
        "636f6e7374616e742e6c75610000000b7574696c6974792e6c75610000000f70726f635f6e6f726d"
        "616c2e6c756100000003000000010000000300000009");
    require(ygo::environment::canonical_required_script_closure_bytes(input) == expected_bytes,
            "required-script closure canonical bytes golden vector failed");
    require(ygo::environment::required_script_closure_identity(input) ==
                "397107b0f9493076372d0df7a360c8a7b12251dfe64530e0fcdfad0d4d567372",
            "required-script closure identity golden vector failed");

    auto reordered = input;
    reordered.required_script_codes = {1, 9, 3, 9};
    require(ygo::environment::required_script_closure_identity(reordered) ==
                ygo::environment::required_script_closure_identity(input),
            "required-card discovery order changed the closure identity");

    auto changed_tree = input;
    changed_tree.card_scripts_tree_sha256[0] = 'b';
    require(ygo::environment::required_script_closure_identity(changed_tree) !=
                ygo::environment::required_script_closure_identity(input),
            "changed CardScripts tree identity did not change the closure identity");

    auto changed_code = input;
    changed_code.required_script_codes.push_back(10);
    require(ygo::environment::required_script_closure_identity(changed_code) !=
                ygo::environment::required_script_closure_identity(input),
            "changed required-card seed set did not change the closure identity");

    auto changed_resolution = input;
    changed_resolution.script_resolution_contract_id = "ocgforge.script_resolution.v2";
    require(ygo::environment::required_script_closure_identity(changed_resolution) !=
                ygo::environment::required_script_closure_identity(input),
            "changed resolution contract did not change the closure identity");

    auto invalid_name = input;
    invalid_name.required_global_script_names = {"../constant.lua"};
    bool rejected = false;
    try {
        (void)ygo::environment::required_script_closure_identity(invalid_name);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "host-path/traversal global script name was not rejected");
}

void test_candidate_domain_golden_vector() {
    const std::vector<std::string> keys = {"card.0.1", "card.0.2"};
    const auto expected_bytes = from_hex(
        "0000001c6f6367666f7267652e63616e6469646174655f646f6d61696e2e76310000000e4341"
        "52445f53454c454354494f4e0000000200000008636172642e302e3100000008636172642e"
        "302e32");
    require(ygo::environment::canonical_candidate_domain_bytes("CARD_SELECTION", keys) == expected_bytes,
            "candidate-domain canonical bytes golden vector failed");
    require(ygo::environment::candidate_domain_digest("CARD_SELECTION", keys) ==
                "fa94ea44f1018ee865e7b3b50b6175a83aef54c4bda7beab9f84140687c6f7c3",
            "candidate-domain digest golden vector failed");

    const std::vector<std::string> reordered = {"card.0.2", "card.0.1"};
    require(ygo::environment::candidate_domain_digest("CARD_SELECTION", reordered) !=
                ygo::environment::candidate_domain_digest("CARD_SELECTION", keys),
            "candidate-domain order mutation did not change the digest");
}

void test_candidate_metrics_and_witness() {
    const auto metrics = std::vector<ygo::environment::CandidateDomainWitness>{
        witness(5, "episode-a", 0, 0, "decision-a", "digest-a"),
        witness(21, "episode-a", 1, 1, "decision-a", "digest-f"),
        witness(7, "episode-a", 2, 2, "decision-a", "digest-z"),
    };
    require(ygo::environment::candidate_domain_max(metrics) == 21,
            "candidate_domain_max must reduce individual domains with MAX");
    require(ygo::environment::candidate_max_total({5, 21, 7}) == 33,
            "candidate_max_total must sum the per-job maxima fixture");

    const auto equal_max = std::vector<ygo::environment::CandidateDomainWitness>{
        witness(21, "episode-z", 0, 0, "decision-a", "digest-a"),
        witness(21, "episode-a", 9, 9, "decision-z", "digest-z"),
        witness(21, "episode-a", 1, 9, "decision-z", "digest-z"),
        witness(21, "episode-a", 1, 1, "decision-z", "digest-z"),
        witness(21, "episode-a", 1, 1, "decision-b", "digest-z"),
        witness(21, "episode-a", 1, 1, "decision-a", "digest-f"),
        witness(20, "episode-a", 0, 0, "decision-a", "digest-0"),
    };
    require(ygo::environment::select_g28_witness_index(equal_max) == 5,
            "G28 witness tie-break order is not deterministic");
}

}  // namespace

int main() {
    try {
        test_seed_vectors();
        test_required_card_seed_set();
        test_closure_golden_vector();
        test_candidate_domain_golden_vector();
        test_candidate_metrics_and_witness();
        std::cout << "normative_prerequisites_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
