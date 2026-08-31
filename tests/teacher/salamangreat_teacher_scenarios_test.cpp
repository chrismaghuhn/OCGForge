#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/candidate_evaluator.hpp"
#include "ygo/teacher/fallback_resolver.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/teacher_explanation_codec.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation public_observation(
    const std::uint64_t decision_index,
    const std::uint32_t phase,
    const std::uint32_t chain_length,
    const std::string& decision_kind,
    const std::optional<std::uint32_t>& visible_passcode = std::nullopt,
    const ygo::observation::SemanticZone visible_zone =
        ygo::observation::SemanticZone::Hand,
    const PublicCardReferenceKind visible_reference_kind =
        PublicCardReferenceKind::VisibleCard,
    const std::string& private_world_marker = {}) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_player = 0;
    source.globals.turn_count = 1;
    source.globals.phase = phase;
    source.globals.chain_length = chain_length;
    source.globals.terminal = false;
    source.match_context.perspective_player = 0;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = decision_kind;
    source.decision_context.player = 0;
    if (!private_world_marker.empty()) {
        // The source marker stands for hidden/control-plane world variation;
        // project_public_observation deliberately excludes these fields.
        source.decision_context.decision_id = private_world_marker;
        source.decision_context.continuation_id = private_world_marker;
        source.engine_step_index = private_world_marker == "world-a" ? 101 : 202;
    }

    if (visible_passcode.has_value()) {
        ygo::observation::ObservedCard entity;
        entity.locator = {std::string("p0:") +
                          ygo::observation::semantic_zone_name(visible_zone) + ":0"};
        entity.identity_known = visible_reference_kind == PublicCardReferenceKind::VisibleCard;
        if (entity.identity_known) {
            entity.passcode = *visible_passcode;
        }
        entity.owner = 0;
        entity.controller = 0;
        entity.zone = visible_zone;
        entity.sequence = 0;
        entity.face_up = false;
        entity.face_down = !entity.identity_known;
        source.entities.push_back(entity);
    }
    if (!private_world_marker.empty()) {
        ygo::observation::ObservedCard hidden_opponent_entity;
        hidden_opponent_entity.locator = {"p1:HAND:0"};
        hidden_opponent_entity.owner = 1;
        hidden_opponent_entity.controller = 1;
        hidden_opponent_entity.zone = ygo::observation::SemanticZone::Hand;
        hidden_opponent_entity.sequence = 0;
        hidden_opponent_entity.face_down = true;
        source.entities.push_back(hidden_opponent_entity);
    }
    return project_public_observation(source);
}

PublicFactSnapshot public_facts(const PublicEnvironmentObservation& observation) {
    const auto extracted = extract_public_fact_snapshot(observation);
    require(extracted.valid, "Salamangreat public fact extraction failed");
    return extracted.snapshot;
}

PublicCardReference card_reference(
    const ygo::observation::SemanticZone zone = ygo::observation::SemanticZone::Hand,
    const PublicCardReferenceKind kind = PublicCardReferenceKind::VisibleCard) {
    return {kind, std::string("p0:") + ygo::observation::semantic_zone_name(zone) + ":0"};
}

EnvironmentActionCandidate idle_source(const std::uint32_t command,
                                        const PublicCardReference& source) {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::IdleCommand;
    value.choice = PublicChoice{PublicChoiceKind::EffectChoice, 0, std::nullopt};
    value.source_reference = source;
    value.phase = command;
    PublicActionKeyInput key;
    key.action_kind = "idle_command";
    key.choice = value.choice;
    key.source_reference = value.source_reference;
    key.phase = value.phase;
    value.public_action_key = public_action_key(key);
    return value;
}

EnvironmentActionCandidate idle_card(
    const std::uint32_t command,
    const ygo::observation::SemanticZone zone = ygo::observation::SemanticZone::Hand) {
    return idle_source(command, card_reference(zone));
}

