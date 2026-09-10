#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/strategy_state_v3.hpp"
#include "ygo/teacher/teacher_decision_v2.hpp"
#include "ygo/teacher/teacher_decision_v3.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation observation(const std::uint64_t decision_index) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_player = 0;
    source.globals.turn_count = 16;
    source.globals.phase = 4;
    source.globals.chain_length = 0;
    source.globals.terminal = false;
    source.match_context.perspective_player = 0;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "unselect_card";
    source.decision_context.player = 0;
    return project_public_observation(source);
}

std::string action_key(const PublicCardSelectionOperation operation,
                       const bool v3) {
    PublicActionKeyInput input;
    input.action_kind = "card_selection";
    input.source_reference = PublicCardReference{
        PublicCardReferenceKind::VisibleCard, "p0:MONSTER_ZONE:0"};
    input.card_selection_operation = operation;
    return v3 ? public_action_key_v3(input) : public_action_key_v2(input);
}

void test_v3_state_and_delta_are_generation_pure() {
    const auto profile = make_salamangreat_profile();
    const auto v2_key = action_key(PublicCardSelectionOperation::Select, false);
    const auto v3_key = action_key(PublicCardSelectionOperation::Select, true);

    const auto reset = reset_strategy_state_v3(profile);
    require(reset.has_value() && validate_strategy_state_v3(*reset),
            "V3 strategy state did not reset and validate");
    auto state = *reset;
    state.last_accepted_decision_index = 1;
    state.last_accepted_public_action_key = v3_key;
    require(validate_strategy_state_v3(state), "V3 state rejected a V3 key");
    state.last_accepted_public_action_key = v2_key;
    require(!validate_strategy_state_v3(state), "V3 state accepted a V2 key");

    auto historical = reset_strategy_state_v2(profile);
    require(historical.has_value(), "V2 strategy state reset failed");
    historical->last_accepted_decision_index = 1;
    historical->last_accepted_public_action_key = v3_key;
    require(!validate_strategy_state_v2(*historical),
            "V2 state accepted a V3 key");

    TeacherStateDeltaV3 delta;
    delta.strategy_profile_id = profile.profile_id;
    delta.proposed_for_public_action_key = v3_key;
    require(validate_teacher_state_delta_v3(delta),
            "V3 state delta rejected a V3 key");
    delta.proposed_for_public_action_key = v2_key;
    require(!validate_teacher_state_delta_v3(delta),
            "V3 state delta accepted a V2 key");

    auto stale_state = reset_strategy_state_v3(profile);
    require(stale_state.has_value(), "V3 stale-state fixture reset failed");
    stale_state->last_accepted_decision_index = 10;
    stale_state->last_accepted_public_action_key = v3_key;
    TeacherStateDeltaV3 stale_delta;
    stale_delta.strategy_profile_id = profile.profile_id;
    stale_delta.base_last_accepted_decision_index = 10;
    stale_delta.base_last_accepted_public_action_key = v3_key;
    stale_delta.proposed_for_public_action_key = v3_key;
    require(!propose_teacher_state_delta_v3(
                *stale_state, observation(10), 0, profile, stale_delta),
            "V3 stale state delta was accepted");
}

void test_v3_reconciliation_and_rejected_step_are_transactional() {
    const auto profile = make_salamangreat_profile();
    auto state = reset_strategy_state_v3(profile);
    require(state.has_value(), "V3 strategy state reset failed");
    state->active_goal_id = "goal.main1.salamangreat";
    state->active_line_id = "line.main1.salamangreat";
    const auto before = *state;

    const auto reconciled = reconcile_strategy_state_with_evidence_v3(
        *state, 0, observation(10));
    require(reconciled.has_value() && reconciled->state == before &&
                reconciled->invalidation_reason_ids.empty(),
            "V3 reconciliation did not retain the public commitment");

    StepRejected rejected;
    rejected.authoritative_state_unchanged = true;
    require(observe_step_rejected_v3(*state, rejected),
            "V3 rejected-step observation was not accepted");
    require(*state == before, "V3 rejected-step observation mutated state");
}

}  // namespace

int main() {
    try {
        test_v3_state_and_delta_are_generation_pure();
        test_v3_reconciliation_and_rejected_step_are_transactional();
        std::cout << "teacher V3 state successor tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
