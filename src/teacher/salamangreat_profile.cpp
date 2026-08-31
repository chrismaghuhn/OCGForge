#include "ygo/teacher/salamangreat_profile.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ygo::teacher {
namespace {

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

PredicateRef observation_boolean(const std::string& fact_id, const bool value) {
    PredicateAtom atom;
    atom.kind = PredicateAtomKind::Boolean;
    atom.boolean = value;
    return predicate(PredicateScope::Observation, "observation.fact_boolean_equals",
                     {token_atom(fact_id), atom});
}

PredicateRef observation_u64_at_least(const std::string& fact_id,
                                      const std::uint64_t value) {
    return predicate(PredicateScope::Observation, "observation.fact_u64_at_least",
                     {token_atom(fact_id), u64_atom(value)});
}

PredicateRef observation_u64_equals(const std::string& fact_id,
                                    const std::uint64_t value) {
    return predicate(PredicateScope::Observation, "observation.fact_u64_equals",
                     {token_atom(fact_id), u64_atom(value)});
}

PredicateRef observation_token(const std::string& fact_id, const std::string& value) {
    return predicate(PredicateScope::Observation, "observation.fact_token_equals",
                     {token_atom(fact_id), token_atom(value)});
}

PredicateRef candidate_action(const std::string& value) {
    return predicate(PredicateScope::Candidate, "candidate.action_kind_equals",
                     {token_atom(value)});
}

PredicateRef candidate_phase(const std::uint32_t value) {
    return predicate(PredicateScope::Candidate, "candidate.phase_equals",
                     {u64_atom(value)});
}

PredicateRef candidate_source_role(const std::string& value) {
    return predicate(PredicateScope::Candidate, "candidate.source_role_contains",
                     {token_atom(value)});
}

PredicateRef candidate_source_visibility(const std::string& value) {
    return predicate(PredicateScope::Candidate, "candidate.source_visibility_equals",
                     {token_atom(value)});
}

std::vector<PredicateRef> idle_role_predicates(const std::uint32_t command,
                                               const std::string& role_id) {
    return {candidate_phase(command), candidate_action("idle_command"),
            candidate_source_role(role_id)};
}

CardRoleEntry card_role(const std::uint32_t passcode,
                        std::vector<std::string> roles) {
    return CardRoleEntry{passcode, std::move(roles)};
}

StrategyProfileV1 build_profile() {
    StrategyProfileV1 value;
    value.matchup_id = "ocgforge.matchup.swordsoul_salamangreat.v1";
    value.rules_bundle_id =
        "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
    value.format_id = "TCG_ADVANCED_2026_05_18";
    value.duel_mode = "DUEL_MODE_MR5";
    value.duel_flags = 190464;
    value.own_deck_role = 1;
    value.own_deck_id = "ocgforge.salamangreat.ml_v1";
    value.own_deck_sha256 =
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
    value.opponent_deck_role = 0;
    value.opponent_deck_id = "ocgforge.swordsoul_tenyi.ml_v1";
    value.opponent_deck_sha256 =
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";

    value.card_roles = {
        card_role(1295111, {"role.salamangreat", "role.sanctuary.access"}),
        card_role(2772337, {"role.interaction", "role.payoff.link3",
                            "role.recovery.princess"}),
        card_role(10045474, {"role.hand.interaction", "role.interaction"}),
        card_role(11962031, {"role.salamangreat", "role.starter.of_fire",
                             "role.trigger.of_fire"}),
        card_role(14558127, {"role.hand.interaction", "role.interaction"}),
        card_role(14812471, {"role.payoff.link1", "role.salamangreat"}),
        card_role(14934922, {"role.interaction", "role.salamangreat",
                             "role.trap.rage"}),
        card_role(20618081, {"role.recovery.falco", "role.salamangreat"}),
        card_role(24224830, {"role.interaction"}),
        card_role(26889158, {"role.salamangreat", "role.starter.gazelle",
                             "role.trigger.gazelle"}),
        card_role(31313405, {"role.payoff.link4", "role.salamangreat"}),
        card_role(41463181, {"role.board.breaker", "role.payoff.link3"}),
        card_role(48815792, {"role.board.breaker", "role.bridge.link2"}),
        card_role(51339637, {"role.interaction", "role.salamangreat",
                             "role.trap.roar"}),
        card_role(52155219, {"role.circle.access", "role.salamangreat"}),
        card_role(52277807, {"role.salamangreat", "role.spinny.activation"}),
        card_role(56003780, {"role.recovery.jaguar", "role.salamangreat"}),
        card_role(57134592, {"role.payoff.link4", "role.recovery.raging",
                             "role.salamangreat"}),
        card_role(57160136, {"role.searcher"}),
        card_role(57357130, {"role.recovery.weasel", "role.salamangreat"}),
        card_role(64178424, {"role.extender.will", "role.salamangreat"}),
        card_role(73642296, {"role.hand.interaction", "role.interaction"}),
        card_role(74652966, {"role.extender.code_of_soul", "role.salamangreat"}),
        card_role(83533296, {"role.charge.access", "role.salamangreat"}),
        card_role(87327776, {"role.bridge.rank3", "role.salamangreat"}),
        card_role(87871125, {"role.payoff.link2", "role.recovery.wolf",
                             "role.salamangreat"}),
        card_role(94620082, {"role.extender.foxy", "role.salamangreat"}),
        card_role(97268402, {"role.hand.interaction", "role.interaction"}),
    };

    value.resources = {
        {"resource.chain.window", "public.chain.length", 4294967295U, 20, 10},
    };

    value.candidate_intents = {
        {"intent.board.breaker", idle_role_predicates(1, "role.board.breaker")},
        {"intent.charge.access", idle_role_predicates(5, "role.charge.access")},
        {"intent.circle.access", idle_role_predicates(5, "role.circle.access")},
        {"intent.code.of.soul", idle_role_predicates(5, "role.extender.code_of_soul")},
        {"intent.falco.recovery", idle_role_predicates(5, "role.recovery.falco")},
        {"intent.foxy.extender", idle_role_predicates(5, "role.extender.foxy")},
        {"intent.gazelle.access", idle_role_predicates(0, "role.starter.gazelle")},
        {"intent.gazelle.trigger",
         {candidate_action("chain"), candidate_source_role("role.trigger.gazelle"),
          candidate_source_visibility("visible")} },
        {"intent.interaction.chain",
         {candidate_action("chain"), candidate_source_role("role.interaction"),
          candidate_source_visibility("visible")} },
        {"intent.jaguar.recovery", idle_role_predicates(5, "role.recovery.jaguar")},
        {"intent.link1.payoff", idle_role_predicates(1, "role.payoff.link1")},
        {"intent.link2.payoff", idle_role_predicates(1, "role.payoff.link2")},
        {"intent.link3.payoff", idle_role_predicates(1, "role.payoff.link3")},
        {"intent.link4.payoff", idle_role_predicates(1, "role.payoff.link4")},
        {"intent.miragestallio.bridge", idle_role_predicates(1, "role.bridge.rank3")},
        {"intent.of_fire.starter", idle_role_predicates(0, "role.starter.of_fire")},
        {"intent.of_fire.trigger",
         {candidate_action("chain"), candidate_source_role("role.trigger.of_fire"),
          candidate_source_visibility("visible")} },
        {"intent.princess.recovery.chain",
         {candidate_action("chain"), candidate_source_role("role.recovery.princess"),
          candidate_source_visibility("visible")} },
        {"intent.princess.recovery.ignition",
         idle_role_predicates(5, "role.recovery.princess")},
        {"intent.sanctuary.access", idle_role_predicates(5, "role.sanctuary.access")},
        {"intent.search", idle_role_predicates(5, "role.searcher")},
        {"intent.spinny.activation", idle_role_predicates(5, "role.spinny.activation")},
        {"intent.weasel.conversion",
         {candidate_action("chain"), candidate_source_role("role.recovery.weasel"),
          candidate_source_visibility("visible")} },
        {"intent.weasel.extension", idle_role_predicates(5, "role.recovery.weasel")},
        {"intent.will.extension", idle_role_predicates(5, "role.extender.will")},
        {"intent.wolf.recovery.chain",
         {candidate_action("chain"), candidate_source_role("role.recovery.wolf"),
          candidate_source_visibility("visible")} },
        {"intent.wolf.recovery.ignition", idle_role_predicates(5, "role.recovery.wolf")},
    };

    const auto terminal_false = observation_boolean("public.terminal", false);
    const auto terminal_true = observation_boolean("public.terminal", true);
    const auto idle_context = observation_token("public.decision_context.kind", "idle_command");
    const auto chain_context = observation_token("public.decision_context.kind", "chain");
    const auto main1_phase = observation_u64_equals("public.turn.phase", 0x04);

    const std::vector<PredicateRef> main1_preconditions = {
        main1_phase, idle_context, terminal_false};
    const std::vector<PredicateRef> interaction_preconditions = {
        chain_context, terminal_false};

    value.goals = {
        {"goal.chain.salamangreat", 90, interaction_preconditions, {terminal_true},
         {terminal_true}},
        {"goal.main1.salamangreat", 100, main1_preconditions, {terminal_true},
         {terminal_true}},
    };

    LineNode interaction_node;
    const auto make_chain_node = [&terminal_true](const char* node_id,
                                                   const char* intent_id) {
        LineNode node;
        node.node_id = node_id;
        node.candidate_intent_ids = {intent_id};
        node.stop_predicates = {terminal_true};
        return node;
    };

    LineDefinition interaction;
    interaction.line_id = "line.chain.salamangreat";
    interaction.goal_id = "goal.chain.salamangreat";
    interaction.applicability_predicates = {chain_context};
    interaction.nodes = {
        make_chain_node("node.chain.gazelle_trigger", "intent.gazelle.trigger"),
        make_chain_node("node.chain.interaction", "intent.interaction.chain"),
        make_chain_node("node.chain.of_fire_trigger", "intent.of_fire.trigger"),
        make_chain_node("node.chain.princess_conversion", "intent.princess.recovery.chain"),
        make_chain_node("node.chain.weasel_conversion", "intent.weasel.conversion"),
        make_chain_node("node.chain.wolf_recovery", "intent.wolf.recovery.chain"),
    };
    interaction.recovery_edge_ids = {"recovery.chain.main1"};

    const auto make_main1_node = [&terminal_true](const char* node_id,
                                                   const char* intent_id) {
        LineNode node;
        node.node_id = node_id;
        node.candidate_intent_ids = {intent_id};
        node.stop_predicates = {terminal_true};
        return node;
    };

    LineDefinition main1;
    main1.line_id = "line.main1.salamangreat";
    main1.goal_id = "goal.main1.salamangreat";
    main1.applicability_predicates = {main1_phase, idle_context};
    main1.nodes = {
        make_main1_node("node.main1.board_breaker", "intent.board.breaker"),
        make_main1_node("node.main1.charge", "intent.charge.access"),
        make_main1_node("node.main1.circle", "intent.circle.access"),
        make_main1_node("node.main1.code_of_soul", "intent.code.of.soul"),
        make_main1_node("node.main1.falco", "intent.falco.recovery"),
        make_main1_node("node.main1.foxy", "intent.foxy.extender"),
        make_main1_node("node.main1.gazelle", "intent.gazelle.access"),
        make_main1_node("node.main1.jaguar", "intent.jaguar.recovery"),
        make_main1_node("node.main1.link1_payoff", "intent.link1.payoff"),
        make_main1_node("node.main1.link2_payoff", "intent.link2.payoff"),
        make_main1_node("node.main1.link3_payoff", "intent.link3.payoff"),
        make_main1_node("node.main1.link4_payoff", "intent.link4.payoff"),
        make_main1_node("node.main1.miragestallio", "intent.miragestallio.bridge"),
        make_main1_node("node.main1.of_fire", "intent.of_fire.starter"),
        make_main1_node("node.main1.princess", "intent.princess.recovery.ignition"),
        make_main1_node("node.main1.sanctuary", "intent.sanctuary.access"),
        make_main1_node("node.main1.search", "intent.search"),
        make_main1_node("node.main1.spinny", "intent.spinny.activation"),
        make_main1_node("node.main1.weasel", "intent.weasel.extension"),
        make_main1_node("node.main1.will", "intent.will.extension"),
        make_main1_node("node.main1.wolf", "intent.wolf.recovery.ignition"),
    };
    main1.recovery_edge_ids = {"recovery.main1.chain"};

    value.lines = {interaction, main1};

    RecoveryEdge interaction_recovery;
    interaction_recovery.recovery_edge_id = "recovery.chain.main1";
    interaction_recovery.source_kind = RecoverySourceKind::Line;
    interaction_recovery.source_id = "line.chain.salamangreat";
    interaction_recovery.invalidation_reason_ids = {"public_state_contradiction"};
    interaction_recovery.preconditions = {main1_phase, idle_context};
    interaction_recovery.candidate_intent_ids = {"intent.of_fire.starter"};
    interaction_recovery.target_goal_id = "goal.main1.salamangreat";
    interaction_recovery.target_line_id = "line.main1.salamangreat";
    interaction_recovery.confidence_cap = ConfidenceClass::Low;

    RecoveryEdge main1_recovery;
    main1_recovery.recovery_edge_id = "recovery.main1.chain";
    main1_recovery.source_kind = RecoverySourceKind::Line;
    main1_recovery.source_id = "line.main1.salamangreat";
    main1_recovery.invalidation_reason_ids = {"public_state_contradiction"};
    main1_recovery.preconditions = {chain_context};
    main1_recovery.candidate_intent_ids = {"intent.interaction.chain"};
    main1_recovery.target_goal_id = "goal.chain.salamangreat";
    main1_recovery.target_line_id = "line.chain.salamangreat";
    main1_recovery.confidence_cap = ConfidenceClass::Medium;

    value.recovery_edges = {interaction_recovery, main1_recovery};

    InteractionRule interaction_rule;
    interaction_rule.interaction_id = "interaction.chain.preserve";
    interaction_rule.trigger_predicates = {
        observation_u64_at_least("public.chain.length", 1)};
    interaction_rule.candidate_intent_ids = {"intent.interaction.chain"};
    interaction_rule.timing_priority = 100;
    interaction_rule.preserve_resource_ids = {"resource.chain.window"};
    value.interactions = {interaction_rule};

    value.preferences = {
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.chain.salamangreat", 70},
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.main1.salamangreat", 80},
        {ScoreDimension::ProfilePreference, PreferenceSubjectKind::Global, "global", 1},
    };

    value.profile_id = strategy_profile_id(value);
    return value;
}

}  // namespace

StrategyProfileV1 make_salamangreat_profile() {
    return build_profile();
}

}  // namespace ygo::teacher