EnvironmentActionCandidate chain_source(const PublicCardReference& source) {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::Chain;
    value.choice = PublicChoice{PublicChoiceKind::EffectChoice, 0, std::nullopt};
    value.source_reference = source;
    PublicActionKeyInput key;
    key.action_kind = "chain";
    key.choice = value.choice;
    key.source_reference = value.source_reference;
    value.public_action_key = public_action_key(key);
    return value;
}

EnvironmentActionCandidate chain_pass() {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::Chain;
    value.phase = 1;
    PublicActionKeyInput key;
    key.action_kind = "chain";
    key.phase = value.phase;
    value.public_action_key = public_action_key(key);
    return value;
}

EnvironmentActionCandidate continuation_finish() {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::Finish;
    value.continuation_operation = "finish";
    PublicActionKeyInput key;
    key.action_kind = "finish";
    key.continuation_operation = value.continuation_operation;
    value.public_action_key = public_action_key(key);
    return value;
}

PublicFactValue u64_fact(const std::string& fact_id, const std::uint64_t value) {
    PublicFactValue fact;
    fact.fact_id = fact_id;
    fact.value_kind = PublicFactValueKind::U64;
    fact.u64_value = value;
    return fact;
}

EpisodeLocalStrategyStateV1 reset_state(const StrategyProfileV1& profile) {
    const auto reset = reset_strategy_state(profile);
    require(reset.has_value(), "Salamangreat profile did not reset strategy state");
    return *reset;
}

void require_progress(const StrategyProfileV1& profile,
                      const GoalLineSelection& selection,
                      const EnvironmentActionCandidate& candidate,
                      const PublicEnvironmentObservation& observation,
                      const std::int32_t expected) {
    const auto result = evaluate_goal_line_progress(
        profile, selection, RecoverySelection{}, candidate, observation, 0);
    require(result.status == CandidateEvaluationStatus::Supported &&
                result.contributions.size() == 1 &&
                result.contributions[0].dimension ==
                    ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress &&
                result.contributions[0].value == expected,
            "Salamangreat candidate progress did not match the declared profile intent");
}

