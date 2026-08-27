#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/serialization.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> from_hex(const std::string& value) {
    require(value.size() % 2 == 0, "golden hex has odd length");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(value.size() / 2);
    const auto digit = [](const char character) -> std::uint8_t {
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<std::uint8_t>(character - 'a' + 10);
        }
        throw std::runtime_error("golden hex contains a non-lowercase digit");
    };
    for (std::size_t index = 0; index < value.size(); index += 2) {
        bytes.push_back(static_cast<std::uint8_t>((digit(value[index]) << 4) | digit(value[index + 1])));
    }
    return bytes;
}

ygo::observation::PlayerObservation hidden_card_observation() {
    ygo::observation::PlayerObservation observation;
    observation.perspective_player = 1;
    observation.globals.life_points = {8000, 8000};
    observation.zones.push_back({0, ygo::observation::SemanticZone::SpellTrapZone, 1, 0, 1, false});
    ygo::observation::ObservedCard hidden;
    hidden.locator = {"p0:SPELL_TRAP_ZONE:0"};
    hidden.identity_known = false;
    hidden.controller = 0;
    hidden.zone = ygo::observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    observation.entities.push_back(hidden);
    observation.match_context.perspective_player = 1;
    return observation;
}

ygo::environment::PublicActionKeyInput hidden_card_action() {
    ygo::environment::PublicActionKeyInput input;
    input.action_kind = "idle_command";
    input.phase = 0;
    input.source_reference = ygo::environment::PublicCardReference{
        ygo::environment::PublicCardReferenceKind::RedactedSlot,
        "p0:SPELL_TRAP_ZONE:0",
    };
    return input;
}

void test_contract_ids() {
    static_assert(ygo::environment::kPublicActionIdentitySchemaId ==
                  "ocgforge.public_action_identity.v1");
    static_assert(ygo::environment::kPublicCandidateDomainSchemaId ==
                  "ocgforge.public_candidate_domain.v1");
    static_assert(ygo::environment::kPublicSemanticDecisionIdentitySchemaId ==
                  "ocgforge.public_semantic_decision_identity.v1");
    static_assert(ygo::environment::kEpisodicEnvironmentV2ContractId ==
                  "ocgforge.episodic_environment.v2");
    static_assert(ygo::environment::kEnvironmentIdentityV2SchemaId ==
                  "ocgforge.environment_identity.v2");
}

