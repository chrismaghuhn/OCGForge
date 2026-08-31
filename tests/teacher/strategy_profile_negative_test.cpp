#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_profile_codec.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void require_throw(Function&& function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

PredicateAtom token_atom(const std::string& value) {
    PredicateAtom atom;
    atom.kind = PredicateAtomKind::Token;
    atom.token = value;
    return atom;
}

PredicateAtom u64_atom(const std::uint64_t value) {
    PredicateAtom atom;
    atom.kind = PredicateAtomKind::U64;
    atom.u64 = value;
    return atom;
}

PredicateRef predicate(const PredicateScope scope, const std::string& id,
                       std::vector<PredicateAtom> arguments = {}) {
    PredicateRef value;
    value.scope = scope;
    value.predicate_id = id;
    value.arguments = std::move(arguments);
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
        {"resource.follow_up", "public.life_points.self", 8000, 20, 10},
        {"resource.normal_summon", "public.turn.phase", 3, 30, 5},
    };

    const auto candidate_action = predicate(
        PredicateScope::Candidate, "candidate.action_kind_equals",
        {token_atom("yes_no")});
    const auto candidate_choice =
        predicate(PredicateScope::Candidate, "candidate.choice_present");
    const auto public_self = predicate(
        PredicateScope::Observation, "observation.fact_u64_at_least",
        {token_atom("public.life_points.self"), u64_atom(1)});
    const auto public_opponent = predicate(
        PredicateScope::Observation, "observation.fact_u64_at_most",
        {token_atom("public.life_points.opponent"), u64_atom(8000)});
    value.candidate_intents = {
        {"intent.advance", {candidate_action}},
        {"intent.interact", {candidate_choice}},
    };

    value.goals = {
        {"goal.establish", 100, {public_opponent, public_self}, {public_self},
         {public_opponent}},
    };

    LineNode first_node;
    first_node.node_id = "node.first";
    first_node.candidate_intent_ids = {"intent.advance"};
    first_node.completion_predicates = {public_self};
    first_node.preserve_resource_ids = {"resource.follow_up"};

    LineNode second_node;
    second_node.node_id = "node.second";
    second_node.candidate_intent_ids = {"intent.interact"};
    second_node.completion_predicates = {public_opponent};
    second_node.preserve_resource_ids = {"resource.normal_summon"};

    LineDefinition line;
    line.line_id = "line.foundation";
    line.goal_id = "goal.establish";
    line.applicability_predicates = {public_self};
    line.required_resources = {{"resource.normal_summon", 1}};
    line.optional_resources = {"resource.follow_up"};
    line.nodes = {first_node, second_node};
    line.dependencies = {{"node.first", "node.second"}};
    line.recovery_edge_ids = {"recovery.foundation"};
    value.lines = {line};

    value.recovery_edges = {
        {"recovery.foundation",
         RecoverySourceKind::Line,
         "line.foundation",
         {"resource_consumed"},
         {public_self},
         {"intent.interact"},
         "goal.establish",
         std::string("line.foundation"),
         {"resource.follow_up"},
         ConfidenceClass::Medium},
    };

    value.interactions = {
        {"interaction.public", {public_opponent}, {"intent.interact"}, 50,
         {"resource.follow_up"}},
    };
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

