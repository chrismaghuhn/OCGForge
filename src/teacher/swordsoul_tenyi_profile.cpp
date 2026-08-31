#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

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
    value.own_deck_role = 0;
    value.own_deck_id = "ocgforge.swordsoul_tenyi.ml_v1";
    value.own_deck_sha256 =
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
    value.opponent_deck_role = 1;
    value.opponent_deck_id = "ocgforge.salamangreat.ml_v1";
    value.opponent_deck_sha256 =
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";

    value.card_roles = {
        card_role(5041348, {"role.interaction", "role.payoff.level8"}),
        card_role(9464441, {"role.interaction", "role.payoff.level8"}),
        card_role(10045474, {"role.hand.interaction", "role.interaction"}),
        card_role(14558127, {"role.hand.interaction", "role.interaction"}),
        card_role(14821890, {"role.interaction"}),
        card_role(19048328, {"role.payoff.level9"}),
        card_role(20001443, {"role.starter.mo_ye", "role.wyrm"}),
        card_role(23431858, {"role.tenyi.body", "role.wyrm"}),
        card_role(24224830, {"role.interaction"}),
        card_role(24557335, {"role.tenyi.body", "role.wyrm"}),
        card_role(32519092, {"role.non_effect.body", "role.tenyi.monk"}),
        card_role(43202238, {"role.board.breaker"}),
        card_role(47710198, {"role.interaction", "role.payoff.level10"}),
        card_role(51684157, {"role.interaction", "role.searcher"}),
        card_role(55273560, {"role.starter.ecclesia"}),
        card_role(56465981, {"role.searcher"}),
        card_role(56495147, {"role.recovery", "role.starter.taia", "role.wyrm"}),
        card_role(69248256, {"role.interaction", "role.payoff.level8"}),
        card_role(78917791, {"role.recovery", "role.tenyi.body", "role.wyrm"}),
        card_role(83755611, {"role.board.breaker", "role.payoff.level8", "role.recovery"}),
        card_role(87052196, {"role.tenyi.body", "role.wyrm"}),
        card_role(93490856, {"role.starter.longyuan", "role.wyrm"}),
        card_role(93850690, {"role.recovery.summit"}),
        card_role(96633955,
                  {"role.interaction", "role.payoff.level10", "role.recovery"}),
        card_role(97268402, {"role.hand.interaction", "role.interaction"}),
        card_role(98159737, {"role.tenyi.tuner", "role.wyrm"}),
    };

    value.resources = {
        {"resource.chain.window", "public.chain.length", 4294967295U, 20, 10},
    };

    value.candidate_intents = {
        {"intent.board.breaker", idle_role_predicates(1, "role.board.breaker")},
        {"intent.interaction.chain",
         {candidate_action("chain"), candidate_source_role("role.interaction"),
          candidate_source_visibility("visible")} },
        {"intent.level10.payoff", idle_role_predicates(1, "role.payoff.level10")},
        {"intent.level8.payoff", idle_role_predicates(1, "role.payoff.level8")},
        {"intent.longyuan.access", idle_role_predicates(5, "role.starter.longyuan")},
        {"intent.mo_ye.starter", idle_role_predicates(0, "role.starter.mo_ye")},
        {"intent.monk.access", idle_role_predicates(1, "role.tenyi.monk")},
        {"intent.search", idle_role_predicates(5, "role.searcher")},
        {"intent.summit.recovery", idle_role_predicates(5, "role.recovery.summit")},
        {"intent.taia.recovery", idle_role_predicates(0, "role.starter.taia")},
        {"intent.tenyi.body", idle_role_predicates(1, "role.tenyi.body")},
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
        {"goal.interaction.preservation", 90, interaction_preconditions, {terminal_true},
         {terminal_true}},
        {"goal.main1.swordsoul", 100, main1_preconditions, {terminal_true}, {terminal_true}},
    };

    LineNode interaction_node;
    interaction_node.node_id = "node.interaction.chain";
    interaction_node.candidate_intent_ids = {"intent.interaction.chain"};
    interaction_node.stop_predicates = {terminal_true};

    LineDefinition interaction;
    interaction.line_id = "line.interaction.preserve";
    interaction.goal_id = "goal.interaction.preservation";
    interaction.applicability_predicates = {chain_context};
    interaction.required_resources = {{"resource.chain.window", 1}};
    interaction.nodes = {interaction_node};
    interaction.recovery_edge_ids = {"recovery.interaction.main1"};

    const auto make_main1_node = [&terminal_true](const char* node_id,
                                                   const char* intent_id) {
        LineNode node;
        node.node_id = node_id;
        node.candidate_intent_ids = {intent_id};
        node.stop_predicates = {terminal_true};
        return node;
    };

    LineDefinition main1;
    main1.line_id = "line.main1.swordsoul";
    main1.goal_id = "goal.main1.swordsoul";
    main1.applicability_predicates = {main1_phase, idle_context};
    main1.nodes = {
        make_main1_node("node.main1.board_breaker", "intent.board.breaker"),
        make_main1_node("node.main1.level10_payoff", "intent.level10.payoff"),
        make_main1_node("node.main1.level8_payoff", "intent.level8.payoff"),
        make_main1_node("node.main1.longyuan", "intent.longyuan.access"),
        make_main1_node("node.main1.mo_ye", "intent.mo_ye.starter"),
        make_main1_node("node.main1.monk", "intent.monk.access"),
        make_main1_node("node.main1.search", "intent.search"),
        make_main1_node("node.main1.summit", "intent.summit.recovery"),
        make_main1_node("node.main1.taia", "intent.taia.recovery"),
        make_main1_node("node.main1.tenyi", "intent.tenyi.body"),
    };
    main1.recovery_edge_ids = {"recovery.main1.interaction"};

    value.lines = {interaction, main1};

    RecoveryEdge foundation_recovery;
    foundation_recovery.recovery_edge_id = "recovery.main1.interaction";
    foundation_recovery.source_kind = RecoverySourceKind::Line;
    foundation_recovery.source_id = "line.main1.swordsoul";
    foundation_recovery.invalidation_reason_ids = {"public_state_contradiction"};
    foundation_recovery.preconditions = {
        chain_context, observation_u64_at_least("public.chain.length", 1)};
    foundation_recovery.candidate_intent_ids = {"intent.interaction.chain"};
    foundation_recovery.target_goal_id = "goal.interaction.preservation";
    foundation_recovery.target_line_id = "line.interaction.preserve";
    foundation_recovery.preserve_resource_ids = {};
    foundation_recovery.confidence_cap = ConfidenceClass::Medium;

    // No public fact identifies a Taia/Summit body, target, or resource, so
    // this minimal slice does not publish a Taia/Summit recovery edge.
    RecoveryEdge interaction_recovery;
    interaction_recovery.recovery_edge_id = "recovery.interaction.main1";
    interaction_recovery.source_kind = RecoverySourceKind::Line;
    interaction_recovery.source_id = "line.interaction.preserve";
    interaction_recovery.invalidation_reason_ids = {"public_state_contradiction"};
    interaction_recovery.preconditions = {main1_phase, idle_context};
    interaction_recovery.candidate_intent_ids = {"intent.mo_ye.starter"};
    interaction_recovery.target_goal_id = "goal.main1.swordsoul";
    interaction_recovery.target_line_id = "line.main1.swordsoul";
    interaction_recovery.preserve_resource_ids = {};
    interaction_recovery.confidence_cap = ConfidenceClass::Low;

    value.recovery_edges = {
        interaction_recovery,
        foundation_recovery,
    };

    InteractionRule blackout;
    blackout.interaction_id = "interaction.blackout.preserve";
    blackout.trigger_predicates = {observation_u64_at_least("public.chain.length", 1)};
    blackout.candidate_intent_ids = {"intent.interaction.chain"};
    blackout.timing_priority = 100;
    blackout.preserve_resource_ids = {"resource.chain.window"};

    value.interactions = {blackout};

    value.preferences = {
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.interaction.preserve", 70},
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.main1.swordsoul", 80},
        {ScoreDimension::ProfilePreference, PreferenceSubjectKind::Global, "global", 1},
    };

    value.profile_id = strategy_profile_id(value);
    return value;
}

}  // namespace

StrategyProfileV1 make_swordsoul_tenyi_profile() {
    return build_profile();
}

}  // namespace ygo::teacher