void test_paired_world_hidden_card_projection() {
    const std::string internal_a = "card.0.3.14821890.0.8.0";
    const std::string internal_b = "card.0.3.7654321.0.8.0";
    require(internal_a != internal_b, "paired worlds must differ internally");

    const auto observation_a = hidden_card_observation();
    const auto observation_b = hidden_card_observation();
    require(ygo::observation::canonical_serialize(observation_a) ==
                ygo::observation::canonical_serialize(observation_b),
            "paired worlds did not preserve the identical PlayerObservation");
    require(ygo::observation::observation_hash(observation_a) ==
                ygo::observation::observation_hash(observation_b),
            "paired worlds did not preserve the identical observation hash");

    const auto public_candidate_a = hidden_card_action();
    const auto public_candidate_b = hidden_card_action();
    require(ygo::environment::canonical_public_action_key_bytes(public_candidate_a) ==
                ygo::environment::canonical_public_action_key_bytes(public_candidate_b),
            "paired worlds did not preserve the identical public candidate descriptor");
    const auto public_a = ygo::environment::public_action_key(public_candidate_a);
    const auto public_b = ygo::environment::public_action_key(public_candidate_b);
    require(public_a == public_b, "same redacted slot did not produce the same public key");
    require(ygo::environment::canonical_public_action_key_bytes(hidden_card_action()) ==
                from_hex("000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
                         "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
                         "0000000c69646c655f636f6d6d616e6401010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                         "0001000000000000000000000000"),
            "public action key canonical bytes golden vector failed");
    require(public_a ==
                "public_action.v1.000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
                "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
                "0000000c69646c655f636f6d6d616e6401010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                "0001000000000000000000000000",
            "public action key golden vector failed");
    require(public_a.find("14821890") == std::string::npos,
            "public key leaked the hidden passcode");
    require(public_a.find("7654321") == std::string::npos,
            "public key leaked the alternate hidden passcode");
    require(ygo::environment::is_public_action_key(public_a),
            "generated public key failed canonical validation");
    require(!ygo::environment::is_public_action_key("public_action.v1.00"),
            "non-canonical public key was accepted");

    const std::vector<std::string> public_domain_a = {public_a};
    const std::vector<std::string> public_domain_b = {public_b};
    const auto digest_a = ygo::environment::public_candidate_domain_digest(
        "idle_command", public_domain_a);
    const auto digest_b = ygo::environment::public_candidate_domain_digest(
        "idle_command", public_domain_b);
    require(digest_a == digest_b, "paired worlds produced different public domain digests");
    require(digest_a == "f4fbe45ae8fe9bff858b7710872d59d111e3f00a704ec4ff7d24266b37393a98",
            "public candidate-domain digest golden vector failed");

    ygo::environment::PublicSemanticDecisionIdentityInput decision_input;
    decision_input.episode_semantic_id = std::string(64, 'a');
    decision_input.decision_index = 17;
    decision_input.acting_player = 1;
    decision_input.request_kind = "idle_command";
    decision_input.public_candidate_domain_digest = digest_a;
    const auto decision_a = ygo::environment::public_semantic_decision_id(decision_input);
    decision_input.public_candidate_domain_digest = digest_b;
    const auto decision_b = ygo::environment::public_semantic_decision_id(decision_input);
    require(decision_a == decision_b, "paired worlds produced different public decision IDs");
    require(decision_a == "1facd2336829ac18b80361d6851773de24ad29f00ee770ed52d2cfd48f7bb379",
            "public semantic decision identity golden vector failed");

    require(ygo::environment::detail::resolve_public_action_key(
                {{public_a, internal_a}}, public_a)
                .value_or("") == internal_a,
            "public key did not resolve to its exact internal candidate");
}

void test_public_domain_order_and_mapping_collision() {
    const auto first = ygo::environment::public_action_key(hidden_card_action());
    auto second_input = hidden_card_action();
    second_input.phase = 1;
    const auto second = ygo::environment::public_action_key(second_input);
    require(first != second, "distinct public action descriptors collided");

    const auto digest = ygo::environment::public_candidate_domain_digest(
        "idle_command", {first, second});
    const auto reversed = ygo::environment::public_candidate_domain_digest(
        "idle_command", {second, first});
    require(digest != reversed, "public domain digest ignored authoritative order");

    bool duplicate_domain_rejected = false;
    try {
        (void)ygo::environment::public_candidate_domain_digest(
            "idle_command", {first, first});
    } catch (const std::invalid_argument&) {
        duplicate_domain_rejected = true;
    }
    require(duplicate_domain_rejected, "duplicate public domain was accepted");

    bool empty_domain_rejected = false;
    try {
        (void)ygo::environment::public_candidate_domain_digest("idle_command", {});
    } catch (const std::invalid_argument&) {
        empty_domain_rejected = true;
    }
    require(empty_domain_rejected, "empty public domain was accepted");

    const auto ambiguous = ygo::environment::detail::resolve_public_action_key(
        {{first, "internal-a"}, {first, "internal-b"}}, first);
    require(!ambiguous.has_value(), "public-key collision did not fail closed");

    const auto unknown = ygo::environment::detail::resolve_public_action_key(
        {{first, "internal-a"}}, second);
    require(!unknown.has_value(), "unknown public key unexpectedly resolved");

    bool invalid_domain_rejected = false;
    try {
        (void)ygo::environment::public_candidate_domain_digest(
            "idle_command", {"public_action.v1.00"});
    } catch (const std::invalid_argument&) {
        invalid_domain_rejected = true;
    }
    require(invalid_domain_rejected, "non-canonical public domain was accepted");
}

void test_public_codec_rejects_non_locator_reference() {
    auto input = hidden_card_action();
    input.source_reference->observation_locator = "p0:SPELL_TRAP_ZONE:0\n";
    bool rejected = false;
    try {
        (void)ygo::environment::public_action_key(input);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "control-bearing public locator was accepted");
}

}  // namespace

int main() {
    try {
        test_contract_ids();
        test_paired_world_hidden_card_projection();
        test_public_domain_order_and_mapping_collision();
        test_public_codec_rejects_non_locator_reference();
        std::cout << "public_action_identity_tests=passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