void test_main1_and_action_specific_intents() {
    const auto profile = make_salamangreat_profile();
    const auto main_observation = public_observation(10, 0x04, 0, "idle_command");
    const auto selection = select_goal_and_line(
        profile, reset_state(profile), public_facts(main_observation));
    require(selection.status == PredicateEvaluationStatus::True &&
                selection.goal_id == std::optional<std::string>("goal.main1.salamangreat") &&
                selection.line_id == std::optional<std::string>("line.main1.salamangreat") &&
                selection.ready_node_ids.size() >= 8,
            "Main1 Salamangreat line was not selected with independent ready nodes");

    for (const auto phase : {0x02U, 0x80U, 0x200U}) {
        const auto non_main = select_goal_and_line(
            profile, reset_state(profile), public_facts(public_observation(
                                                10, phase, 0, "idle_command")));
        require(non_main.status == PredicateEvaluationStatus::False &&
                    !non_main.goal_id.has_value() && !non_main.line_id.has_value(),
                "non-Main1 public phase activated the Main1 Salamangreat line");
    }

    std::vector<std::string> matched;
    const auto of_fire = idle_card(0);
    const auto of_fire_observation =
        public_observation(10, 0x04, 0, "idle_command", 11962031);
    require(match_candidate_intent_set(
                profile, {"intent.of_fire.starter"}, of_fire, of_fire_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Of Fire normal summon did not match its starter intent");
    require_progress(profile, selection, of_fire, of_fire_observation, 3);

    const auto gazelle = idle_card(0);
    const auto gazelle_observation =
        public_observation(10, 0x04, 0, "idle_command", 26889158);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.gazelle.access"}, gazelle, gazelle_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Gazelle normal summon did not match its access intent");
    require_progress(profile, selection, gazelle, gazelle_observation, 3);

    const auto of_fire_wrong_command = idle_card(5);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.of_fire.starter"},
                                       of_fire_wrong_command, of_fire_observation, 0,
                                       matched) == PredicateEvaluationStatus::False,
            "Of Fire activation-shaped candidate matched normal-summon intent");

    const auto spinny = idle_card(5);
    const auto spinny_observation =
        public_observation(10, 0x04, 0, "idle_command", 52277807);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.spinny.extender"}, spinny, spinny_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Spinny activation did not match its extender intent");

    const auto balelynx = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto balelynx_observation =
        public_observation(10, 0x04, 0, "idle_command", 14812471,
                           ygo::observation::SemanticZone::ExtraDeck);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.link1.payoff"}, balelynx, balelynx_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Balelynx Extra Deck summon did not match Link-1 payoff");
    require_progress(profile, selection, balelynx, balelynx_observation, 3);

    const auto raging = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto raging_observation =
        public_observation(10, 0x04, 0, "idle_command", 57134592,
                           ygo::observation::SemanticZone::ExtraDeck);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.link4.payoff"}, raging, raging_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Raging Phoenix Extra Deck summon did not match Link-4 payoff");
    require_progress(profile, selection, raging, raging_observation, 3);

    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.link4.payoff"}, of_fire, of_fire_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Of Fire was incorrectly classified as a Link-4 payoff");

    const auto miragestallio = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto miragestallio_observation =
        public_observation(10, 0x04, 0, "idle_command", 87327776,
                           ygo::observation::SemanticZone::ExtraDeck);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.link3.payoff"}, miragestallio, miragestallio_observation, 0,
                matched) == PredicateEvaluationStatus::False,
            "Miragestallio bridge was incorrectly classified as Link-3 payoff");
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.miragestallio.bridge"},
                                       miragestallio, miragestallio_observation, 0,
                                       matched) == PredicateEvaluationStatus::True,
            "Miragestallio bridge did not match its public bridge intent");
    require_progress(profile, selection, miragestallio, miragestallio_observation, 3);

    const auto code_of_soul_phase_one = idle_card(1);
    const auto code_of_soul_observation =
        public_observation(10, 0x04, 0, "idle_command", 74652966);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.code.of.soul"},
                                       code_of_soul_phase_one, code_of_soul_observation, 0,
                                       matched) == PredicateEvaluationStatus::False,
            "Code of Soul phase-1 procedure matched its Ignition intent");
    const auto code_of_soul = idle_card(5);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.code.of.soul"}, code_of_soul,
                                       code_of_soul_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Code of Soul phase-5 activation did not match its access intent");

    const auto circle = idle_card(5);
    const auto circle_observation =
        public_observation(10, 0x04, 0, "idle_command", 52155219);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.circle.access"}, circle,
                                       circle_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Circle did not match its dedicated access intent");
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.search"}, circle,
                                       circle_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Circle was incorrectly classified as generic search");

    const auto sanctuary = idle_card(5);
    const auto sanctuary_observation =
        public_observation(10, 0x04, 0, "idle_command", 1295111);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.sanctuary.access"}, sanctuary,
                                       sanctuary_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Sanctuary did not match its dedicated access intent");
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.search"}, sanctuary,
                                       sanctuary_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Sanctuary was incorrectly classified as a searcher");

    const auto mining = idle_card(5);
    const auto mining_observation =
        public_observation(10, 0x04, 0, "idle_command", 57160136);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.search"}, mining,
                                       mining_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Cynet Mining did not match the generic search intent");

    const auto will = idle_card(5);
    const auto will_observation = public_observation(10, 0x04, 0, "idle_command", 64178424);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.will.extension"}, will,
                                       will_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Will did not match its public extension intent");

    const auto wolf = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto wolf_observation =
        public_observation(10, 0x04, 0, "idle_command", 87871125,
                           ygo::observation::SemanticZone::ExtraDeck);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.link2.payoff"}, wolf,
                                       wolf_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Sunlight Wolf Link summon did not match Link-2 payoff");
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.wolf.recovery.ignition"}, wolf,
                                       wolf_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Sunlight Wolf Link summon matched ignition recovery");
    const auto wolf_ignition = idle_card(5, ygo::observation::SemanticZone::Graveyard);
    const auto wolf_recovery_observation =
        public_observation(10, 0x04, 0, "idle_command", 87871125,
                           ygo::observation::SemanticZone::Graveyard);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.wolf.recovery.ignition"},
                                       wolf_ignition, wolf_recovery_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Sunlight Wolf phase-5 recovery did not match ignition intent");

    const auto princess = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto princess_observation =
        public_observation(10, 0x04, 0, "idle_command", 2772337,
                           ygo::observation::SemanticZone::ExtraDeck);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.link3.payoff"}, princess,
                                       princess_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Princess Link summon did not match Link-3 payoff");
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.princess.recovery.ignition"},
                                       princess, princess_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Princess Link summon matched ignition recovery");
    const auto princess_ignition = idle_card(5, ygo::observation::SemanticZone::Graveyard);
    const auto princess_recovery_observation =
        public_observation(10, 0x04, 0, "idle_command", 2772337,
                           ygo::observation::SemanticZone::Graveyard);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.princess.recovery.ignition"},
                                       princess_ignition, princess_recovery_observation, 0,
                                       matched) ==
                PredicateEvaluationStatus::True,
            "Princess phase-5 recovery did not match ignition intent");

    const auto weasel_phase_one = idle_card(1);
    const auto weasel_observation =
        public_observation(10, 0x04, 0, "idle_command", 57357130);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.weasel.extension"},
                                       weasel_phase_one, weasel_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Weasel phase-1 procedure matched its Ignition extension");
    const auto weasel_extension = idle_card(5);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.weasel.extension"},
                                       weasel_extension, weasel_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Weasel phase-5 activation did not match extension intent");

    std::vector<std::string> all_intents;
    for (const auto& intent : profile.candidate_intents) {
        all_intents.push_back(intent.intent_id);
    }
    const auto finish = continuation_finish();
    matched.clear();
    require(match_candidate_intent_set(profile, all_intents, finish, main_observation, 0,
                                       matched) == PredicateEvaluationStatus::False &&
                matched.empty(),
            "continuation Finish acquired a strategic Salamangreat intent");
}

