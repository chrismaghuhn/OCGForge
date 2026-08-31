#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_profile_codec.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PredicateRef predicate(const PredicateScope scope, const std::string& id) {
    PredicateRef value;
    value.scope = scope;
    value.predicate_id = id;
    return value;
}

StrategyProfileV1 valid_profile() {
    StrategyProfileV1 value;
    value.matchup_id = "ocgforge.matchup.swordsoul_salamangreat.v1";
    value.rules_bundle_id =
        "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
    value.format_id = "TCG_ADVANCED_2026_05_18";
    value.duel_mode = "DUEL_MODE_MR5";
    value.duel_flags = 190464;
    value.own_deck_role = 0;
    value.own_deck_id = "ocgforge.swordsoul_tenyi.ml_v1";
    value.own_deck_sha256 =
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
    value.opponent_deck_role = 1;
    value.opponent_deck_id = "ocgforge.salamangreat.ml_v1";
    value.opponent_deck_sha256 =
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";

    value.card_roles = {
        {100, {"role.engine", "role.starter"}},
        {200, {"role.interaction"}},
    };

    value.resources = {
        {"resource.follow_up", "fact.follow_up", 3, 20, 10},
        {"resource.normal_summon", "fact.normal_summon", 1, 30, 5},
    };

    const auto fact_a = predicate(PredicateScope::Observation, "fact.a");
    const auto fact_b = predicate(PredicateScope::Candidate, "fact.b");
    value.candidate_intents = {
        {"intent.advance", {fact_a}},
        {"intent.interact", {fact_b}},
    };

    GoalDefinition goal;
    goal.goal_id = "goal.establish";
    goal.priority = 100;
    goal.preconditions = {fact_a, fact_b};
    goal.completion_predicates = {fact_a};
    goal.stop_predicates = {fact_b};
    value.goals = {goal};

    LineNode first_node;
    first_node.node_id = "node.first";
    first_node.candidate_intent_ids = {"intent.advance"};
    first_node.completion_predicates = {fact_a};
    first_node.preserve_resource_ids = {"resource.follow_up"};
    first_node.stop_predicates = {};

    LineNode second_node;
    second_node.node_id = "node.second";
    second_node.candidate_intent_ids = {"intent.interact"};
    second_node.completion_predicates = {fact_b};
    second_node.preserve_resource_ids = {"resource.normal_summon"};
    second_node.stop_predicates = {};

    LineDefinition line;
    line.line_id = "line.foundation";
    line.goal_id = "goal.establish";
    line.applicability_predicates = {fact_a};
    line.required_resources = {{"resource.normal_summon", 1}};
    line.optional_resources = {"resource.follow_up"};
    line.nodes = {first_node, second_node};
    line.dependencies = {{"node.first", "node.second"}};
    line.recovery_edge_ids = {"recovery.foundation"};
    value.lines = {line};

    RecoveryEdge recovery;
    recovery.recovery_edge_id = "recovery.foundation";
    recovery.source_kind = RecoverySourceKind::Line;
    recovery.source_id = "line.foundation";
    recovery.invalidation_reason_ids = {"resource_consumed"};
    recovery.preconditions = {fact_a};
    recovery.candidate_intent_ids = {"intent.interact"};
    recovery.target_goal_id = "goal.establish";
    recovery.target_line_id = "line.foundation";
    recovery.preserve_resource_ids = {"resource.follow_up"};
    recovery.confidence_cap = ConfidenceClass::Medium;
    value.recovery_edges = {recovery};

    InteractionRule interaction;
    interaction.interaction_id = "interaction.public";
    interaction.trigger_predicates = {fact_b};
    interaction.candidate_intent_ids = {"intent.interact"};
    interaction.timing_priority = 50;
    interaction.preserve_resource_ids = {"resource.follow_up"};
    value.interactions = {interaction};

    value.preferences = {
        {ScoreDimension::ProfilePreference, PreferenceSubjectKind::Global, "global", 10},
        {ScoreDimension::ProfilePreference, PreferenceSubjectKind::Goal, "goal.establish", 20},
    };
    value.profile_id = strategy_profile_id(value);
    return value;
}

