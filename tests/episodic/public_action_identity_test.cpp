#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/continuation.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
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
    input.action_kind = "card_selection";
    input.source_index = 3;
    input.source_reference = ygo::environment::PublicCardReference{
        ygo::environment::PublicCardReferenceKind::RedactedSlot,
        "p0:SPELL_TRAP_ZONE:0",
    };
    return input;
}

ygo::environment::PublicActionKeyInput typed_choice_action(
    const ygo::environment::PublicChoiceKind kind,
    const std::uint64_t value,
    const std::optional<std::uint32_t> response_index = std::nullopt) {
    ygo::environment::PublicActionKeyInput input;
    input.action_kind = "choice";
    input.choice = ygo::environment::PublicChoice{kind, value, response_index};
    return input;
}

ygo::protocol::DecisionRequest attached_request(
    const std::string& decision_id, const std::string& continuation_id,
    const ygo::protocol::DecisionRequestKind kind = ygo::protocol::DecisionRequestKind::Option,
    const std::optional<std::uint32_t> source_code = std::nullopt) {
    ygo::protocol::DecisionRequest request;
    request.kind = kind;
    request.decision_id = decision_id;
    request.engine_step_index = 19;
    request.player = 1;
    request.engine_message_type = 0;
    request.engine_message_name = "synthetic_attached_request";
    request.raw_message_hash = decision_id + ".raw";
    ygo::protocol::SelectionContinuation continuation;
    continuation.continuation_id = continuation_id;
    continuation.raw_message_hash = request.raw_message_hash;
    request.continuation = std::move(continuation);
    if (source_code.has_value()) {
        ygo::protocol::ActionCandidate candidate;
        candidate.semantic_key = "card.0.3." + std::to_string(*source_code) + ".0.8.0";
        candidate.source_card = *source_code;
        candidate.source_controller = 0;
        candidate.source_location = 8;
        candidate.source_sequence = 0;
        candidate.source_index = 3;
        request.candidates.push_back(std::move(candidate));
    }
    return request;
}

void test_public_safe_state_is_owned_by_projection() {
    const auto observation = hidden_card_observation();
    const auto safe_state = ygo::environment::canonical_public_safe_state_bytes(observation);
    require(!safe_state.empty(), "public safe-state serializer returned empty bytes");
    require(safe_state ==
                from_hex("0000001d6f6367666f7267652e7075626c69635f736166655f73746174652e76310000001d6f6367666f7267652e7075"
                         "626c69635f736166655f73746174652e763100000000000000000000000200001f4000001f4000000000000000000000"
                         "0000000001000400000001000000000000000100000000010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                         "000000010004010000000000000001000000000000000000000000000000000000010000000000000000010000000000"
                         "0000000000000000000000000000"),
            "public safe-state canonical bytes golden vector failed");

    const auto projected = ygo::environment::project_public_observation(observation);
    require(projected.canonical_safe_state_bytes() == safe_state,
            "public projection did not use its canonical safe-state serializer");

    auto malformed = observation;
    malformed.entities.front().passcode = 14821890;
    bool rejected = false;
    try {
        (void)ygo::environment::project_public_observation(malformed);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "public safe-state serializer accepted hidden card identity");

    ygo::environment::PublicEnvironmentObservationInput caller_input;
    rejected = false;
    try {
        (void)ygo::environment::canonical_public_environment_observation_bytes(caller_input);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "caller-created public observation bypassed the safe-state projector");
}

void test_contract_ids() {
    static_assert(ygo::environment::kPublicActionIdentitySchemaId ==
                  "ocgforge.public_action_identity.v1");
    static_assert(ygo::environment::kPublicCandidateDomainSchemaId ==
                  "ocgforge.public_candidate_domain.v1");
    static_assert(ygo::environment::kPublicSemanticDecisionIdentitySchemaId ==
                  "ocgforge.public_semantic_decision_identity.v1");
    static_assert(ygo::environment::kPublicEnvironmentObservationSchemaId ==
                  "ocgforge.public_environment_observation.v1");
    static_assert(ygo::environment::kPublicSafeStateSchemaId ==
                  "ocgforge.public_safe_state.v1");
    static_assert(ygo::environment::kEpisodicEnvironmentV2ContractId ==
                  "ocgforge.episodic_environment.v2");
    static_assert(ygo::environment::kEnvironmentIdentityV2SchemaId ==
                  "ocgforge.environment_identity.v2");
}