void test_interaction_recovery_and_domain_preservation() {
    const auto profile = make_salamangreat_profile();
    const auto chain_observation = public_observation(20, 0x04, 1, "chain", 14558127);
    const auto chain_selection = select_goal_and_line(
        profile, reset_state(profile), public_facts(chain_observation));
    require(chain_selection.goal_id ==
                std::optional<std::string>("goal.chain.salamangreat") &&
                chain_selection.line_id == std::optional<std::string>("line.chain.salamangreat") &&
                chain_selection.ready_node_ids ==
                    std::vector<std::string>{
                        "node.chain.gazelle_trigger", "node.chain.interaction",
                        "node.chain.of_fire_trigger", "node.chain.princess_conversion",
                        "node.chain.weasel_conversion", "node.chain.wolf_recovery"},
            "Salamangreat chain strategy line was not selected from public chain context");

    const auto chain_construction = public_observation(20, 0x04, 0, "chain", 14558127);
    const auto chain_construction_selection = select_goal_and_line(
        profile, reset_state(profile), public_facts(chain_construction));
    require(chain_construction_selection.status == PredicateEvaluationStatus::True &&
                chain_construction_selection.line_id ==
                    std::optional<std::string>("line.chain.salamangreat"),
            "chain construction with no existing chain length was incorrectly blocked");

    std::vector<std::string> matched;
    const auto trigger_cases = std::vector<std::pair<std::uint32_t, const char*>>{
        {26889158U, "intent.gazelle.trigger"},
        {11962031U, "intent.of_fire.trigger"},
        {87871125U, "intent.wolf.recovery.chain"},
        {2772337U, "intent.princess.recovery.chain"},
        {57357130U, "intent.weasel.conversion"},
    };
    for (const auto& [passcode, intent_id] : trigger_cases) {
        const auto trigger = chain_source(card_reference());
        const auto trigger_observation = public_observation(20, 0x04, 1, "chain", passcode);
        matched.clear();
        require(match_candidate_intent_set(profile, {intent_id}, trigger,
                                           trigger_observation, 0, matched) ==
                    PredicateEvaluationStatus::True,
                std::string("public Chain trigger did not match ") + intent_id);
        require_progress(profile, chain_selection, trigger, trigger_observation, 3);
    }
    const auto wolf_chain = chain_source(card_reference());
    const auto wolf_chain_observation = public_observation(20, 0x04, 1, "chain", 87871125);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.wolf.recovery.ignition"}, wolf_chain,
                                       wolf_chain_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Sunlight Wolf Chain trigger matched its Ignition-only intent");
    const auto princess_chain = chain_source(card_reference());
    const auto princess_chain_observation =
        public_observation(20, 0x04, 1, "chain", 2772337);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.princess.recovery.ignition"},
                                       princess_chain, princess_chain_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Princess Chain trigger matched its Ignition-only intent");

    const auto ash_chain = chain_source(card_reference());
    require(match_candidate_intent_set(
                profile, {"intent.interaction.chain"}, ash_chain, chain_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Ash chain activation did not match interaction intent");
    require_progress(profile, chain_selection, ash_chain, chain_observation, 3);

    const auto chain_pass_candidate = chain_pass();
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.interaction.chain"},
                                       chain_pass_candidate, chain_observation, 0,
                                       matched) == PredicateEvaluationStatus::False,
            "chain.pass matched interaction intent");
    require_progress(profile, chain_selection, chain_pass_candidate, chain_observation, 0);

    const auto redacted_chain = chain_source(
        card_reference(ygo::observation::SemanticZone::Hand,
                       PublicCardReferenceKind::RedactedSlot));
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.interaction.chain"}, redacted_chain,
                                       chain_observation, 0, matched) ==
                PredicateEvaluationStatus::Unsupported,
            "redacted interaction source was not unsupported");

    for (const auto passcode : {14558127U, 73642296U, 97268402U, 10045474U,
                                24224830U}) {
        const auto hand_interaction = chain_source(card_reference());
        const auto interaction_observation =
            public_observation(20, 0x04, 1, "chain", passcode);
        matched.clear();
        require(match_candidate_intent_set(profile, {"intent.interaction.chain"},
                                           hand_interaction, interaction_observation, 0,
                                           matched) == PredicateEvaluationStatus::True,
                "locked hand interaction did not match interaction intent");
    }
    for (const auto passcode : {51339637U, 14934922U}) {
        const auto trap_interaction = chain_source(
            card_reference(ygo::observation::SemanticZone::SpellTrapZone));
        const auto interaction_observation = public_observation(
            20, 0x04, 1, "chain", passcode,
            ygo::observation::SemanticZone::SpellTrapZone);
        matched.clear();
        require(match_candidate_intent_set(profile, {"intent.interaction.chain"},
                                           trap_interaction, interaction_observation, 0,
                                           matched) == PredicateEvaluationStatus::True,
                "locked Salamangreat trap interaction did not match interaction intent");
    }

    auto interaction_state = reset_state(profile);
    interaction_state.active_goal_id = "goal.chain.salamangreat";
    interaction_state.active_line_id = "line.chain.salamangreat";
    interaction_state.public_resource_facts = {u64_fact("public.chain.length", 1)};
    const auto main_observation =
        public_observation(21, 0x04, 0, "idle_command", 11962031);
    const auto recovery = select_recovery_edge(profile, interaction_state, main_observation, 0);
    require(recovery.status == PredicateEvaluationStatus::True &&
                recovery.recovery_edge_id ==
                    std::optional<std::string>("recovery.chain.main1") &&
                recovery.target_line_id ==
                    std::optional<std::string>("line.main1.salamangreat"),
            "public interaction-window contradiction did not select Main1 recovery");

    auto lp_only_state = interaction_state;
    const auto unchanged_chain = public_observation(21, 0x04, 1, "chain", 14558127);
    const auto no_recovery =
        select_recovery_edge(profile, lp_only_state, unchanged_chain, 0);
    require(no_recovery.status == PredicateEvaluationStatus::False &&
                !no_recovery.recovery_edge_id.has_value(),
            "unchanged public interaction fact produced recovery");

    const auto wrong_participant =
        select_recovery_edge(profile, interaction_state, main_observation, 1);
    require(wrong_participant.status == PredicateEvaluationStatus::Invalid,
            "cross-participant recovery observation was accepted");

    auto main_state = reset_state(profile);
    main_state.active_goal_id = "goal.main1.salamangreat";
    main_state.active_line_id = "line.main1.salamangreat";
    main_state.public_resource_facts = {u64_fact("public.chain.length", 0)};
    const auto chain_opening = public_observation(22, 0x04, 1, "chain", 14558127);
    const auto chain_recovery =
        select_recovery_edge(profile, main_state, chain_opening, 0);
    require(chain_recovery.status == PredicateEvaluationStatus::True &&
                chain_recovery.recovery_edge_id ==
                    std::optional<std::string>("recovery.main1.chain") &&
                chain_recovery.target_line_id ==
                    std::optional<std::string>("line.chain.salamangreat"),
            "public chain opening did not select the Salamangreat chain strategy");

    const std::vector<EnvironmentActionCandidate> supplied = {ash_chain, chain_pass_candidate};
    std::vector<std::string> keys;
    std::vector<std::vector<std::string>> evidence;
    for (const auto& candidate : supplied) {
        keys.push_back(candidate.public_action_key);
        std::vector<std::string> candidate_matches;
        (void)match_candidate_intent_set(profile, {"intent.interaction.chain"}, candidate,
                                         chain_observation, 0, candidate_matches);
        evidence.push_back(std::move(candidate_matches));
    }
    require(keys.size() == supplied.size() && evidence.size() == supplied.size() &&
                keys[0] == supplied[0].public_action_key &&
                keys[1] == supplied[1].public_action_key,
            "profile intent evaluation changed the complete supplied candidate domain");
}

