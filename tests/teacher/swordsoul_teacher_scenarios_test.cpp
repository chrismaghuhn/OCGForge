#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

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
    const std::uint32_t self_life_points,
    const std::uint32_t opponent_life_points,
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
    source.globals.life_points = {self_life_points, opponent_life_points};
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
    require(extracted.valid, "Swordsoul scenario public fact extraction failed");
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
    // The decoder's chain.pass has no source or choice and carries phase 1.
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
    require(reset.has_value(), "Swordsoul profile did not reset strategy state");
    return *reset;
}

void require_idle_shape(const EnvironmentActionCandidate& value,
                        const std::uint32_t command,
                        const std::string& locator) {
    require(value.action_kind == EnvironmentActionKind::IdleCommand &&
                value.phase == std::optional<std::uint32_t>(command) && value.choice.has_value() &&
                value.choice->kind == PublicChoiceKind::EffectChoice &&
                value.choice->value == 0 && !value.choice->response_index.has_value() &&
                value.source_reference.has_value() &&
                value.source_reference->kind == PublicCardReferenceKind::VisibleCard &&
                value.source_reference->observation_locator == locator &&
                !value.source_index.has_value(),
            "IdleCommand fixture is not a decoder-shaped source-card action");
}

void require_chain_shape(const EnvironmentActionCandidate& value,
                         const bool source_bearing) {
    require(value.action_kind == EnvironmentActionKind::Chain,
            "Chain fixture has the wrong action kind");
    if (source_bearing) {
        require(value.choice.has_value() && value.choice->kind == PublicChoiceKind::EffectChoice &&
                    value.choice->value == 0 && !value.phase.has_value() &&
                    value.source_reference.has_value(),
                "source-bearing Chain fixture is not decoder-shaped");
    } else {
        require(!value.choice.has_value() && value.phase == std::optional<std::uint32_t>(1) &&
                    !value.source_reference.has_value(),
                "chain.pass fixture is not decoder-shaped");
    }
}

void require_progress(const StrategyProfileV1& profile,
                      const GoalLineSelection& selection,
                      const EnvironmentActionCandidate& candidate,
                      const PublicEnvironmentObservation& observation,
                      const std::int32_t expected) {
    RecoverySelection no_recovery;
    const auto result = evaluate_goal_line_progress(
        profile, selection, no_recovery, candidate, observation, 0);
    require(result.status == CandidateEvaluationStatus::Supported &&
                result.contributions.size() == 1 &&
                result.contributions[0].dimension ==
                    ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress &&
                result.contributions[0].value == expected,
            "Swordsoul candidate progress did not match the declared profile intent");
}

void test_frame_controller_and_independent_nodes() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto main_observation =
        public_observation(12, 8000, 7000, 0x04, 0, "idle_command");
    const auto selection =
        select_goal_and_line(profile, reset_state(profile), public_facts(main_observation));
    require(selection.status == PredicateEvaluationStatus::True &&
                selection.goal_id == std::optional<std::string>("goal.main1.swordsoul") &&
                selection.line_id == std::optional<std::string>("line.main1.swordsoul") &&
                selection.ready_node_ids ==
                    std::vector<std::string>{
                        "node.main1.board_breaker", "node.main1.level10_payoff",
                        "node.main1.level8_payoff", "node.main1.longyuan",
                        "node.main1.mo_ye", "node.main1.monk", "node.main1.search",
                        "node.main1.summit", "node.main1.taia", "node.main1.tenyi"},
            "Main1 controller did not select the independent Swordsoul nodes");

    for (const auto& line : profile.lines) {
        require(line.dependencies.empty(),
                "minimal Swordsoul profile retained an unproven sequential dependency");
    }

    const auto chain_observation =
        public_observation(13, 8000, 7000, 0x04, 1, "chain", 51684157);
    const auto chain_selection =
        select_goal_and_line(profile, reset_state(profile), public_facts(chain_observation));
    require(chain_selection.status == PredicateEvaluationStatus::True &&
                chain_selection.goal_id ==
                    std::optional<std::string>("goal.interaction.preservation") &&
                chain_selection.line_id ==
                    std::optional<std::string>("line.interaction.preserve"),
            "interaction goal was not restricted to public chain context");

    const std::vector<std::pair<std::uint32_t, std::string>> non_main_phases = {
        {0x02, "STANDBY"}, {0x80, "BATTLE"}, {0x200, "END"}};
    for (const auto& phase : non_main_phases) {
        auto state = reset_state(profile);
        state.active_goal_id = "goal.main1.swordsoul";
        const auto non_main = select_goal_and_line(
            profile, state,
            public_facts(public_observation(14, 8000, 7000, phase.first, 0, "idle_command")));
        require(non_main.status == PredicateEvaluationStatus::False &&
                    !non_main.goal_id.has_value() && !non_main.line_id.has_value() &&
                    non_main.ready_node_ids.empty(),
                phase.second + " incorrectly activated the Main1 Swordsoul line");
    }
}

