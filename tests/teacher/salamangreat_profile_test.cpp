#include "ygo/teacher/salamangreat_profile.hpp"

#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_profile_codec.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const std::vector<std::uint32_t>& locked_salamangreat_passcodes() {
    static const std::vector<std::uint32_t> values = {
        1295111,  2772337,  10045474, 11962031, 14558127, 14812471, 14934922,
        20618081, 24224830, 26889158, 31313405, 41463181, 48815792, 51339637,
        52155219, 52277807, 56003780, 57134592, 57160136, 57357130, 64178424,
        73642296, 74652966, 83533296, 87327776, 87871125, 94620082, 97268402,
    };
    return values;
}

bool has_role(const StrategyProfileV1& profile, const std::uint32_t passcode,
              const std::string& role_id) {
    const auto entry = std::find_if(
        profile.card_roles.begin(), profile.card_roles.end(),
        [passcode](const auto& value) { return value.passcode == passcode; });
    return entry != profile.card_roles.end() &&
           std::binary_search(entry->role_ids.begin(), entry->role_ids.end(), role_id);
}

void test_exact_binding_and_identity() {
    const auto profile = make_salamangreat_profile();
    std::string diagnostic;
    require(validate_strategy_profile(profile, &diagnostic),
            "Salamangreat profile validation failed: " + diagnostic);
    require(validate_strategy_profile_binding(profile,
                                              CertifiedEnvironmentConfig::canonical(),
                                              &diagnostic),
            "Salamangreat profile binding failed: " + diagnostic);
    require(profile.matchup_id == kCertifiedMatchupId, "wrong Salamangreat matchup");
    require(profile.rules_bundle_id ==
                "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
            "wrong rules bundle");
    require(profile.format_id == "TCG_ADVANCED_2026_05_18" &&
                profile.duel_mode == "DUEL_MODE_MR5" && profile.duel_flags == 190464,
            "wrong certified duel configuration");
    require(profile.own_deck_role == 1 &&
                profile.own_deck_id == "ocgforge.salamangreat.ml_v1" &&
                profile.own_deck_sha256 ==
                    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
            "wrong Salamangreat own-deck binding");
    require(profile.opponent_deck_role == 0 &&
                profile.opponent_deck_id == "ocgforge.swordsoul_tenyi.ml_v1" &&
                profile.opponent_deck_sha256 ==
                    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
            "wrong Swordsoul opponent binding");

    const auto content = canonical_strategy_profile_content_bytes(profile);
    require(profile.profile_id == strategy_profile_id(profile),
            "profile ID was not derived from canonical content");
    require(ygo::trace::sha256_bytes(content) ==
                profile.profile_id.substr(kStrategyProfileIdentityPrefix.size()),
            "profile content digest does not match profile ID");
    require(ygo::trajectory::is_canonical_identity(profile.profile_id,
                                                    kStrategyProfileIdentityPrefix),
            "profile ID is not canonical");

    const auto copied = make_salamangreat_profile();
    require(canonical_strategy_profile_content_bytes(copied) == content &&
                copied.profile_id == profile.profile_id,
            "repeated profile construction was not deterministic");

    const auto encoded = canonical_strategy_profile_bytes(profile);
    const auto decoded = decode_strategy_profile(encoded);
    require(decoded && canonical_strategy_profile_bytes(*decoded.value) == encoded,
            "Salamangreat profile did not round-trip canonically");
}