void test_paired_world_hidden_card_projection() {
    const std::string internal_a = "card.0.3.14821890.0.8.0";
    const std::string internal_b = "card.0.3.7654321.0.8.0";
    require(internal_a != internal_b, "paired worlds must differ internally");

    auto observation_a = hidden_card_observation();
    auto observation_b = hidden_card_observation();
    observation_b.engine_step_index = 91;
    ygo::observation::attach_decision_context(
        observation_a,
        attached_request("internal-" + internal_a, "continuation-" + internal_a,
                         ygo::protocol::DecisionRequestKind::CardSelection, 14821890));
    ygo::observation::attach_decision_context(
        observation_b,
        attached_request("internal-" + internal_b, "continuation-" + internal_b,
                         ygo::protocol::DecisionRequestKind::CardSelection, 7654321));
    require(ygo::observation::canonical_serialize(observation_a) !=
                ygo::observation::canonical_serialize(observation_b),
            "paired worlds did not preserve distinct attached internal context");
    require(ygo::observation::observation_hash(observation_a) !=
                ygo::observation::observation_hash(observation_b),
            "paired worlds did not bind the distinct internal observation context");

    const auto public_observation_a = ygo::environment::project_public_observation(observation_a);
    const auto public_observation_b = ygo::environment::project_public_observation(observation_b);
    require(public_observation_a.canonical_safe_state_bytes() ==
                public_observation_b.canonical_safe_state_bytes(),
            "paired worlds did not preserve the identical public safe state");
    require(ygo::environment::canonical_public_environment_observation_bytes(public_observation_a) ==
                ygo::environment::canonical_public_environment_observation_bytes(public_observation_b),
            "paired worlds did not preserve the identical public observation");
    const auto paired_public_observation_bytes =
        ygo::environment::canonical_public_environment_observation_bytes(public_observation_a);
    require(paired_public_observation_bytes ==
                from_hex("0000002a6f6367666f7267652e7075626c69635f656e7669726f6e6d656e745f6f62736572766174696f6e2e76310000002a"
                         "6f6367666f7267652e7075626c69635f656e7669726f6e6d656e745f6f62736572766174696f6e2e76310100000000000000"
                         "00000000cf0000001d6f6367666f7267652e7075626c69635f736166655f73746174652e76310000001d6f6367666f726765"
                         "2e7075626c69635f736166655f73746174652e763100000000000000000000000200001f4000001f40010100000000000000"
                         "00000000000001000400000001000000000000000100000000010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                         "0000000100040100000000000000010000000000000000000000000000000000000100000000000000000100000000000000"
                         "000000000000000000000000010000000e636172645f73656c656374696f6e0101000000010000001470303a5350454c4c5f"
                         "545241505f5a4f4e453a30"),
            "paired public observation canonical bytes golden vector failed");
    const std::string paired_public_observation_text(
        paired_public_observation_bytes.begin(), paired_public_observation_bytes.end());
    require(paired_public_observation_text.find("14821890") == std::string::npos &&
                paired_public_observation_text.find("7654321") == std::string::npos,
            "paired public observation leaked hidden card identity");
    const auto public_observation_digest_a =
        ygo::environment::public_observation_digest(public_observation_a);
    const auto public_observation_digest_b =
        ygo::environment::public_observation_digest(public_observation_b);
    require(public_observation_digest_a == public_observation_digest_b,
            "paired worlds produced different public observation digests");
    require(public_observation_digest_a ==
                "be50bc48a0dfc60398e2cae94483b51f4a018168a99c01bd5556f0884b091207",
            "paired public observation digest golden vector failed");

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
                         "0000000e636172645f73656c656374696f6e0001010000001470303a5350454c4c5f545241505f5a4f4e453a3000000001000000030000000000"),
            "public action key canonical bytes golden vector failed");
    require(public_a ==
                "public_action.v1.000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
                "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
                "0000000e636172645f73656c656374696f6e0001010000001470303a5350454c4c5f545241505f5a4f4e453a3000000001000000030000000000",
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
        "card_selection", public_domain_a);
    const auto digest_b = ygo::environment::public_candidate_domain_digest(
        "card_selection", public_domain_b);
    require(digest_a == digest_b, "paired worlds produced different public domain digests");
    require(digest_a == "b35640b35822c76ed165a65f86c6d7ac5520abdf4359482b7608bf125274e1e6",
            "public candidate-domain digest golden vector failed");

    ygo::environment::PublicSemanticDecisionIdentityInput decision_input;
    decision_input.episode_semantic_id = std::string(64, 'a');
    decision_input.decision_index = 17;
    decision_input.acting_player = 1;
    decision_input.request_kind = "card_selection";
    decision_input.public_observation_digest = public_observation_digest_a;
    decision_input.public_candidate_domain_digest = digest_a;
    const auto decision_a = ygo::environment::public_semantic_decision_id(decision_input);
    decision_input.public_candidate_domain_digest = digest_b;
    const auto decision_b = ygo::environment::public_semantic_decision_id(decision_input);
    require(decision_a == decision_b, "paired worlds produced different public decision IDs");
    require(decision_a == "aa97f600faf5ff2977aa708173cccd3d3f92e77d413fe3e2b003a68f8ff54c06",
            "public semantic decision identity golden vector failed");

    require(ygo::environment::detail::resolve_public_action_key(
                {{public_a, internal_a}}, public_a)
                .value_or("") == internal_a,
            "public key did not resolve to its exact internal candidate");
    require(ygo::environment::detail::resolve_public_action_key(
                {{public_b, internal_b}}, public_b)
                .value_or("") == internal_b,
            "paired public key did not resolve to the alternate internal candidate");
}