void test_action_specific_intents_and_progress() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto main_observation =
        public_observation(20, 8000, 7000, 0x04, 0, "idle_command");
    const auto selection =
        select_goal_and_line(profile, reset_state(profile), public_facts(main_observation));

    const auto mo_ye = idle_card(0);
    const auto mo_ye_observation =
        public_observation(20, 8000, 7000, 0x04, 0, "idle_command", 20001443);
    require_idle_shape(mo_ye, 0, "p0:HAND:0");
    std::vector<std::string> matched;
    require(match_candidate_intent_set(
                profile, {"intent.mo_ye.starter"}, mo_ye, mo_ye_observation, 0,
                matched) == PredicateEvaluationStatus::True,
            "Mo Ye normal-summon intent did not match");
    require_progress(profile, selection, mo_ye, mo_ye_observation, 3);

    const auto mo_ye_set = idle_card(3);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.mo_ye.starter"}, mo_ye_set, mo_ye_observation, 0,
                matched) == PredicateEvaluationStatus::False,
            "Mo Ye monster-set candidate matched normal-summon intent");

    const auto longyuan = idle_card(5);
    const auto longyuan_observation =
        public_observation(20, 8000, 7000, 0x04, 0, "idle_command", 93490856);
    require_idle_shape(longyuan, 5, "p0:HAND:0");
    require(match_candidate_intent_set(
                profile, {"intent.longyuan.access"}, longyuan, longyuan_observation, 0,
                matched) == PredicateEvaluationStatus::True,
            "Longyuan activation intent did not match");
    require_progress(profile, selection, longyuan, longyuan_observation, 3);
    const auto longyuan_normal = idle_card(0);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.longyuan.access"}, longyuan_normal, longyuan_observation, 0,
                matched) == PredicateEvaluationStatus::False,
            "Longyuan normal-summon-shaped candidate matched activation intent");

    const auto tenyi = idle_card(1);
    const auto tenyi_observation =
        public_observation(20, 8000, 7000, 0x04, 0, "idle_command", 87052196);
    require_idle_shape(tenyi, 1, "p0:HAND:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.tenyi.body"}, tenyi, tenyi_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Tenyi special-summon intent did not match");
    require_progress(profile, selection, tenyi, tenyi_observation, 3);
    const auto tenyi_wrong_command = idle_card(5);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.tenyi.body"}, tenyi_wrong_command, tenyi_observation, 0,
                matched) == PredicateEvaluationStatus::False,
            "Tenyi activation-shaped candidate matched free-body intent");

    const auto monk = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto monk_observation = public_observation(
        20, 8000, 7000, 0x04, 0, "idle_command", 32519092,
        ygo::observation::SemanticZone::ExtraDeck);
    require_idle_shape(monk, 1, "p0:EXTRA_DECK:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.monk.access"}, monk, monk_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Monk Extra Deck intent did not match");
    require_progress(profile, selection, monk, monk_observation, 3);

    const auto taia = idle_card(0);
    const auto taia_observation =
        public_observation(20, 8000, 7000, 0x04, 0, "idle_command", 56495147);
    require_idle_shape(taia, 0, "p0:HAND:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.taia.recovery"}, taia, taia_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Taia normal-summon intent did not match");
    require_progress(profile, selection, taia, taia_observation, 3);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.level8.payoff"}, taia, taia_observation, 0, matched) ==
                PredicateEvaluationStatus::False,
            "Taia bridge was classified as a Level-8 payoff");

    const auto summit = idle_card(5);
    const auto summit_observation =
        public_observation(20, 8000, 7000, 0x04, 0, "idle_command", 93850690);
    require_idle_shape(summit, 5, "p0:HAND:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.summit.recovery"}, summit, summit_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Sacred Summit activation intent did not match");
    require_progress(profile, selection, summit, summit_observation, 3);

    const auto chixiao = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto chixiao_observation = public_observation(
        20, 8000, 7000, 0x04, 0, "idle_command", 69248256,
        ygo::observation::SemanticZone::ExtraDeck);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.level8.payoff"}, chixiao, chixiao_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Chixiao Level-8 payoff intent did not match");
    require_progress(profile, selection, chixiao, chixiao_observation, 3);

    const auto qixing = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto qixing_observation = public_observation(
        20, 8000, 7000, 0x04, 0, "idle_command", 47710198,
        ygo::observation::SemanticZone::ExtraDeck);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.level10.payoff"}, qixing, qixing_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Qixing Level-10 payoff intent did not match");
    require_progress(profile, selection, qixing, qixing_observation, 3);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.level10.payoff"}, longyuan, longyuan_observation, 0,
                matched) == PredicateEvaluationStatus::False,
            "Longyuan bridge was classified as a Level-10 payoff");

    const auto finish = continuation_finish();
    std::vector<std::string> all_intents;
    for (const auto& intent : profile.candidate_intents) {
        all_intents.push_back(intent.intent_id);
    }
    matched.clear();
    require(match_candidate_intent_set(
                profile, all_intents, finish, main_observation, 0, matched) ==
                PredicateEvaluationStatus::False && matched.empty(),
            "continuation Finish acquired a strategic profile intent");
}