void test_locked_roles_and_shape() {
    const auto profile = make_salamangreat_profile();
    const auto& allowed = locked_salamangreat_passcodes();
    require(std::is_sorted(allowed.begin(), allowed.end()),
            "locked Salamangreat passcode set is not sorted");
    for (const auto& entry : profile.card_roles) {
        require(std::binary_search(allowed.begin(), allowed.end(), entry.passcode),
                "profile references a passcode outside the locked Salamangreat deck");
    }

    require(has_role(profile, 11962031, "role.starter.of_fire"),
            "Of Fire starter role is missing");
    require(has_role(profile, 11962031, "role.trigger.of_fire") &&
                has_role(profile, 26889158, "role.trigger.gazelle"),
            "engine-trigger roles are missing");
    require(has_role(profile, 26889158, "role.starter.gazelle"),
            "Gazelle starter role is missing");
    require(has_role(profile, 52277807, "role.spinny.activation") &&
                !has_role(profile, 52277807, "role.extender.spinny"),
            "Spinny activation role is missing");
    require(has_role(profile, 83533296, "role.charge.access") &&
                !has_role(profile, 83533296, "role.recovery.charge"),
            "Charge access role is missing");
    require(has_role(profile, 14812471, "role.payoff.link1") &&
                has_role(profile, 87871125, "role.payoff.link2") &&
                has_role(profile, 57134592, "role.payoff.link4") &&
                has_role(profile, 31313405, "role.payoff.link4"),
            "Link payoff roles are incomplete");
    require(has_role(profile, 87327776, "role.bridge.rank3") &&
                has_role(profile, 2772337, "role.recovery.princess"),
            "bridge/recovery roles are incomplete");
    require(has_role(profile, 1295111, "role.sanctuary.access") &&
                !has_role(profile, 1295111, "role.searcher") &&
                has_role(profile, 52155219, "role.circle.access") &&
                !has_role(profile, 52155219, "role.searcher"),
            "Sanctuary/Circle access roles are not separated from search");
    for (const auto passcode : {14558127U, 73642296U, 97268402U, 10045474U,
                                24224830U, 51339637U, 14934922U}) {
        require(has_role(profile, passcode, "role.interaction"),
                "locked interaction card is not mapped to role.interaction");
    }
    require(!has_role(profile, 11962031, "role.payoff.link4") &&
                !has_role(profile, 26889158, "role.payoff.link4"),
            "starter was incorrectly classified as Link-4 payoff");

    require(profile.goals.size() == 2 && profile.lines.size() == 2,
            "minimal Salamangreat profile has an unexpected plan surface");
    const auto chain_line = std::find_if(
        profile.lines.begin(), profile.lines.end(), [](const auto& line) {
            return line.line_id == "line.chain.salamangreat";
        });
    require(chain_line != profile.lines.end() && chain_line->required_resources.empty() &&
                chain_line->nodes.size() == 6,
            "Salamangreat chain strategy does not expose its independent trigger nodes");
    for (const auto& line : profile.lines) {
        require(line.dependencies.empty(),
                "minimal Salamangreat profile contains an unproven dependency");
    }
    for (const auto& intent : profile.candidate_intents) {
        require(intent.intent_id.find("copy") == std::string::npos &&
                    intent.intent_id.find("budget") == std::string::npos,
                "Task-10 profile invented a copy-budget intent");
    }
}

void test_json_binding_fixture_is_present() {
#ifdef YGO_SOURCE_DIR
    const auto profile = make_salamangreat_profile();
    std::ifstream input(std::string(YGO_SOURCE_DIR) +
                        "/fixtures/teacher_profiles/ocgforge.salamangreat.v1.json");
    require(input.good(), "Salamangreat profile authoring fixture is missing");
    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    for (const auto* value : {
             "ocgforge.salamangreat.ml_v1",
             "ocgforge.swordsoul_tenyi.ml_v1",
             "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
             "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
             "ocgforge.matchup.swordsoul_salamangreat.v1",
             "role.starter.of_fire",
             "role.payoff.link4",
             "role.interaction",
         }) {
        require(json.find(value) != std::string::npos,
                std::string("Salamangreat JSON/factory binding evidence is missing: ") + value);
    }
    for (const auto& entry : profile.card_roles) {
        require(json.find("\"passcode\": " + std::to_string(entry.passcode)) !=
                    std::string::npos,
                "Salamangreat JSON is missing a factory card-role passcode");
        for (const auto& role_id : entry.role_ids) {
            require(json.find(role_id) != std::string::npos,
                    "Salamangreat JSON is missing a factory role ID");
        }
    }
    for (const auto& resource : profile.resources) {
        require(json.find(resource.resource_id) != std::string::npos &&
                    json.find(resource.public_fact_id) != std::string::npos,
                "Salamangreat JSON is missing a factory resource");
    }
    for (const auto& intent : profile.candidate_intents) {
        require(json.find(intent.intent_id) != std::string::npos,
                "Salamangreat JSON is missing a factory candidate intent");
    }
    for (const auto& goal : profile.goals) {
        require(json.find(goal.goal_id) != std::string::npos,
                "Salamangreat JSON is missing a factory goal");
    }
    for (const auto& line : profile.lines) {
        require(json.find(line.line_id) != std::string::npos,
                "Salamangreat JSON is missing a factory line");
    }
    for (const auto& edge : profile.recovery_edges) {
        require(json.find(edge.recovery_edge_id) != std::string::npos,
                "Salamangreat JSON is missing a factory recovery edge");
    }
    for (const auto& interaction : profile.interactions) {
        require(json.find(interaction.interaction_id) != std::string::npos,
                "Salamangreat JSON is missing a factory interaction");
    }
#endif
}

}  // namespace

int main() {
    try {
        test_exact_binding_and_identity();
        test_locked_roles_and_shape();
        test_json_binding_fixture_is_present();
        std::cout << "salamangreat_profile_id="
                  << make_salamangreat_profile().profile_id << '\n';
        std::cout << "salamangreat_profile_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "salamangreat_profile_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
