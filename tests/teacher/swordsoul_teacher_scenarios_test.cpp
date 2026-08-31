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
        ygo::observation::SemanticZone::Hand) {
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
        entity.identity_known = true;
        entity.passcode = *visible_passcode;
        entity.owner = 0;
        entity.controller = 0;
        entity.zone = visible_zone;
        entity.sequence = 0;
        entity.face_up = false;
        source.entities.push_back(entity);
    }
    return project_public_observation(source);
}

PublicFactSnapshot public_facts(const PublicEnvironmentObservation& observation) {
    const auto extracted = extract_public_fact_snapshot(observation);
    require(extracted.valid, "Swordsoul scenario public fact extraction failed");
    return extracted.snapshot;
}

PublicCardReference visible_source(
    const ygo::observation::SemanticZone zone = ygo::observation::SemanticZone::Hand) {
    return {PublicCardReferenceKind::VisibleCard,
            std::string("p0:") + ygo::observation::semantic_zone_name(zone) + ":0"};
}

PublicCardReference redacted_source() {
    return {PublicCardReferenceKind::RedactedSlot, "p0:HAND:0"};
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
    const ygo::observation::SemanticZone zone =
        ygo::observation::SemanticZone::Hand) {
    return idle_source(command, visible_source(zone));
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

EnvironmentActionCandidate chain_activation() {
    return chain_source(visible_source());
}

EnvironmentActionCandidate chain_pass() {
    // The public projection of the decoder's chain.pass has no source or
    // choice and carries the pass phase marker.
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

void require_idle_shape(const EnvironmentActionCandidate& value,
                        const std::uint32_t command,
                        const std::string& locator) {
    require(value.action_kind == EnvironmentActionKind::IdleCommand &&
                value.phase.has_value() && *value.phase == command && value.choice.has_value() &&
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

void test_public_profile_goals_and_intents() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto observation = public_observation(12, 8000, 7000, 0x04, 0, "idle_command");
    const auto facts = public_facts(observation);
    const auto state = reset_state(profile);

    const auto selected = select_goal_and_line(profile, state, facts);
    require(selected.status == PredicateEvaluationStatus::True &&
                selected.goal_id == std::optional<std::string>("goal.foundation.chixiao") &&
                selected.line_id == std::optional<std::string>("line.foundation.chixiao") &&
                selected.ready_node_ids ==
                    std::vector<std::string>{"node.foundation.chixiao"},
            "Swordsoul foundation goal/line selection was not deterministic");

    const auto mo_ye = idle_card(0);
    const auto mo_ye_observation =
        public_observation(12, 8000, 7000, 0x04, 0, "idle_command", 20001443);
    require_idle_shape(mo_ye, 0, "p0:HAND:0");
    std::vector<std::string> matched;
    require(match_candidate_intent_set(
                profile, {"intent.mo_ye.starter"}, mo_ye, mo_ye_observation, 0, matched) ==
                PredicateEvaluationStatus::True &&
                matched == std::vector<std::string>{"intent.mo_ye.starter"},
            "Mo Ye public role intent did not match");

    const auto longyuan = idle_card(5);
    const auto longyuan_observation =
        public_observation(12, 8000, 7000, 0x04, 0, "idle_command", 93490856);
    require_idle_shape(longyuan, 5, "p0:HAND:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.longyuan.access"}, longyuan, longyuan_observation, 0,
                matched) == PredicateEvaluationStatus::True,
            "Longyuan public role intent did not match");

    const auto tenyi = idle_card(1);
    const auto tenyi_observation =
        public_observation(12, 8000, 7000, 0x04, 0, "idle_command", 87052196);
    require_idle_shape(tenyi, 1, "p0:HAND:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.tenyi.body"}, tenyi, tenyi_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Tenyi body public role intent did not match");

    const auto taia = idle_card(0);
    const auto taia_observation =
        public_observation(12, 8000, 7000, 0x04, 0, "idle_command", 56495147);
    require_idle_shape(taia, 0, "p0:HAND:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.taia.recovery"}, taia, taia_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Taia public role intent did not match");

    const auto summit = idle_card(5);
    const auto summit_observation =
        public_observation(12, 8000, 7000, 0x04, 0, "idle_command", 93850690);
    require_idle_shape(summit, 5, "p0:HAND:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.summit.recovery"}, summit, summit_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Sacred Summit public role intent did not match");

    const auto monk = idle_card(1, ygo::observation::SemanticZone::ExtraDeck);
    const auto monk_observation =
        public_observation(12, 8000, 7000, 0x04, 0, "idle_command", 32519092,
                           ygo::observation::SemanticZone::ExtraDeck);
    require_idle_shape(monk, 1, "p0:EXTRA_DECK:0");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.monk.access"}, monk, monk_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Monk public role intent did not match");

    const auto redacted = idle_source(0, redacted_source());
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.mo_ye.starter"}, redacted, mo_ye_observation, 0, matched) ==
                PredicateEvaluationStatus::Unsupported,
            "redacted Swordsoul role was not unsupported");

    const auto finish = continuation_finish();
    std::vector<std::string> all_intents;
    for (const auto& intent : profile.candidate_intents) {
        all_intents.push_back(intent.intent_id);
    }
    matched.clear();
    require(match_candidate_intent_set(profile, all_intents, finish, observation, 0, matched) ==
                PredicateEvaluationStatus::False && matched.empty(),
            "continuation Finish was classified as a strategic safe stop");

    const std::vector<std::pair<std::string, std::string>> main_phase_lines = {
        {"goal.foundation.chixiao", "line.foundation.chixiao"},
        {"goal.level10.access", "line.level10.longyuan"},
        {"goal.taia.summit.recovery", "line.taia.summit"},
        {"goal.tenyi.monk.access", "line.tenyi.monk"},
    };
    for (const auto& expected : main_phase_lines) {
        auto main_state = reset_state(profile);
        main_state.active_goal_id = expected.first;
        const auto main = select_goal_and_line(
            profile, main_state,
            public_facts(public_observation(40, 8000, 7000, 0x04, 0, "idle_command")));
        require(main.status == PredicateEvaluationStatus::True &&
                    main.goal_id == std::optional<std::string>(expected.first) &&
                    main.line_id == std::optional<std::string>(expected.second),
                "MAIN1 did not activate a Main-Phase Swordsoul line");

        for (const auto phase : {0x02U, 0x80U, 0x200U}) {
            auto non_main_state = reset_state(profile);
            non_main_state.active_goal_id = expected.first;
            const auto non_main = select_goal_and_line(
                profile, non_main_state,
                public_facts(public_observation(40, 8000, 7000, phase, 0, "idle_command")));
            require(non_main.status == PredicateEvaluationStatus::True &&
                        non_main.goal_id == std::optional<std::string>(expected.first) &&
                        !non_main.line_id.has_value() && non_main.ready_node_ids.empty(),
                    "non-MAIN1 phase activated a Main-Phase Swordsoul line");
        }
    }

    const auto chain_observation =
        public_observation(41, 8000, 7000, 0x04, 1, "chain", 51684157);
    const auto chain_activation_candidate = chain_activation();
    const auto chain_pass_candidate = chain_pass();
    require_chain_shape(chain_activation_candidate, true);
    require_chain_shape(chain_pass_candidate, false);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.interaction.chain"}, chain_activation_candidate,
                chain_observation, 0, matched) == PredicateEvaluationStatus::True,
            "source-bearing visible Chain candidate did not match interaction intent");
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.interaction.chain"}, chain_pass_candidate,
                chain_observation, 0, matched) == PredicateEvaluationStatus::False,
            "chain.pass matched interaction intent");
    const auto redacted_chain = chain_source(redacted_source());
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.interaction.chain"}, redacted_chain,
                chain_observation, 0, matched) == PredicateEvaluationStatus::Unsupported,
            "redacted Chain source was not UNSUPPORTED");

    GoalLineSelection interaction_selection;
    interaction_selection.status = PredicateEvaluationStatus::True;
    interaction_selection.goal_id = "goal.interaction.preservation";
    interaction_selection.line_id = "line.interaction.preserve";
    interaction_selection.ready_node_ids = {"node.interaction.preserve"};
    RecoverySelection no_recovery;
    const auto pass_progress = evaluate_goal_line_progress(
        profile, interaction_selection, no_recovery, chain_pass_candidate, chain_observation, 0);
    require(pass_progress.status == CandidateEvaluationStatus::Supported &&
                pass_progress.contributions.size() == 1 &&
                pass_progress.contributions[0].value == 0,
            "chain.pass received interaction progress");
    const auto activation_progress = evaluate_goal_line_progress(
        profile, interaction_selection, no_recovery, chain_activation_candidate,
        chain_observation, 0);
    require(activation_progress.status == CandidateEvaluationStatus::Supported &&
                activation_progress.contributions.size() == 1 &&
                activation_progress.contributions[0].value == 3,
            "source-bearing Chain activation did not receive interaction progress");

    const std::vector<EnvironmentActionCandidate> complete_domain = {
        chain_activation_candidate, chain_pass_candidate};
    require(complete_domain.size() == 2,
            "scenario fixture did not retain its complete candidate domain");
    for (const auto& supplied : complete_domain) {
        std::vector<std::string> evidence;
        (void)match_candidate_intent_set(
            profile, {"intent.mo_ye.starter", "intent.interaction.chain"},
            supplied, chain_observation, 0, evidence);
    }
    require(complete_domain.size() == 2,
            "profile intent evaluation filtered the supplied candidate domain");
}

