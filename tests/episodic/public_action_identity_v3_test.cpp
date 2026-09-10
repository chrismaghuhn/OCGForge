#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "public_action_identity_internal.hpp"
#include "ygo/environment/public_action_identity.hpp"

namespace {

using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicCardReference;
using ygo::environment::PublicCardReferenceKind;
using ygo::environment::PublicCardSelectionOperation;
using ygo::environment::PublicSemanticDecisionIdentityInputV3;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> from_hex(const std::string_view value) {
    require(value.size() % 2 == 0, "golden hex has odd length");
    std::vector<std::uint8_t> result;
    result.reserve(value.size() / 2);
    const auto digit = [](const char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        throw std::runtime_error("golden hex contains an invalid digit");
    };
    for (std::size_t index = 0; index < value.size(); index += 2) {
        result.push_back(static_cast<std::uint8_t>((digit(value[index]) << 4) |
                                                   digit(value[index + 1])));
    }
    return result;
}

PublicActionKeyInput card_action(const PublicCardSelectionOperation operation) {
    PublicActionKeyInput result;
    result.action_kind = "card_selection";
    result.card_selection_operation = operation;
    result.source_index = 3;
    result.source_reference = PublicCardReference{
        PublicCardReferenceKind::RedactedSlot,
        "p0:SPELL_TRAP_ZONE:0",
    };
    return result;
}

template <typename Function>
void require_rejected(const Function& operation, const std::string& message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

void test_v3_action_goldens_and_version_purity() {
    static_assert(ygo::environment::kPublicActionIdentityV3SchemaId ==
                  "ocgforge.public_action_identity.v3");
    static_assert(ygo::environment::kPublicCandidateDomainV3SchemaId ==
                  "ocgforge.public_candidate_domain.v3");
    static_assert(ygo::environment::kPublicSemanticDecisionIdentityV3SchemaId ==
                  "ocgforge.public_semantic_decision_identity.v3");
    static_assert(ygo::environment::kPublicActionKeyV3Prefix ==
                  "public_action.v3.");

    const auto none = card_action(PublicCardSelectionOperation::None);
    const auto select = card_action(PublicCardSelectionOperation::Select);
    const auto unselect = card_action(PublicCardSelectionOperation::Unselect);

    const auto none_bytes = ygo::environment::canonical_public_action_key_bytes_v3(none);
    const auto select_bytes = ygo::environment::canonical_public_action_key_bytes_v3(select);
    const auto unselect_bytes =
        ygo::environment::canonical_public_action_key_bytes_v3(unselect);
    require(none_bytes == from_hex(
                         "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                         "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                         "0000000e636172645f73656c656374696f6e000001010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                         "00000001000000030000000000"),
            "V3 None action bytes golden vector failed");
    require(select_bytes == from_hex(
                           "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                           "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                           "0000000e636172645f73656c656374696f6e010001010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                           "00000001000000030000000000"),
              "V3 Select action bytes golden vector failed");
    require(unselect_bytes == from_hex(
                             "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                             "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                             "0000000e636172645f73656c656374696f6e020001010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                             "00000001000000030000000000"),
                "V3 Unselect action bytes golden vector failed");

    const auto none_key = ygo::environment::public_action_key_v3(none);
    const auto select_key = ygo::environment::public_action_key_v3(select);
    const auto unselect_key = ygo::environment::public_action_key_v3(unselect);
    require(none_key ==
                "public_action.v3.000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                "0000000e636172645f73656c656374696f6e000001010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                "00000001000000030000000000",
            "V3 None action key golden vector failed");
    require(select_key ==
                "public_action.v3.000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                "0000000e636172645f73656c656374696f6e010001010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                "00000001000000030000000000",
            "V3 Select action key golden vector failed");
    require(unselect_key ==
                "public_action.v3.000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7633"
                "0000000e636172645f73656c656374696f6e020001010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                "00000001000000030000000000",
            "V3 Unselect action key golden vector failed");
    require(select_key != unselect_key &&
                ygo::environment::is_public_action_key_v3(none_key) &&
                ygo::environment::is_public_action_key_v3(select_key) &&
                ygo::environment::is_public_action_key_v3(unselect_key),
            "V3 action key generation or validation failed");
    require(!ygo::environment::is_public_action_key_v2(select_key) &&
                !ygo::environment::is_public_action_key_v3(
                    ygo::environment::public_action_key_v2(select)),
            "V2/V3 action key validators crossed generations");

    auto invalid_operation = select;
    invalid_operation.card_selection_operation =
        static_cast<PublicCardSelectionOperation>(0xff);
    require_rejected(
        [&] { (void)ygo::environment::public_action_key_v3(invalid_operation); },
        "V3 action accepted an unknown operation code");
    auto invalid_non_card = select;
    invalid_non_card.action_kind = "cancel";
    require_rejected(
        [&] { (void)ygo::environment::public_action_key_v3(invalid_non_card); },
        "V3 non-card action accepted Select metadata");
}

void test_v3_domain_goldens_and_rejection() {
    const auto select_key = ygo::environment::public_action_key_v3(
        card_action(PublicCardSelectionOperation::Select));
    const auto unselect_key = ygo::environment::public_action_key_v3(
        card_action(PublicCardSelectionOperation::Unselect));
    const std::vector<std::string> keys = {select_key, unselect_key};
    const auto bytes = ygo::environment::canonical_public_candidate_domain_bytes_v3(
        "unselect_card", keys);
    require(bytes.size() == 642,
            "V3 candidate-domain canonical byte length changed");
    require(ygo::environment::public_candidate_domain_digest_v3("unselect_card", keys) ==
                "6a06616ad7bc1e3d7c19710fe185bd4b1ce60320f592c0e8cc9e6e185290e6d7",
            "V3 candidate-domain digest golden vector failed");
    require(ygo::environment::public_candidate_domain_digest_v3(
                "unselect_card", {unselect_key, select_key}) !=
                ygo::environment::public_candidate_domain_digest_v3("unselect_card", keys),
            "V3 candidate-domain order was not identity-bearing");

    const auto v2_select = ygo::environment::public_action_key_v2(
        card_action(PublicCardSelectionOperation::Select));
    require_rejected(
        [&] { (void)ygo::environment::public_candidate_domain_digest_v3(
                      "unselect_card", {v2_select}); },
        "V3 candidate domain accepted a V2 action key");
    require_rejected(
        [&] { (void)ygo::environment::public_candidate_domain_digest_v2(
                      "unselect_card", {select_key}); },
        "V2 candidate domain accepted a V3 action key");
    require_rejected(
        [&] { (void)ygo::environment::public_candidate_domain_digest_v3(
                      "unselect_card", {select_key, select_key}); },
        "V3 candidate domain accepted duplicate keys");
    require_rejected(
        [&] { (void)ygo::environment::public_candidate_domain_digest_v3(
                      "unselect_card", {}); },
        "V3 candidate domain accepted an empty domain");
}

void test_v3_decision_identity_and_resolver() {
    const auto select = card_action(PublicCardSelectionOperation::Select);
    const auto unselect = card_action(PublicCardSelectionOperation::Unselect);
    const auto select_key = ygo::environment::public_action_key_v3(select);
    const auto unselect_key = ygo::environment::public_action_key_v3(unselect);
    const std::vector<std::string> keys = {select_key, unselect_key};

    PublicSemanticDecisionIdentityInputV3 input;
    input.episode_semantic_id = std::string(64, 'a');
    input.decision_index = 17;
    input.acting_player = 1;
    input.request_kind = "unselect_card";
    input.public_observation_digest = std::string(64, 'b');
    input.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest_v3(input.request_kind, keys);
    input.public_action_keys = keys;
    const auto bytes =
        ygo::environment::canonical_public_semantic_decision_identity_bytes_v3(input);
    require(bytes.size() == 328,
            "V3 public semantic-decision canonical byte length changed");
    require(ygo::environment::public_semantic_decision_id_v3(input) ==
                "b5a58d0271c7fe452913fada328287c3452bfa94938371e747b7783529133744",
            "V3 public semantic-decision ID golden vector failed");

    auto v2_digest = input;
    v2_digest.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest_v2(
            "unselect_card",
            {ygo::environment::public_action_key_v2(select),
             ygo::environment::public_action_key_v2(unselect)});
    require_rejected(
        [&] { (void)ygo::environment::public_semantic_decision_id_v3(v2_digest); },
        "V3 public decision accepted a V2 candidate-domain digest");

    auto v2_key_input = input;
    v2_key_input.public_action_keys.front() =
        ygo::environment::public_action_key_v2(select);
    require_rejected(
        [&] { (void)ygo::environment::public_semantic_decision_id_v3(v2_key_input); },
        "V3 public decision accepted a V2 action key");

    require(ygo::environment::detail::resolve_public_action_key_v3(
                {{select_key, "internal-select"}}, select_key)
                .value_or("") == "internal-select",
            "V3 public key did not resolve through the V3 resolver");
    require(!ygo::environment::detail::resolve_public_action_key_v2(
                 {{select_key, "internal-select"}}, select_key)
                 .has_value(),
            "V2 public key resolver accepted a V3 key");
    require(!ygo::environment::detail::resolve_public_action_key_v3(
                 {{ygo::environment::public_action_key_v2(select), "internal-select"}},
                 ygo::environment::public_action_key_v2(select))
                 .has_value(),
            "V3 public key resolver accepted a V2 key");
}

}  // namespace

int main() {
    try {
        test_v3_action_goldens_and_version_purity();
        test_v3_domain_goldens_and_rejection();
        test_v3_decision_identity_and_resolver();
        std::cout << "public action identity V3 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
