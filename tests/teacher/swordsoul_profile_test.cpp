#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_profile_codec.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const std::vector<std::uint32_t>& locked_swordsoul_passcodes() {
    static const std::vector<std::uint32_t> values = {
        5041348, 9464441, 10045474, 14558127, 14821890, 19048328,
        20001443, 23431858, 24224830, 24557335, 32519092, 43202238, 47710198,
        51684157, 55273560, 56465981, 56495147, 69248256, 78917791, 83755611,
        87052196, 93490856, 93850690, 96633955, 97268402, 98159737,
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

TeacherPolicyBindingV1 binding_for(const StrategyProfileV1& profile) {
    TeacherPolicyBindingV1 binding;
    binding.teacher_core_artifact_identity = "ocgforge.teacher_core.v1";
    binding.strategy_profile_id = profile.profile_id;
    binding.score_contract_identity = std::string(kTeacherScoreContractId);
    binding.fallback_contract_identity = std::string(kTeacherFallbackContractId);
    binding.tie_break_contract_identity = std::string(kTeacherTieBreakContractId);
    binding.diagnostic_contract_identity = std::string(kTeacherDiagnosticContractId);
    binding.teacher_policy_binding_id = teacher_policy_binding_id(binding);
    return binding;
}

void test_exact_binding_and_identity() {
    const auto profile = make_swordsoul_tenyi_profile();
    std::string diagnostic;
    require(validate_strategy_profile(profile, &diagnostic),
            "Swordsoul profile validation failed: " + diagnostic);
    require(validate_strategy_profile_binding(profile,
                                              CertifiedEnvironmentConfig::canonical(),
                                              &diagnostic),
            "Swordsoul profile binding failed: " + diagnostic);
    require(profile.matchup_id == kCertifiedMatchupId, "wrong Swordsoul matchup");
    require(profile.rules_bundle_id ==
                "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
            "wrong Swordsoul rules bundle");
    require(profile.format_id == "TCG_ADVANCED_2026_05_18" &&
                profile.duel_mode == "DUEL_MODE_MR5" && profile.duel_flags == 190464,
            "wrong certified duel configuration");
    require(profile.own_deck_role == 0 && profile.own_deck_id ==
                "ocgforge.swordsoul_tenyi.ml_v1" &&
                profile.own_deck_sha256 ==
                    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
            "wrong Swordsoul own-deck binding");
    require(profile.opponent_deck_role == 1 && profile.opponent_deck_id ==
                "ocgforge.salamangreat.ml_v1" &&
                profile.opponent_deck_sha256 ==
                    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
            "wrong Salamangreat opponent binding");

    const auto content = canonical_strategy_profile_content_bytes(profile);
    require(profile.profile_id == strategy_profile_id(profile),
            "profile ID was not derived from canonical content");
    require(ygo::trace::sha256_bytes(content) ==
                profile.profile_id.substr(kStrategyProfileIdentityPrefix.size()),
            "profile content digest does not match profile ID");
    require(ygo::trajectory::is_canonical_identity(profile.profile_id,
                                                    kStrategyProfileIdentityPrefix),
            "profile ID is not canonical");

    const auto copied = make_swordsoul_tenyi_profile();
    require(canonical_strategy_profile_content_bytes(copied) == content &&
                copied.profile_id == profile.profile_id,
            "repeated profile construction was not deterministic");

    const auto encoded = canonical_strategy_profile_bytes(profile);
    const auto decoded = decode_strategy_profile(encoded);
    require(decoded && canonical_strategy_profile_bytes(*decoded.value) == encoded,
            "Swordsoul profile did not round-trip canonically");

    const auto binding = binding_for(profile);
    require(validate_teacher_policy_binding(binding, profile, &diagnostic),
            "Swordsoul policy binding failed: " + diagnostic);
}

void test_locked_roles_and_minimal_slice() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto& allowed = locked_swordsoul_passcodes();
    require(std::is_sorted(allowed.begin(), allowed.end()),
            "test locked passcode set is not sorted");
    for (const auto& entry : profile.card_roles) {
        require(std::binary_search(allowed.begin(), allowed.end(), entry.passcode),
                "profile references a passcode outside the locked Swordsoul deck");
    }

    require(has_role(profile, 20001443, "role.starter.mo_ye"),
            "Mo Ye starter role is missing");
    require(has_role(profile, 93490856, "role.starter.longyuan"),
            "Longyuan starter role is missing");
    require(has_role(profile, 56495147, "role.starter.taia"),
            "Taia starter role is missing");
    require(has_role(profile, 87052196, "role.tenyi.body") &&
                has_role(profile, 32519092, "role.tenyi.monk"),
            "Tenyi body/Monk roles are missing");
    require(has_role(profile, 69248256, "role.payoff.level8") &&
                has_role(profile, 47710198, "role.payoff.level10"),
            "Chixiao/Level-10 payoff roles are missing");
    require(has_role(profile, 93850690, "role.recovery.summit") &&
                has_role(profile, 51684157, "role.interaction"),
            "Summit/interaction roles are missing");

    const std::vector<std::string> expected_intents = {
        "intent.board.breaker", "intent.interaction.chain", "intent.level10.payoff",
        "intent.level8.payoff", "intent.longyuan.access",
        "intent.mo_ye.starter", "intent.monk.access", "intent.search",
        "intent.summit.recovery", "intent.taia.recovery", "intent.tenyi.body",
    };
    for (const auto& intent : expected_intents) {
        require(std::any_of(profile.candidate_intents.begin(), profile.candidate_intents.end(),
                            [&intent](const auto& value) { return value.intent_id == intent; }),
                "minimal Swordsoul candidate intent is missing: " + intent);
    }

}

}  // namespace

int main() {
    try {
        test_exact_binding_and_identity();
        test_locked_roles_and_minimal_slice();
        std::cout << "swordsoul_profile_id="
                  << make_swordsoul_tenyi_profile().profile_id << '\n';
        std::cout << "swordsoul_profile_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "swordsoul_profile_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