void test_typed_public_choices_are_part_of_identity() {
    auto no = typed_choice_action(ygo::environment::PublicChoiceKind::YesNo, 0);
    auto yes = typed_choice_action(ygo::environment::PublicChoiceKind::YesNo, 1);
    require(ygo::environment::public_action_key(no) != ygo::environment::public_action_key(yes),
            "yes/no choices shared a public key");
    auto effect_no = typed_choice_action(ygo::environment::PublicChoiceKind::EffectYesNo, 0);
    require(ygo::environment::public_action_key(no) != ygo::environment::public_action_key(effect_no),
            "ordinary and effect yes/no choices shared a public key");

    auto effect_zero = typed_choice_action(ygo::environment::PublicChoiceKind::EffectChoice, 0);
    auto effect_one = typed_choice_action(ygo::environment::PublicChoiceKind::EffectChoice, 1);
    require(ygo::environment::public_action_key(effect_zero) !=
                ygo::environment::public_action_key(effect_one),
            "distinct effect choices shared a public key");

    auto option_zero = typed_choice_action(ygo::environment::PublicChoiceKind::OptionValue, 0x100000000ULL, 0);
    auto option_one = typed_choice_action(ygo::environment::PublicChoiceKind::OptionValue, 0x100000000ULL, 1);
    require(ygo::environment::public_action_key(option_zero) !=
                ygo::environment::public_action_key(option_one),
            "distinct option response selectors shared a public key");
    auto option_value_zero =
        typed_choice_action(ygo::environment::PublicChoiceKind::OptionValue, 0x100000000ULL, 0);
    auto option_value_one =
        typed_choice_action(ygo::environment::PublicChoiceKind::OptionValue, 0x200000000ULL, 0);
    require(ygo::environment::public_action_key(option_value_zero) !=
                ygo::environment::public_action_key(option_value_one),
            "distinct option values shared a public key");

    auto number_one = typed_choice_action(ygo::environment::PublicChoiceKind::AnnouncementNumber, 7, 0);
    auto number_two = typed_choice_action(ygo::environment::PublicChoiceKind::AnnouncementNumber, 8, 1);
    require(ygo::environment::public_action_key(number_one) !=
                ygo::environment::public_action_key(number_two),
            "distinct announcement values shared a public key");
    auto announcement_same_selector_a =
        typed_choice_action(ygo::environment::PublicChoiceKind::AnnouncementNumber, 7, 0);
    auto announcement_same_selector_b =
        typed_choice_action(ygo::environment::PublicChoiceKind::AnnouncementNumber, 8, 0);
    require(ygo::environment::public_action_key(announcement_same_selector_a) !=
                ygo::environment::public_action_key(announcement_same_selector_b),
            "distinct announcement values with the same selector shared a public key");

    auto invalid_option = typed_choice_action(ygo::environment::PublicChoiceKind::OptionValue, 7);
    bool invalid_option_rejected = false;
    try {
        (void)ygo::environment::public_action_key(invalid_option);
    } catch (const std::invalid_argument&) {
        invalid_option_rejected = true;
    }
    require(invalid_option_rejected, "option choice without response selector was accepted");
}