void test_interaction_and_public_recovery() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto chain_observation =
        public_observation(30, 8000, 7000, 0x04, 1, "chain", 51684157);
    const auto chain_selection =
        select_goal_and_line(profile, reset_state(profile), public_facts(chain_observation));
    const auto chain_activation = chain_source(card_reference());
    const auto chain_pass_candidate = chain_pass();
    require_chain_shape(chain_activation, true);
    require_chain_shape(chain_pass_candidate, false);
    std::vector<std::string> matched;
    require(match_candidate_intent_set(
                profile, {"intent.interaction.chain"}, chain_activation, chain_observation, 0,
                matched) == PredicateEvaluationStatus::True,
            "visible source-bearing Chain activation did not match");
    require_progress(profile, chain_selection, chain_activation, chain_observation, 3);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.interaction.chain"}, chain_pass_candidate,
                chain_observation, 0, matched) == PredicateEvaluationStatus::False,
            "chain.pass matched interaction intent");
    require_progress(profile, chain_selection, chain_pass_candidate, chain_observation, 0);

    for (const auto passcode : {14558127U, 97268402U, 10045474U}) {
        const auto hand_interaction = chain_source(card_reference());
        const auto interaction_observation =
            public_observation(30, 8000, 7000, 0x04, 1, "chain", passcode);
        matched.clear();
        require(match_candidate_intent_set(
                    profile, {"intent.interaction.chain"}, hand_interaction,
                    interaction_observation, 0, matched) == PredicateEvaluationStatus::True,
                "locked hand interaction did not match interaction intent");
    }

    const auto redacted_chain = chain_source(
        card_reference(ygo::observation::SemanticZone::Hand,
                       PublicCardReferenceKind::RedactedSlot));
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.interaction.chain"}, redacted_chain, chain_observation, 0,
                matched) == PredicateEvaluationStatus::Unsupported,
            "redacted Chain source was not UNSUPPORTED");
    const auto redacted_progress = evaluate_goal_line_progress(
        profile, chain_selection, RecoverySelection{}, redacted_chain, chain_observation, 0);
    require(redacted_progress.status == CandidateEvaluationStatus::Unsupported &&
                redacted_progress.contributions.empty(),
            "redacted Chain source produced strategic progress");

    auto main_state = reset_state(profile);
    main_state.active_goal_id = "goal.main1.swordsoul";
    main_state.active_line_id = "line.main1.swordsoul";
    main_state.public_resource_facts = {u64_fact("public.chain.length", 0)};
    const auto main_recovery =
        select_recovery_edge(profile, main_state, chain_observation, 0);
    require(main_recovery.status == PredicateEvaluationStatus::True &&
                main_recovery.recovery_edge_id ==
                    std::optional<std::string>("recovery.main1.interaction") &&
                main_recovery.target_line_id ==
                    std::optional<std::string>("line.interaction.preserve"),
            "public chain-window opening did not select interaction recovery");

    auto interaction_state = reset_state(profile);
    interaction_state.active_goal_id = "goal.interaction.preservation";
    interaction_state.active_line_id = "line.interaction.preserve";
    interaction_state.public_resource_facts = {u64_fact("public.chain.length", 1)};
    const auto main_observation =
        public_observation(31, 7000, 7000, 0x04, 0, "idle_command", 20001443);
    const auto interaction_recovery =
        select_recovery_edge(profile, interaction_state, main_observation, 0);
    require(interaction_recovery.status == PredicateEvaluationStatus::True &&
                interaction_recovery.recovery_edge_id ==
                    std::optional<std::string>("recovery.interaction.main1") &&
                interaction_recovery.target_line_id ==
                    std::optional<std::string>("line.main1.swordsoul"),
            "public chain-window loss did not select Main1 recovery");

    auto lp_only_state = interaction_state;
    const auto lp_only_observation =
        public_observation(31, 7000, 7000, 0x04, 1, "chain", 51684157);
    const auto lp_only_recovery =
        select_recovery_edge(profile, lp_only_state, lp_only_observation, 0);
    require(lp_only_recovery.status == PredicateEvaluationStatus::False &&
                !lp_only_recovery.recovery_edge_id.has_value(),
            "life-point movement alone triggered interaction recovery");

    const auto unchanged_observation =
        public_observation(31, 8000, 7000, 0x04, 1, "chain", 51684157);
    const auto unchanged_recovery =
        select_recovery_edge(profile, interaction_state, unchanged_observation, 0);
    require(unchanged_recovery.status == PredicateEvaluationStatus::False &&
                !unchanged_recovery.recovery_edge_id.has_value(),
            "unchanged public chain resource triggered recovery");

    const auto wrong_participant =
        select_recovery_edge(profile, interaction_state, main_observation, 1);
    require(wrong_participant.status == PredicateEvaluationStatus::Invalid,
            "cross-participant recovery observation was accepted");
}

void test_complete_domain_is_preserved() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto observation =
        public_observation(40, 8000, 7000, 0x04, 1, "chain", 51684157);
    const auto activation = chain_source(card_reference());
    const auto pass = chain_pass();
    const std::vector<EnvironmentActionCandidate> supplied = {activation, pass};
    const auto first_keys = std::vector<std::string>{
        supplied[0].public_action_key, supplied[1].public_action_key};
    std::vector<std::vector<std::string>> evidence;
    for (const auto& candidate : supplied) {
        std::vector<std::string> matched;
        (void)match_candidate_intent_set(
            profile, {"intent.interaction.chain"}, candidate, observation, 0, matched);
        evidence.push_back(std::move(matched));
    }
    require(supplied.size() == 2 && supplied[0].public_action_key == first_keys[0] &&
                supplied[1].public_action_key == first_keys[1] && evidence.size() == 2,
            "profile intent evaluation changed the supplied candidate domain");
}

}  // namespace

int main() {
    try {
        test_frame_controller_and_independent_nodes();
        test_action_specific_intents_and_progress();
        test_interaction_and_public_recovery();
        test_complete_domain_is_preserved();
        std::cout << "swordsoul_teacher_scenarios_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "swordsoul_teacher_scenarios_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