void test_public_interruption_recovery() {
    const auto profile = make_swordsoul_tenyi_profile();
    auto state = reset_state(profile);
    state.active_goal_id = "goal.interaction.preservation";
    state.active_line_id = "line.interaction.preserve";
    state.public_resource_facts = {
        u64_fact("public.chain.length", 1),
    };

    const auto lp_only_observation =
        public_observation(30, 7000, 7000, 0x04, 1, "idle_command", 20001443);
    const auto lp_only_recovery =
        select_recovery_edge(profile, state, lp_only_observation, 0);
    require(lp_only_recovery.status == PredicateEvaluationStatus::False &&
                !lp_only_recovery.recovery_edge_id.has_value(),
            "life-point movement alone triggered Swordsoul recovery");

    const auto interrupted_observation =
        public_observation(30, 7000, 7000, 0x04, 0, "idle_command", 20001443);
    require_idle_shape(idle_card(0), 0, "p0:HAND:0");
    const auto recovery = select_recovery_edge(
        profile, state, interrupted_observation, 0);
    require(recovery.status == PredicateEvaluationStatus::True &&
                recovery.recovery_edge_id ==
                    std::optional<std::string>("recovery.interaction.foundation") &&
                recovery.target_line_id ==
                    std::optional<std::string>("line.foundation.chixiao"),
            "public chain-window interruption did not select declared recovery");

    std::vector<std::string> matched;
    const auto mo_ye = idle_card(0);
    require(match_candidate_intent_set(
                profile, {"intent.mo_ye.starter"}, mo_ye, interrupted_observation, 0,
                matched) == PredicateEvaluationStatus::True,
            "public recovery candidate did not match its public role intent");

    auto unchanged_state = state;
    unchanged_state.public_resource_facts = {
        u64_fact("public.chain.length", 1),
    };
    const auto unchanged_observation =
        public_observation(30, 8000, 7000, 0x04, 1, "idle_command", 20001443);
    const auto no_recovery =
        select_recovery_edge(profile, unchanged_state, unchanged_observation, 0);
    require(no_recovery.status == PredicateEvaluationStatus::False &&
                !no_recovery.recovery_edge_id.has_value(),
            "unchanged public facts incorrectly triggered recovery");

    const auto wrong_participant =
        select_recovery_edge(profile, state,
                             public_observation(30, 7000, 7000, 0x04, 0, "idle_command", 20001443), 1);
    require(wrong_participant.status == PredicateEvaluationStatus::Invalid,
            "cross-participant Swordsoul recovery observation was accepted");
}

}  // namespace

int main() {
    try {
        test_public_profile_goals_and_intents();
        test_public_interruption_recovery();
        std::cout << "swordsoul_teacher_scenarios_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "swordsoul_teacher_scenarios_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