void test_profile_value_rejections() {
    auto value = valid_profile();
    value.own_deck_role = 2;
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "DeckRole > 1 was accepted");

    value = valid_profile();
    value.resources[0].resource_id = "Resource.Upper";
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "noncanonical token was accepted");

    value = valid_profile();
    value.resources.push_back(value.resources.front());
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "duplicate resource was accepted");

    value = valid_profile();
    std::swap(value.resources[0], value.resources[1]);
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "unsorted resource collection was accepted");

    value = valid_profile();
    value.goals[0].preconditions.push_back(value.goals[0].preconditions.front());
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "duplicate PredicateRef was accepted");

    value = valid_profile();
    std::swap(value.goals[0].preconditions[0], value.goals[0].preconditions[1]);
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "unsorted PredicateRef was accepted");

    value = valid_profile();
    value.lines[0].required_resources[0].resource_id = "resource.missing";
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "dangling resource reference was accepted");

    value = valid_profile();
    value.lines[0].goal_id = "goal.missing";
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "dangling goal reference was accepted");

    value = valid_profile();
    value.lines[0].dependencies.push_back({"node.second", "node.first"});
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "cyclic line graph was accepted");

    value = valid_profile();
    value.lines[0].nodes[0].candidate_intent_ids = {"intent.missing"};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "dangling candidate-intent reference was accepted");

    value = valid_profile();
    value.lines[0].recovery_edge_ids = {"recovery.missing"};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "dangling recovery reference was accepted");

    value = valid_profile();
    value.recovery_edges[0].invalidation_reason_ids = {"unknown_reason"};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "unregistered invalidation reason was accepted");

    value = valid_profile();
    value.lines[0].dependencies = {{"node.first", "node.missing"}};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "dangling node reference was accepted");

    value = valid_profile();
    value.recovery_edges[0].source_id = "line.missing";
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "dangling line reference was accepted");

    value = valid_profile();
    value.recovery_edges[0].confidence_cap = static_cast<ConfidenceClass>(4);
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "invalid confidence code was accepted");

    value = valid_profile();
    value.goals[0].priority = 1000001;
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "out-of-range score value was accepted");

    value = valid_profile();
    value.format_id = std::string(1, static_cast<char>(0xff));
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(value); },
                  "malformed UTF-8 was accepted");

    value = valid_profile();
    value.profile_id = "ocgforge.strategy_profile.v1." + std::string(64, '0');
    require_throw([&] { (void)canonical_strategy_profile_bytes(value); },
                  "mismatched profile identity was accepted");

    value = valid_profile();
    value.profile_id = "strategy_profile.v1." + std::string(64, '0');
    require_throw([&] { (void)canonical_strategy_profile_bytes(value); },
                  "shortened profile identity was accepted");

    value = valid_profile();
    value.matchup_id = "ocgforge.matchup.other.v1";
    value.profile_id = strategy_profile_id(value);
    const auto config = CertifiedEnvironmentConfig::canonical();
    std::string diagnostic;
    require(!validate_strategy_profile_binding(value, config, &diagnostic),
            "wrong matchup was accepted by environment binding");

    value = valid_profile();
    value.rules_bundle_id = std::string(64, '0');
    value.profile_id = strategy_profile_id(value);
    diagnostic.clear();
    require(!validate_strategy_profile_binding(value, config, &diagnostic),
            "wrong rules bundle was accepted");

    value = valid_profile();
    value.format_id = "TCG_ADVANCED_OTHER";
    value.profile_id = strategy_profile_id(value);
    diagnostic.clear();
    require(!validate_strategy_profile_binding(value, config, &diagnostic),
            "wrong format was accepted");

    value = valid_profile();
    value.duel_mode = "DUEL_MODE_OTHER";
    value.profile_id = strategy_profile_id(value);
    diagnostic.clear();
    require(!validate_strategy_profile_binding(value, config, &diagnostic),
            "wrong duel mode was accepted");

    value = valid_profile();
    value.duel_flags ^= 1;
    value.profile_id = strategy_profile_id(value);
    diagnostic.clear();
    require(!validate_strategy_profile_binding(value, config, &diagnostic),
            "wrong duel flags were accepted");

    value = valid_profile();
    value.own_deck_id = value.opponent_deck_id;
    value.profile_id = strategy_profile_id(value);
    diagnostic.clear();
    require(!validate_strategy_profile_binding(value, config, &diagnostic),
            "wrong own-deck binding was accepted");

    value = valid_profile();
    value.opponent_deck_id = value.own_deck_id;
    value.profile_id = strategy_profile_id(value);
    diagnostic.clear();
    require(!validate_strategy_profile_binding(value, config, &diagnostic),
            "wrong opponent-deck binding was accepted");
}