struct PublicTeacherOutputs final {
    TeacherRankingResult ranking;
    std::vector<std::uint8_t> explanation_bytes;
};

bool same_candidate_evaluation(const CandidateEvaluation& left,
                               const CandidateEvaluation& right) {
    return left.public_action_key == right.public_action_key &&
           left.status == right.status && left.score == right.score &&
           left.matched_intent_ids == right.matched_intent_ids &&
           left.matched_goal_ids == right.matched_goal_ids &&
           left.matched_line_ids == right.matched_line_ids &&
           left.reason_ids == right.reason_ids;
}

bool same_ranking_result(const TeacherRankingResult& left,
                         const TeacherRankingResult& right) {
    if (left.status != right.status ||
        left.selected_public_action_key != right.selected_public_action_key ||
        left.selected_score_vector != right.selected_score_vector ||
        left.fallback_level != right.fallback_level || left.explanation != right.explanation ||
        left.proposed_state_delta != right.proposed_state_delta ||
        left.evaluations.size() != right.evaluations.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.evaluations.size(); ++index) {
        if (!same_candidate_evaluation(left.evaluations[index], right.evaluations[index])) {
            return false;
        }
    }
    return true;
}

PublicTeacherOutputs compose_public_teacher_outputs(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& state,
    const PublicEnvironmentObservation& observation,
    const std::vector<EnvironmentActionCandidate>& candidates) {
    const auto facts = public_facts(observation);
    const auto selection = select_goal_and_line(profile, state, facts);
    require(selection.status == PredicateEvaluationStatus::True,
            "paired-world Main1 selection was not publicly supported");

    std::vector<TeacherFallbackCandidateValue> stage;
    stage.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const auto outcome = evaluate_goal_line_progress(
            profile, selection, RecoverySelection{}, candidate, observation, 0);
        require(outcome.status == CandidateEvaluationStatus::Supported,
                "paired-world candidate did not have a supported public outcome");

        TeacherFallbackCandidateValue value;
        value.public_action_key = candidate.public_action_key;
        value.status = outcome.status;
        value.score = ScoreVector{};
        value.contributions = outcome.contributions;
        for (const auto& contribution : value.contributions) {
            require(add_score_contribution(*value.score, contribution.dimension,
                                           contribution.value),
                    "paired-world score contribution could not be composed");
        }
        value.matched_intent_ids = outcome.matched_intent_ids;
        value.matched_goal_ids = outcome.matched_goal_ids;
        value.matched_line_ids = outcome.matched_line_ids;
        value.reason_ids = outcome.reason_ids;
        stage.push_back(std::move(value));
    }

    TeacherFallbackStageSet stages;
    stages.stage_evaluations[0] = std::move(stage);
    PublicTeacherOutputs output;
    output.ranking = resolve_teacher_fallback(candidates, stages);
    require(output.ranking.status == TeacherRankingStatus::Selected &&
                output.ranking.selected_public_action_key.has_value() &&
                output.ranking.explanation.has_value(),
            "paired-world fallback composition did not select a public action with explanation");

    TeacherStateDeltaV1 requested;
    requested.strategy_profile_id = profile.profile_id;
    requested.proposed_for_public_action_key = *output.ranking.selected_public_action_key;
    requested.active_goal_id = selection.goal_id;
    requested.active_line_id = selection.line_id;
    requested.public_resource_facts = {u64_fact("public.chain.length", 0)};
    const auto delta = propose_teacher_state_delta(
        state, observation, 0, profile, requested);
    require(delta.has_value(), "paired-world state proposal was not accepted");
    output.ranking.proposed_state_delta = *delta;
    std::string diagnostic;
    require(validate_teacher_ranking_result(output.ranking, &diagnostic),
            "paired-world ranking plus state delta is invalid: " + diagnostic);
    output.explanation_bytes = canonical_teacher_decision_explanation_bytes(
        *output.ranking.explanation);
    return output;
}