void test_attached_internal_context_is_sanitized_before_public_digest() {
    auto observation_a = hidden_card_observation();
    auto observation_b = hidden_card_observation();
    ygo::observation::attach_decision_context(
        observation_a, attached_request("internal-decision-a", "internal-continuation-a"));
    ygo::observation::attach_decision_context(
        observation_b, attached_request("internal-decision-b", "internal-continuation-b"));

    require(observation_a.decision_context.decision_id != observation_b.decision_context.decision_id,
            "attached test requests did not carry distinct internal decision IDs");
    require(observation_a.decision_context.continuation_id != observation_b.decision_context.continuation_id,
            "attached test requests did not carry distinct internal continuation IDs");
    require(ygo::observation::canonical_serialize(observation_a) !=
                ygo::observation::canonical_serialize(observation_b),
            "internal PlayerObservation records unexpectedly erased their own context");

    const auto public_a = ygo::environment::project_public_observation(observation_a);
    const auto public_b = ygo::environment::project_public_observation(observation_b);
    require(ygo::environment::canonical_public_environment_observation_bytes(public_a) ==
                ygo::environment::canonical_public_environment_observation_bytes(public_b),
            "attached internal context changed the public observation projection");
    const auto digest_a = ygo::environment::public_observation_digest(public_a);
    const auto digest_b = ygo::environment::public_observation_digest(public_b);
    require(digest_a == digest_b, "attached internal context changed the public observation digest");
    require(ygo::environment::canonical_public_environment_observation_bytes(public_a) ==
                from_hex("0000002a6f6367666f7267652e7075626c69635f656e7669726f6e6d656e745f6f62736572766174696f6e2e76310000002a"
                         "6f6367666f7267652e7075626c69635f656e7669726f6e6d656e745f6f62736572766174696f6e2e76310100000000000000"
                         "00000000cf0000001d6f6367666f7267652e7075626c69635f736166655f73746174652e76310000001d6f6367666f726765"
                         "2e7075626c69635f736166655f73746174652e763100000000000000000000000200001f4000001f40010100000000000000"
                         "00000000000001000400000001000000000000000100000000010000001470303a5350454c4c5f545241505f5a4f4e453a30"
                         "0000000100040100000000000000010000000000000000000000000000000000000100000000000000000100000000000000"
                         "00000000000000000000000001000000066f7074696f6e010100000000"),
            "public observation canonical bytes golden vector failed");
    require(digest_a == "e5289b089bf725f1dc50a551aaeacb22c1b995ab9c119e5456bc56d2c7394979",
            "public observation digest golden vector failed");
    const auto public_bytes =
        ygo::environment::canonical_public_environment_observation_bytes(public_a);
    const std::string public_bytes_text(public_bytes.begin(), public_bytes.end());
    require(public_bytes_text.find("internal-decision-a") == std::string::npos,
            "public observation projection leaked the internal decision ID");
    require(public_bytes_text.find("internal-continuation-a") == std::string::npos,
            "public observation projection leaked the internal continuation ID");

    auto changed_observation = observation_a;
    changed_observation.globals.life_points[0] = 7000;
    const auto changed_public_state =
        ygo::environment::project_public_observation(changed_observation);
    const auto changed_digest = ygo::environment::public_observation_digest(changed_public_state);
    require(changed_digest != digest_a, "public observation digest ignored visible public state");

    ygo::environment::PublicSemanticDecisionIdentityInput decision_input;
    decision_input.episode_semantic_id = std::string(64, 'a');
    decision_input.decision_index = 17;
    decision_input.acting_player = 1;
    decision_input.request_kind = "option";
    decision_input.public_observation_digest = digest_a;
    decision_input.public_candidate_domain_digest = std::string(64, 'c');
    const auto decision_a = ygo::environment::public_semantic_decision_id(decision_input);
    require(decision_a == "a3687d26b773b8fdaecd12f488daf934c95c60fc58f25399981714b5a899be9d",
            "attached public semantic decision identity golden vector failed");
    decision_input.public_observation_digest = changed_digest;
    const auto decision_b = ygo::environment::public_semantic_decision_id(decision_input);
    require(decision_a != decision_b, "public semantic decision ID ignored public observation state");
}

void test_public_domain_order_and_mapping_collision() {
    const auto first = ygo::environment::public_action_key(hidden_card_action());
    auto second_input = hidden_card_action();
    second_input.phase = 1;
    const auto second = ygo::environment::public_action_key(second_input);
    require(first != second, "distinct public action descriptors collided");

    const auto digest = ygo::environment::public_candidate_domain_digest(
        "card_selection", {first, second});
    const auto reversed = ygo::environment::public_candidate_domain_digest(
        "card_selection", {second, first});
    require(digest != reversed, "public domain digest ignored authoritative order");

    bool duplicate_domain_rejected = false;
    try {
        (void)ygo::environment::public_candidate_domain_digest(
            "card_selection", {first, first});
    } catch (const std::invalid_argument&) {
        duplicate_domain_rejected = true;
    }
    require(duplicate_domain_rejected, "duplicate public domain was accepted");

    bool empty_domain_rejected = false;
    try {
        (void)ygo::environment::public_candidate_domain_digest("card_selection", {});
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
        test_public_safe_state_is_owned_by_projection();
        test_paired_world_hidden_card_projection();
        test_typed_public_choices_are_part_of_identity();
        test_attached_internal_context_is_sanitized_before_public_digest();
        test_public_domain_order_and_mapping_collision();
        test_public_codec_rejects_non_locator_reference();
        std::cout << "public_action_identity_tests=passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
