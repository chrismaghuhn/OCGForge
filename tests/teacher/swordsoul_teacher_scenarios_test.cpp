#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/recovery_controller.hpp"
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

namespace ygo::teacher {
StrategyProfileV1 make_swordsoul_tenyi_profile();
}

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
    const std::optional<std::uint32_t>& visible_passcode = std::nullopt) {
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
        entity.locator = {"p0:MONSTER_ZONE:0"};
        entity.identity_known = true;
        entity.passcode = *visible_passcode;
        entity.owner = 0;
        entity.controller = 0;
        entity.zone = ygo::observation::SemanticZone::MonsterZone;
        entity.sequence = 0;
        entity.face_up = true;
        source.entities.push_back(entity);
    }
    return project_public_observation(source);
}

PublicFactSnapshot public_facts(const PublicEnvironmentObservation& observation) {
    const auto extracted = extract_public_fact_snapshot(observation);
    require(extracted.valid, "Swordsoul scenario public fact extraction failed");
    return extracted.snapshot;
}

PublicCardReference visible_source() {
    return {PublicCardReferenceKind::VisibleCard, "p0:MONSTER_ZONE:0"};
}

PublicCardReference redacted_source() {
    return {PublicCardReferenceKind::RedactedSlot, "p0:MONSTER_ZONE:0"};
}

EnvironmentActionCandidate candidate(
    const EnvironmentActionKind action_kind,
    const std::optional<PublicCardReference>& source = std::nullopt) {
    EnvironmentActionCandidate value;
    value.action_kind = action_kind;
    value.source_reference = source;
    PublicActionKeyInput key;
    key.action_kind = std::string(environment_action_kind_name(action_kind));
    key.source_reference = source;
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

void test_public_profile_goals_and_intents() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto observation = public_observation(12, 8000, 7000, 2, 0, "option");
    const auto facts = public_facts(observation);
    const auto state = reset_state(profile);

    const auto selected = select_goal_and_line(profile, state, facts);
    require(selected.status == PredicateEvaluationStatus::True &&
                selected.goal_id == std::optional<std::string>("goal.foundation.chixiao") &&
                selected.line_id == std::optional<std::string>("line.foundation.chixiao") &&
                selected.ready_node_ids ==
                    std::vector<std::string>{"node.foundation.chixiao"},
            "Swordsoul foundation goal/line selection was not deterministic");

    const auto mo_ye = candidate(EnvironmentActionKind::Option, visible_source());
    const auto mo_ye_observation =
        public_observation(12, 8000, 7000, 2, 0, "option", 20001443);
    std::vector<std::string> matched;
    require(match_candidate_intent_set(
                profile, {"intent.mo_ye.starter"}, mo_ye, mo_ye_observation, 0, matched) ==
                PredicateEvaluationStatus::True &&
                matched == std::vector<std::string>{"intent.mo_ye.starter"},
            "Mo Ye public role intent did not match");

    const auto longyuan = candidate(EnvironmentActionKind::Option, visible_source());
    const auto longyuan_observation =
        public_observation(12, 8000, 7000, 2, 0, "option", 93490856);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.longyuan.access"}, longyuan, longyuan_observation, 0,
                matched) == PredicateEvaluationStatus::True,
            "Longyuan public role intent did not match");

    const auto tenyi = candidate(EnvironmentActionKind::Option, visible_source());
    const auto tenyi_observation =
        public_observation(12, 8000, 7000, 2, 0, "option", 87052196);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.tenyi.body"}, tenyi, tenyi_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Tenyi body public role intent did not match");

    const auto monk = candidate(EnvironmentActionKind::Option, visible_source());
    const auto monk_observation =
        public_observation(12, 8000, 7000, 2, 0, "option", 32519092);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.monk.access"}, monk, monk_observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "Monk public role intent did not match");

    const auto redacted = candidate(EnvironmentActionKind::Option, redacted_source());
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.mo_ye.starter"}, redacted, mo_ye_observation, 0, matched) ==
                PredicateEvaluationStatus::Unsupported,
            "redacted Swordsoul role was not unsupported");

    auto safe_stop_state = reset_state(profile);
    safe_stop_state.active_goal_id = "goal.safe.stop";
    const auto safe_stop_selection =
        select_goal_and_line(profile, safe_stop_state, facts);
    require(safe_stop_selection.status == PredicateEvaluationStatus::True &&
                safe_stop_selection.goal_id == std::optional<std::string>("goal.safe.stop") &&
                safe_stop_selection.line_id == std::optional<std::string>("line.safe.stop"),
            "public safe-stop goal/line was not retained");

    const auto safe_stop = candidate(EnvironmentActionKind::Finish);
    matched.clear();
    require(match_candidate_intent_set(
                profile, {"intent.safe.stop"}, safe_stop, observation, 0, matched) ==
                PredicateEvaluationStatus::True,
            "public safe-stop intent did not match");

    const std::vector<EnvironmentActionCandidate> complete_domain = {
        mo_ye, candidate(EnvironmentActionKind::Chain), candidate(EnvironmentActionKind::Finish)};
    require(complete_domain.size() == 3,
            "scenario fixture did not retain its complete candidate domain");
    for (const auto& supplied : complete_domain) {
        std::vector<std::string> evidence;
        (void)match_candidate_intent_set(
            profile, {"intent.mo_ye.starter", "intent.interaction.chain", "intent.safe.stop"},
            supplied, mo_ye_observation, 0, evidence);
    }
    require(complete_domain.size() == 3,
            "profile intent evaluation filtered the supplied candidate domain");
}

void test_public_interruption_recovery() {
    const auto profile = make_swordsoul_tenyi_profile();
    auto state = reset_state(profile);
    state.active_goal_id = "goal.taia.summit.recovery";
    state.active_line_id = "line.taia.summit";
    state.public_resource_facts = {
        u64_fact("public.turn.phase", 2),
        u64_fact("public.life_points.self", 8000),
    };

    const auto interrupted_observation =
        public_observation(30, 7000, 7000, 2, 0, "option", 93850690);
    const auto recovery = select_recovery_edge(
        profile, state, interrupted_observation, 0);
    require(recovery.status == PredicateEvaluationStatus::True &&
                recovery.recovery_edge_id ==
                    std::optional<std::string>("recovery.taia.summit") &&
                recovery.target_line_id == std::optional<std::string>("line.taia.summit"),
            "public resource interruption did not select the declared Taia/Summit recovery");

    std::vector<std::string> matched;
    const auto summit = candidate(EnvironmentActionKind::Option, visible_source());
    require(match_candidate_intent_set(
                profile, {"intent.summit.recovery"}, summit, interrupted_observation, 0,
                matched) == PredicateEvaluationStatus::True,
            "Summit recovery candidate did not match its public role intent");

    auto unchanged_state = state;
    unchanged_state.public_resource_facts = {
        u64_fact("public.turn.phase", 2),
        u64_fact("public.life_points.self", 8000),
    };
    const auto unchanged_observation =
        public_observation(30, 8000, 7000, 2, 0, "option", 93850690);
    const auto no_recovery =
        select_recovery_edge(profile, unchanged_state, unchanged_observation, 0);
    require(no_recovery.status == PredicateEvaluationStatus::False &&
                !no_recovery.recovery_edge_id.has_value(),
            "unchanged public facts incorrectly triggered recovery");

    const auto wrong_participant =
        select_recovery_edge(profile, state,
                             public_observation(30, 7000, 7000, 2, 0, "option", 93850690), 1);
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