void test_equal_public_worlds_are_identical() {
    const auto profile = make_salamangreat_profile();
    const auto world_a =
        public_observation(50, 0x04, 0, "idle_command", 11962031,
                           ygo::observation::SemanticZone::Hand,
                           PublicCardReferenceKind::VisibleCard, "world-a");
    const auto world_b =
        public_observation(50, 0x04, 0, "idle_command", 11962031,
                           ygo::observation::SemanticZone::Hand,
                           PublicCardReferenceKind::VisibleCard, "world-b");
    require(canonical_public_environment_observation_bytes(world_a) ==
                canonical_public_environment_observation_bytes(world_b),
            "paired worlds did not produce identical public observation bytes");

    const auto candidates = std::vector<EnvironmentActionCandidate>{idle_card(0)};
    const auto state_a = reset_state(profile);
    const auto state_b = reset_state(profile);
    require(state_a == state_b, "paired-world initial states differ");
    const auto output_a = compose_public_teacher_outputs(profile, state_a, world_a, candidates);
    const auto output_b = compose_public_teacher_outputs(profile, state_b, world_b, candidates);
    require(same_ranking_result(output_a.ranking, output_b.ranking),
            "equal public worlds produced different ranking evidence or state delta");
    require(output_a.explanation_bytes == output_b.explanation_bytes,
            "equal public worlds produced different explanation bytes");
    require(output_a.ranking.explanation->invalidation_reason_ids.empty() &&
                output_b.ranking.explanation->invalidation_reason_ids.empty(),
            "paired-world explanation carried unexpected invalidation evidence");
}

}  // namespace

int main() {
    try {
        test_main1_and_action_specific_intents();
        test_interaction_recovery_and_domain_preservation();
        test_equal_public_worlds_are_identical();
        std::cout << "salamangreat_teacher_scenarios_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "salamangreat_teacher_scenarios_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
