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

PredicateRef candidate_action(const std::string& value) {
    return predicate(PredicateScope::Candidate, "candidate.action_kind_equals",
                     {token_atom(value)});
}

PredicateRef candidate_source_role(const std::string& value) {
    return predicate(PredicateScope::Candidate, "candidate.source_role_contains",
                     {token_atom(value)});
}

PredicateRef candidate_source_visibility(const std::string& value) {
    return predicate(PredicateScope::Candidate, "candidate.source_visibility_equals",
                     {token_atom(value)});
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
        card_role(10045474, {"role.hand.interaction"}),
        card_role(14558127, {"role.hand.interaction"}),
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
        card_role(56495147,
                  {"role.payoff.level8", "role.recovery", "role.starter.taia", "role.wyrm"}),
        card_role(69248256, {"role.interaction", "role.payoff.level8"}),
        card_role(78917791, {"role.recovery", "role.tenyi.body", "role.wyrm"}),
        card_role(83755611, {"role.board.breaker", "role.payoff.level8", "role.recovery"}),
        card_role(87052196, {"role.tenyi.body", "role.wyrm"}),
        card_role(93490856,
                  {"role.payoff.level10", "role.starter.longyuan", "role.wyrm"}),
        card_role(93850690, {"role.recovery.summit"}),
        card_role(96633955,
                  {"role.interaction", "role.payoff.level10", "role.recovery"}),
        card_role(97268402, {"role.hand.interaction"}),
        card_role(98159737, {"role.tenyi.tuner", "role.wyrm"}),
    };

    value.resources = {
        {"resource.chain.window", "public.chain.length", 4294967295U, 20, 10},
    };

    value.candidate_intents = {
        {"intent.board.breaker", {candidate_source_role("role.board.breaker")} },
        {"intent.interaction.chain",
         {candidate_action("chain"), candidate_source_role("role.interaction"),
          candidate_source_visibility("visible")} },
        {"intent.level10.payoff", {candidate_source_role("role.payoff.level10")} },
        {"intent.level8.payoff", {candidate_source_role("role.payoff.level8")} },
        {"intent.longyuan.access", {candidate_source_role("role.starter.longyuan")} },
        {"intent.mo_ye.starter", {candidate_source_role("role.starter.mo_ye")} },
        {"intent.monk.access", {candidate_source_role("role.tenyi.monk")} },
        {"intent.search", {candidate_source_role("role.searcher")} },
        {"intent.summit.recovery", {candidate_source_role("role.recovery.summit")} },
        {"intent.taia.recovery", {candidate_source_role("role.starter.taia")} },
        {"intent.tenyi.body", {candidate_source_role("role.tenyi.body")} },
    };

    const auto terminal_false = observation_boolean("public.terminal", false);
    const auto terminal_true = observation_boolean("public.terminal", true);
    const auto active_actor = observation_boolean("public.current_actor_is_perspective", true);
    const auto main1_phase = observation_u64_equals("public.turn.phase", 0x04);

    // The current public fact registry has no ATK/DEF or damage-proof fact, so
    // this slice deliberately does not publish a lethal-action intent.
    value.goals = {
        {"goal.foundation.chixiao", 100, {terminal_false}, {}, {terminal_true}},
        {"goal.interaction.preservation", 90, {terminal_false}, {}, {terminal_true}},
        {"goal.level10.access", 80, {terminal_false}, {}, {terminal_true}},
        {"goal.taia.summit.recovery", 70, {terminal_false}, {}, {terminal_true}},
        {"goal.tenyi.monk.access", 60, {terminal_false}, {}, {terminal_true}},
    };

    LineNode foundation_chixiao;
    foundation_chixiao.node_id = "node.foundation.chixiao";
    foundation_chixiao.candidate_intent_ids = {"intent.level8.payoff", "intent.mo_ye.starter"};
    foundation_chixiao.stop_predicates = {terminal_true};

    LineNode foundation_interaction;
    foundation_interaction.node_id = "node.foundation.interaction";
    foundation_interaction.candidate_intent_ids = {"intent.interaction.chain"};
    foundation_interaction.stop_predicates = {terminal_true};

    LineDefinition foundation;
    foundation.line_id = "line.foundation.chixiao";
    foundation.goal_id = "goal.foundation.chixiao";
    foundation.applicability_predicates = {main1_phase};
    foundation.nodes = {foundation_chixiao, foundation_interaction};
    foundation.dependencies = {{"node.foundation.chixiao", "node.foundation.interaction"}};
    foundation.recovery_edge_ids = {"recovery.foundation.interaction"};

    LineNode interaction_node;
    interaction_node.node_id = "node.interaction.preserve";
    interaction_node.candidate_intent_ids = {"intent.board.breaker", "intent.interaction.chain"};
    interaction_node.stop_predicates = {terminal_true};

    LineDefinition interaction;
    interaction.line_id = "line.interaction.preserve";
    interaction.goal_id = "goal.interaction.preservation";
    interaction.applicability_predicates = {active_actor};
    interaction.required_resources = {{"resource.chain.window", 1}};
    interaction.nodes = {interaction_node};
    interaction.recovery_edge_ids = {"recovery.interaction.foundation"};

    LineNode level10_access;
    level10_access.node_id = "node.level10.longyuan";
    level10_access.candidate_intent_ids = {"intent.longyuan.access"};
    level10_access.stop_predicates = {terminal_true};

    LineNode level10_payoff;
    level10_payoff.node_id = "node.level10.payoff";
    level10_payoff.candidate_intent_ids = {"intent.level10.payoff"};
    level10_payoff.stop_predicates = {terminal_true};

    LineDefinition level10;
    level10.line_id = "line.level10.longyuan";
    level10.goal_id = "goal.level10.access";
    level10.applicability_predicates = {main1_phase};
    level10.nodes = {level10_access, level10_payoff};
    level10.dependencies = {{"node.level10.longyuan", "node.level10.payoff"}};
    level10.recovery_edge_ids = {"recovery.level10.tenyi"};

    LineNode taia_access;
    taia_access.node_id = "node.taia.access";
    taia_access.candidate_intent_ids = {"intent.taia.recovery"};
    taia_access.stop_predicates = {terminal_true};

    LineNode summit_recovery;
    summit_recovery.node_id = "node.taia.summit";
    summit_recovery.candidate_intent_ids = {"intent.summit.recovery"};
    summit_recovery.stop_predicates = {terminal_true};

    LineDefinition taia;
    taia.line_id = "line.taia.summit";
    taia.goal_id = "goal.taia.summit.recovery";
    taia.applicability_predicates = {main1_phase};
    taia.nodes = {taia_access, summit_recovery};
    taia.dependencies = {{"node.taia.access", "node.taia.summit"}};

    LineNode tenyi_body;
    tenyi_body.node_id = "node.tenyi.body";
    tenyi_body.candidate_intent_ids = {"intent.tenyi.body"};
    tenyi_body.stop_predicates = {terminal_true};

    LineNode monk_access;
    monk_access.node_id = "node.tenyi.monk";
    monk_access.candidate_intent_ids = {"intent.monk.access"};
    monk_access.stop_predicates = {terminal_true};

    LineDefinition tenyi;
    tenyi.line_id = "line.tenyi.monk";
    tenyi.goal_id = "goal.tenyi.monk.access";
    tenyi.applicability_predicates = {main1_phase};
    tenyi.nodes = {tenyi_body, monk_access};
    tenyi.dependencies = {{"node.tenyi.body", "node.tenyi.monk"}};

    value.lines = {foundation, interaction, level10, taia, tenyi};

    RecoveryEdge foundation_recovery;
    foundation_recovery.recovery_edge_id = "recovery.foundation.interaction";
    foundation_recovery.source_kind = RecoverySourceKind::Line;
    foundation_recovery.source_id = "line.foundation.chixiao";
    foundation_recovery.invalidation_reason_ids = {"public_state_contradiction"};
    foundation_recovery.preconditions = {terminal_false};
    foundation_recovery.candidate_intent_ids = {"intent.interaction.chain"};
    foundation_recovery.target_goal_id = "goal.interaction.preservation";
    foundation_recovery.target_line_id = "line.interaction.preserve";
    foundation_recovery.preserve_resource_ids = {};
    foundation_recovery.confidence_cap = ConfidenceClass::Medium;

    RecoveryEdge level10_recovery;
    level10_recovery.recovery_edge_id = "recovery.level10.tenyi";
    level10_recovery.source_kind = RecoverySourceKind::Line;
    level10_recovery.source_id = "line.level10.longyuan";
    level10_recovery.invalidation_reason_ids = {"public_state_contradiction"};
    level10_recovery.preconditions = {terminal_false};
    level10_recovery.candidate_intent_ids = {"intent.tenyi.body"};
    level10_recovery.target_goal_id = "goal.tenyi.monk.access";
    level10_recovery.target_line_id = "line.tenyi.monk";
    level10_recovery.preserve_resource_ids = {};
    level10_recovery.confidence_cap = ConfidenceClass::Low;

    // No public fact identifies a Taia/Summit body, target, or resource, so
    // this minimal slice does not publish a Taia/Summit recovery edge.
    RecoveryEdge interaction_recovery;
    interaction_recovery.recovery_edge_id = "recovery.interaction.foundation";
    interaction_recovery.source_kind = RecoverySourceKind::Line;
    interaction_recovery.source_id = "line.interaction.preserve";
    interaction_recovery.invalidation_reason_ids = {"public_state_contradiction"};
    interaction_recovery.preconditions = {terminal_false};
    interaction_recovery.candidate_intent_ids = {"intent.mo_ye.starter"};
    interaction_recovery.target_goal_id = "goal.foundation.chixiao";
    interaction_recovery.target_line_id = "line.foundation.chixiao";
    interaction_recovery.preserve_resource_ids = {};
    interaction_recovery.confidence_cap = ConfidenceClass::Low;

    value.recovery_edges = {
        foundation_recovery,
        interaction_recovery,
        level10_recovery,
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
         PreferenceSubjectKind::Line, "line.foundation.chixiao", 80},
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.interaction.preserve", 70},
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.level10.longyuan", 60},
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.taia.summit", 50},
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.tenyi.monk", 40},
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