TeacherPolicyBindingV1 valid_binding(const StrategyProfileV1& profile) {
    TeacherPolicyBindingV1 value;
    value.teacher_core_artifact_identity = "ocgforge.teacher_core.v1";
    value.strategy_profile_id = profile.profile_id;
    value.score_contract_identity = std::string(kTeacherScoreContractId);
    value.fallback_contract_identity = std::string(kTeacherFallbackContractId);
    value.tie_break_contract_identity = std::string(kTeacherTieBreakContractId);
    value.diagnostic_contract_identity = std::string(kTeacherDiagnosticContractId);
    value.teacher_policy_binding_id = teacher_policy_binding_id(value);
    return value;
}

void test_profile_round_trip_and_identity() {
    const auto profile = valid_profile();
    const auto content = canonical_strategy_profile_content_bytes(profile);
    const auto encoded = canonical_strategy_profile_bytes(profile);
    require(ygo::trace::sha256_bytes(content) ==
                profile.profile_id.substr(kStrategyProfileIdentityPrefix.size()),
            "profile content digest is not the declared identity");

    const auto decoded = decode_strategy_profile(encoded);
    require(static_cast<bool>(decoded), "valid StrategyProfile did not decode");
    require(decoded.value->profile_id == profile.profile_id, "profile ID changed on decode");
    require(canonical_strategy_profile_bytes(*decoded.value) == encoded,
            "profile encode/decode/re-encode changed canonical bytes");

    const auto copied = profile;
    require(canonical_strategy_profile_content_bytes(copied) == content,
            "identical profile content changed identity");

    const auto binding = valid_binding(profile);
    const auto binding_content = canonical_teacher_policy_binding_content_bytes(binding);
    const auto binding_encoded = canonical_teacher_policy_binding_bytes(binding);
    require(ygo::trace::sha256_bytes(binding_content) ==
                binding.teacher_policy_binding_id.substr(
                    kTeacherPolicyBindingIdentityPrefix.size()),
            "binding content digest is not the declared identity");
    const auto binding_decoded = decode_teacher_policy_binding(binding_encoded);
    require(static_cast<bool>(binding_decoded),
            "valid TeacherPolicyBinding did not decode");
    require(canonical_teacher_policy_binding_bytes(*binding_decoded.value) == binding_encoded,
            "binding encode/decode/re-encode changed canonical bytes");
    require(validate_teacher_policy_binding(binding, profile),
            "binding did not validate against its profile");
}

void test_exact_deck_roles_and_binding() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto profile = valid_profile();
    std::string diagnostic;
    require(validate_strategy_profile_binding(profile, config, &diagnostic),
            "first-deck profile did not bind to certified environment: " + diagnostic);

    auto reverse = profile;
    reverse.own_deck_role = 1;
    reverse.own_deck_id = profile.opponent_deck_id;
    reverse.own_deck_sha256 = profile.opponent_deck_sha256;
    reverse.opponent_deck_role = 0;
    reverse.opponent_deck_id = profile.own_deck_id;
    reverse.opponent_deck_sha256 = profile.own_deck_sha256;
    reverse.profile_id = strategy_profile_id(reverse);
    diagnostic.clear();
    require(validate_strategy_profile_binding(reverse, config, &diagnostic),
            "second-deck profile did not bind to certified environment: " + diagnostic);
}

void test_predicate_ref_canonical_order() {
    const auto profile = valid_profile();
    for (const auto& goal : profile.goals) {
        for (std::size_t index = 1; index < goal.preconditions.size(); ++index) {
            require(canonical_predicate_ref_bytes(goal.preconditions[index - 1]) <
                        canonical_predicate_ref_bytes(goal.preconditions[index]),
                    "goal PredicateRef vector is not canonical-byte sorted");
        }
    }
}

}  // namespace

int main() {
    try {
        test_profile_round_trip_and_identity();
        test_exact_deck_roles_and_binding();
        test_predicate_ref_canonical_order();
        std::cout << "strategy_profile_codec_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "strategy_profile_codec_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