void test_predicate_field_scope_rejections() {
    const auto base = valid_profile();
    const auto candidate_predicate =
        base.candidate_intents.front().public_predicates.front();

    auto goal_preconditions = base;
    goal_preconditions.goals[0].preconditions = {candidate_predicate};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(goal_preconditions); },
                  "candidate predicate was accepted in goal preconditions");

    auto goal_completion = base;
    goal_completion.goals[0].completion_predicates = {candidate_predicate};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(goal_completion); },
                  "candidate predicate was accepted in goal completion");

    auto line_applicability = base;
    line_applicability.lines[0].applicability_predicates = {candidate_predicate};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(line_applicability); },
                  "candidate predicate was accepted in line applicability");

    auto node_completion = base;
    node_completion.lines[0].nodes[0].completion_predicates = {candidate_predicate};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(node_completion); },
                  "candidate predicate was accepted in node completion");

    auto recovery_preconditions = base;
    recovery_preconditions.recovery_edges[0].preconditions = {candidate_predicate};
    require_throw(
        [&] { (void)canonical_strategy_profile_content_bytes(recovery_preconditions); },
        "candidate predicate was accepted in recovery preconditions");

    auto interaction_trigger = base;
    interaction_trigger.interactions[0].trigger_predicates = {candidate_predicate};
    require_throw([&] { (void)canonical_strategy_profile_content_bytes(interaction_trigger); },
                  "candidate predicate was accepted in interaction triggers");
}

void test_binding_rejections() {
    const auto profile = valid_profile();
    auto binding = valid_binding(profile);

    binding.teacher_core_artifact_identity = "not-an-identity";
    require_throw([&] { (void)canonical_teacher_policy_binding_content_bytes(binding); },
                  "invalid ProducerImplementation identity was accepted");

    binding = valid_binding(profile);
    binding.strategy_profile_id = "strategy_profile.v1." + std::string(64, '0');
    require_throw([&] { (void)canonical_teacher_policy_binding_content_bytes(binding); },
                  "shortened binding profile identity was accepted");

    binding = valid_binding(profile);
    binding.score_contract_identity = "ocgforge.policy.other_score.v1";
    require_throw([&] { (void)canonical_teacher_policy_binding_content_bytes(binding); },
                  "wrong score contract was accepted");

    binding = valid_binding(profile);
    binding.fallback_contract_identity = "ocgforge.policy.other_fallback.v1";
    require_throw([&] { (void)canonical_teacher_policy_binding_content_bytes(binding); },
                  "wrong fallback contract was accepted");

    binding = valid_binding(profile);
    binding.tie_break_contract_identity = "ocgforge.policy.other_tie.v1";
    require_throw([&] { (void)canonical_teacher_policy_binding_content_bytes(binding); },
                  "wrong tie-break contract was accepted");

    binding = valid_binding(profile);
    binding.diagnostic_contract_identity = "ocgforge.teacher_other_explanation.v1";
    require_throw([&] { (void)canonical_teacher_policy_binding_content_bytes(binding); },
                  "wrong diagnostic contract was accepted");

    binding = valid_binding(profile);
    binding.teacher_policy_binding_id =
        "ocgforge.teacher_policy_binding.v1." + std::string(64, '0');
    require_throw([&] { (void)canonical_teacher_policy_binding_bytes(binding); },
                  "mismatched binding identity was accepted");

    binding = valid_binding(profile);
    binding.strategy_profile_id = "ocgforge.strategy_profile.v1." + std::string(64, '0');
    require(!validate_teacher_policy_binding(binding, profile),
            "binding with a different profile was accepted");
}

void test_decode_rejections() {
    const auto profile = valid_profile();
    auto bytes = canonical_strategy_profile_bytes(profile);
    bytes.push_back(0);
    require(!decode_strategy_profile(bytes), "profile trailing bytes were accepted");

    bytes = canonical_strategy_profile_bytes(profile);
    require(bytes.size() > 4, "profile encoding unexpectedly short");
    bytes[4] = static_cast<std::uint8_t>('x');
    require(!decode_strategy_profile(bytes), "unknown profile domain was accepted");

    bytes = canonical_strategy_profile_bytes(profile);
    const auto schema_offset = 4 + kStrategyProfileSchemaId.size() + 4;
    require(bytes.size() > schema_offset, "profile schema offset is out of range");
    bytes[schema_offset] = static_cast<std::uint8_t>('x');
    require(!decode_strategy_profile(bytes), "unknown profile schema was accepted");

    auto binding = valid_binding(profile);
    auto binding_bytes = canonical_teacher_policy_binding_bytes(binding);
    binding_bytes.push_back(0);
    require(!decode_teacher_policy_binding(binding_bytes),
            "binding trailing bytes were accepted");
}

}  // namespace

int main() {
    try {
        test_profile_value_rejections();
        test_predicate_field_scope_rejections();
        test_binding_rejections();
        test_decode_rejections();
        std::cout << "strategy_profile_negative_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "strategy_profile_negative_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
