#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"

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
        PublicCardReferenceKind::VisibleCard) {
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

    for (const auto passcode : {1295111U, 52155219U, 57160136U}) {
        const auto searcher = idle_card(5);
        const auto search_observation =
            public_observation(10, 0x04, 0, "idle_command", passcode);
        matched.clear();
        require(match_candidate_intent_set(profile, {"intent.search"}, searcher,
                                           search_observation, 0, matched) ==
                    PredicateEvaluationStatus::True,
                "locked searcher did not match its public search intent");
    }
    const auto will = idle_card(5);
    const auto will_observation = public_observation(10, 0x04, 0, "idle_command", 64178424);
    matched.clear();
    require(match_candidate_intent_set(profile, {"intent.will.extension"}, will,
                                       will_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Will did not match its public extension intent");

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
                std::optional<std::string>("goal.interaction.preservation") &&
                chain_selection.line_id == std::optional<std::string>("line.interaction.preserve"),
            "interaction line was not selected from public chain context");

    const auto ash_chain = chain_source(card_reference());
    std::vector<std::string> matched;
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
    interaction_state.active_goal_id = "goal.interaction.preservation";
    interaction_state.active_line_id = "line.interaction.preserve";
    interaction_state.public_resource_facts = {u64_fact("public.chain.length", 1)};
    const auto main_observation =
        public_observation(21, 0x04, 0, "idle_command", 11962031);
    const auto recovery = select_recovery_edge(profile, interaction_state, main_observation, 0);
    require(recovery.status == PredicateEvaluationStatus::True &&
                recovery.recovery_edge_id ==
                    std::optional<std::string>("recovery.interaction.main1") &&
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

}  // namespace

int main() {
    try {
        test_main1_and_action_specific_intents();
        test_interaction_recovery_and_domain_preservation();
        std::cout << "salamangreat_teacher_scenarios_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "salamangreat_teacher_scenarios_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
