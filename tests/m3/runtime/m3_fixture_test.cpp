#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "ocgapi_constants.h"
#include "ocgapi_types.h"
#include "ygo/core/core_host.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/m3/conformance_policy.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/observation_session.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"
#include "ygo/trace/sha256.hpp"
#include "query_decoder.hpp"

#ifndef YGO_M3_DECK_A
#error "YGO_M3_DECK_A must be supplied by CMake"
#endif
#ifndef YGO_M3_DECK_B
#error "YGO_M3_DECK_B must be supplied by CMake"
#endif
#ifndef YGO_M3_CARDSCRIPTS
#error "YGO_M3_CARDSCRIPTS must be supplied by CMake"
#endif
#ifndef YGO_M3_SS01_SETUP
#error "YGO_M3_SS01_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS06_SETUP
#error "YGO_M3_SS06_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS05_SETUP
#error "YGO_M3_SS05_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_INT01_SETUP
#error "YGO_M3_INT01_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_INT02_SETUP
#error "YGO_M3_INT02_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_INT03_SETUP
#error "YGO_M3_INT03_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_INT04_SETUP
#error "YGO_M3_INT04_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_INT05_SETUP
#error "YGO_M3_INT05_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG01_SETUP
#error "YGO_M3_SG01_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG02_SETUP
#error "YGO_M3_SG02_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG03_SETUP
#error "YGO_M3_SG03_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG04_SETUP
#error "YGO_M3_SG04_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS10_SETUP
#error "YGO_M3_SS10_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS10_BANISH_SETUP
#error "YGO_M3_SS10_BANISH_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS12_SETUP
#error "YGO_M3_SS12_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS14_SETUP
#error "YGO_M3_SS14_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS15_SETUP
#error "YGO_M3_SS15_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS16_SETUP
#error "YGO_M3_SS16_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS16_CHENGYING_SETUP
#error "YGO_M3_SS16_CHENGYING_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SS17_SETUP
#error "YGO_M3_SS17_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG05_SETUP
#error "YGO_M3_SG05_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG06_SETUP
#error "YGO_M3_SG06_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG07_JACK_SETUP
#error "YGO_M3_SG07_JACK_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG07_WEASEL_SETUP
#error "YGO_M3_SG07_WEASEL_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG07_FALCO_SETUP
#error "YGO_M3_SG07_FALCO_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG08_REAL_SETUP
#error "YGO_M3_SG08_REAL_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG09_DIRECT_SETUP
#error "YGO_M3_SG09_DIRECT_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG11_SETUP
#error "YGO_M3_SG11_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG11_PLAYER0_SETUP
#error "YGO_M3_SG11_PLAYER0_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG12_SETUP
#error "YGO_M3_SG12_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG13_SETUP
#error "YGO_M3_SG13_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG14_SETUP
#error "YGO_M3_SG14_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG15_SETUP
#error "YGO_M3_SG15_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG18_SETUP
#error "YGO_M3_SG18_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG19_SETUP
#error "YGO_M3_SG19_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG19_NO_TARGET_SETUP
#error "YGO_M3_SG19_NO_TARGET_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG16_NEGATE_SETUP
#error "YGO_M3_SG16_NEGATE_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG16_RECOVERY_SETUP
#error "YGO_M3_SG16_RECOVERY_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG17_SETUP
#error "YGO_M3_SG17_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_BTL01_SETUP
#error "YGO_M3_BTL01_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M3_SG20_SETUP
#error "YGO_M3_SG20_SETUP must be supplied by CMake"
#endif

namespace {

constexpr std::uint32_t kMoYe = 20001443;
constexpr std::uint32_t kTaia = 56495147;
constexpr std::uint32_t kLongyuan = 93490856;
constexpr std::uint32_t kFire = 11962031;
constexpr std::uint32_t kBalelynx = 14812471;
constexpr std::uint32_t kSanctuary = 1295111;
constexpr std::uint32_t kToken = 20001444;
constexpr std::uint32_t kChixiao = 69248256;
constexpr std::uint32_t kQixing = 47710198;
constexpr std::uint32_t kEmergence = 56465981;
constexpr std::uint32_t kBlackout = 14821890;
constexpr std::uint32_t kBaxia = 83755611;
constexpr std::uint32_t kChengying = 96633955;
constexpr std::uint32_t kRage = 14934922;
constexpr std::uint32_t kFoxy = 94620082;
constexpr std::uint32_t kCircle = 51684157;
constexpr std::uint32_t kVishuda = 23431858;
constexpr std::uint32_t kAshuna = 87052196;
constexpr std::uint32_t kAdhara = 98159737;
constexpr std::uint32_t kMonk = 32519092;
constexpr std::uint32_t kShaman = 78917791;
constexpr std::uint32_t kGazelle = 26889158;
constexpr std::uint32_t kSpinny = 52277807;
constexpr std::uint32_t kJackJaguar = 56003780;
constexpr std::uint32_t kWeasel = 57357130;
constexpr std::uint32_t kFalco = 20618081;
constexpr std::uint32_t kMiragestallio = 87327776;
constexpr std::uint32_t kRagingPhoenix = 57134592;
constexpr std::uint32_t kPyroPhoenix = 31313405;
constexpr std::uint32_t kHeatleo = 41463181;
constexpr std::uint32_t kSunlightWolf = 87871125;
constexpr std::uint32_t kPrometheanPrincess = 2772337;
constexpr std::uint32_t kHiita = 48815792;
constexpr std::uint32_t kRoar = 51339637;
constexpr std::uint32_t kAsh = 14558127;
constexpr std::uint32_t kVeiler = 97268402;
constexpr std::uint32_t kImpermanence = 10045474;
constexpr std::uint32_t kCalledBy = 24224830;
constexpr std::uint32_t kGhostBelle = 73642296;

using ygo::protocol::ActionCandidate;
using ygo::protocol::DecisionRequest;

struct FixtureSpec {
    std::string id;
    std::string setup;
    std::vector<std::uint32_t> focus_codes;
    std::uint32_t max_steps = 160;
    bool mirror_seats = false;
};

struct Evidence {
    std::set<std::string> decision_families;
    std::set<std::uint32_t> observed_codes;
    std::vector<std::uint32_t> unselect_sources;
    std::vector<std::uint32_t> selected_trigger_chain_sources;
    std::vector<std::uint32_t> selected_blackout_sources;
    std::set<std::uint32_t> blackout_destroyed_codes;
    std::vector<std::string> observation_hashes;
    std::uint32_t continuation_intermediate_steps = 0;
    std::uint32_t decisions = 0;
    std::uint32_t engine_steps = 0;
    bool terminal = false;
    std::uint8_t winner = 255;
    bool token_state_valid = false;
    bool chixiao_state_valid = false;
    bool chixiao_search_selected = false;
    bool chixiao_search_state_valid = false;
    bool trigger_chain_resolved = false;
    bool balelynx_state_valid = false;
    bool sanctuary_state_valid = false;
    bool balelynx_effect_yes = false;
    bool sanctuary_searched = false;
    bool sanctuary_activated = false;
    bool reincarnation_extra_selected = false;
    bool reincarnation_material_selected = false;
    bool reincarnation_placement_selected = false;
    bool longyuan_discarded = false;
    bool longyuan_burn = false;
    bool qixing_state_valid = false;
    bool blackout_activated = false;
    bool blackout_banished_from_grave = false;
    bool blackout_banish_trigger_activated = false;
    bool blackout_token_state_valid = false;
    bool chengying_state_valid = false;
    bool chengying_dynamic_state_valid = false;
    bool chengying_replacement_selected = false;
    std::int32_t chengying_attack_before_banish = -1;
    std::int32_t chengying_attack_after_banish = -1;
    std::uint64_t ashuna_activation_engine_step = 0;
    bool ashuna_phase_boundary_after_activation = false;
    bool ashuna_extra_available_after_expiry = false;
    bool qixing_interaction_activated = false;
    bool qixing_rage_activation = false;
    bool qixing_interaction_chain_resolved = false;
    bool qixing_rage_banished = false;
    bool qixing_interaction_damage = false;
    bool chixiao_negation_activated = false;
    bool chixiao_negation_targeted = false;
    bool chixiao_negation_chain_resolved = false;
    bool interaction_chain_activated = false;
    bool interaction_target_selected = false;
    bool interaction_chain_resolved = false;
    bool ghost_belle_chain_activated = false;
    bool ghost_belle_chain_resolved = false;
    bool circle_activated = false;
    bool circle_release_selected = false;
    bool circle_search_selected = false;
    bool circle_search_state_valid = false;
    bool ashuna_activated = false;
    bool ashuna_deck_target_selected = false;
    bool ashuna_deck_state_valid = false;
    bool ashuna_extra_available_before = false;
    bool ashuna_non_wyrm_extra_blocked = false;
    bool vishuda_activated = false;
    bool vishuda_target_selected = false;
    bool vishuda_return_state_valid = false;
    bool adhara_activated = false;
    bool adhara_target_selected = false;
    bool adhara_recovery_state_valid = false;
    bool gazelle_hand_activated = false;
    bool spinny_discarded = false;
    bool gazelle_deck_send_selected = false;
    bool gazelle_deck_state_valid = false;
    bool gazelle_chain_resolved = false;
    std::uint32_t gazelle_deck_salamegreat_candidate_count = 0;
    bool gazelle_foxy_deck_send_selected = false;
    bool gazelle_foxy_gy_state_valid = false;
    bool jack_jaguar_activated = false;
    bool jack_jaguar_target_selected = false;
    bool jack_jaguar_summoned = false;
    bool jack_jaguar_state_valid = false;
    bool jack_jaguar_target_recycled = false;
    bool jack_link_condition_observed = false;
    bool jack_emzone_place_domain = false;
    bool jack_candidate_present_under_mr5 = false;
    bool weasel_trigger_activated = false;
    bool weasel_selected = false;
    bool weasel_recycled = false;
    bool weasel_summoned = false;
    bool weasel_target_summoned = false;
    bool weasel_drawn = false;
    std::uint64_t weasel_trigger_engine_step = 0;
    bool weasel_state_valid = false;
    bool falco_activated = false;
    bool falco_target_selected = false;
    bool falco_target_recovered = false;
    bool falco_summoned = false;
    bool falco_state_valid = false;
    bool wolf_fire_trigger_activated = false;
    bool wolf_fire_target_selected = false;
    bool wolf_fire_recovered = false;
    bool wolf_state_valid = false;
    bool wolf_st_trigger_activated = false;
    bool wolf_st_target_selected = false;
    bool wolf_st_recovered = false;
    bool raging_phoenix_reincarnation = false;
    bool raging_phoenix_trigger_activated = false;
    bool raging_phoenix_search_selected = false;
    bool raging_phoenix_search_resolved = false;
    bool raging_phoenix_state_valid = false;
    bool pyro_phoenix_reincarnation = false;
    bool pyro_phoenix_trigger_activated = false;
    bool pyro_phoenix_destroyed_opponent = false;
    bool pyro_phoenix_revive_activated = false;
    bool pyro_phoenix_revive_target_selected = false;
    bool pyro_phoenix_revived_link = false;
    bool pyro_phoenix_state_valid = false;
    bool heatleo_reincarnation = false;
    bool heatleo_trigger_activated = false;
    bool heatleo_target_selected = false;
    bool heatleo_target_returned = false;
    bool heatleo_state_valid = false;
    bool promethean_extra_selected = false;
    bool promethean_material_selected = false;
    bool promethean_placement_selected = false;
    bool promethean_state_valid = false;
    bool promethean_fire_revival_activated = false;
    bool promethean_fire_target_selected = false;
    bool promethean_fire_revived = false;
    bool promethean_nonfire_candidate_before = false;
    bool promethean_nonfire_candidate_blocked = false;
    bool hiita_extra_selected = false;
    bool hiita_material_selected = false;
    bool hiita_placement_selected = false;
    bool hiita_activation_activated = false;
    bool hiita_target_selected = false;
    bool hiita_revived = false;
    bool hiita_state_valid = false;
    bool hiita_no_target_activation_absent = false;
    bool roar_negate_activated = false;
    bool roar_negate_chain_resolved = false;
    bool roar_negate_target_destroyed = false;
    bool roar_recovery_trigger_activated = false;
    bool roar_recovery_set = false;
    bool roar_state_valid = false;
    bool rage_activated = false;
    bool rage_cost_sent = false;
    bool rage_target_selected = false;
    bool rage_target_destroyed = false;
    std::uint32_t rage_target_count = 0;
    bool rage_state_valid = false;
    bool xyz_extra_selected = false;
    bool xyz_material_one_selected = false;
    bool xyz_material_two_selected = false;
    std::set<std::uint32_t> xyz_material_selected_codes;
    bool xyz_summoned = false;
    bool xyz_state_valid = false;
    bool xyz_material_identity_verified = false;
    bool xyz_material_identity_redacted = false;
    bool xyz_query_before_detach = false;
    bool xyz_query_after_detach = false;
    bool xyz_detached_query_empty = false;
    bool xyz_detached_code_known = false;
    std::uint32_t xyz_detached_code = 0;
    std::uint32_t xyz_material_count_before = 0;
    std::uint32_t xyz_material_count_after = 0;
    bool xyz_detach_accepted = false;
    bool xyz_effect_activated = false;
    bool xyz_deck_summon_selected = false;
    bool xyz_deck_summoned = false;
    bool xyz_fire_restriction_seen = false;
    bool xyz_fire_restriction_blocked = false;
    bool xyz_fire_restriction_expired = false;
    bool xyz_restriction_phase_boundary = false;
    std::uint64_t xyz_effect_engine_step = 0;
    std::uint32_t battle_commands = 0;
    bool battle_attack_selected = false;
    bool battle_target_selected = false;
    bool battle_life_points_changed = false;
    bool battle_card_destroyed = false;
    bool battle_terminal = false;
    bool link_extra_selected = false;
    bool link_material_fire_selected = false;
    bool link_material_spinny_selected = false;
    bool link_placement_selected = false;
    bool baxia_extra_selected = false;
    bool baxia_token_material_selected = false;
    bool baxia_taia_material_selected = false;
    bool baxia_summoned = false;
    bool baxia_trigger_activated = false;
    bool baxia_ignition_activated = false;
    bool baxia_destroy_target_selected = false;
    bool baxia_revive_target_selected = false;
    bool baxia_destroyed = false;
    bool baxia_revived = false;
    std::vector<std::uint32_t> baxia_trigger_targets;
    bool token_restriction_extra_available_before = false;
    bool token_restriction_non_synchro_blocked = false;
    bool token_restriction_expired = false;
    bool token_restriction_extra_available_after = false;
    bool token_restriction_balelynx_selected_after = false;
    bool tenyi_condition_false_no_ashuna = false;
    bool tenyi_condition_true_ashuna_candidate = false;
    bool tenyi_condition_true_ashuna_selected = false;
    bool monk_extra_selected = false;
    bool shaman_extra_selected = false;
    bool monk_material_selected = false;
    bool shaman_material_selected = false;
    bool monk_placement_selected = false;
    bool shaman_placement_selected = false;
    bool monk_state_valid = false;
    bool shaman_state_valid = false;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool candidate_is_code(const ActionCandidate& candidate, std::uint32_t code) {
    return candidate.source_card == code ||
           candidate.semantic_key.find(std::to_string(code)) != std::string::npos;
}

const ygo::observation::ObservedCard* find_card(
    const ygo::observation::PlayerObservation& observation, std::uint32_t code,
    ygo::observation::SemanticZone zone) {
    for (const auto& entity : observation.entities) {
        if (entity.identity_known && entity.passcode.value_or(0) == code && entity.zone == zone) {
            return &entity;
        }
    }
    return nullptr;
}

void validate_observation_candidates(const ygo::observation::PlayerObservation& observation,
                                     const DecisionRequest& request,
                                     bool allow_redacted_xyz_material_candidate = false) {
    for (const auto& candidate : request.candidates) {
        const auto visible_location = [](const ActionCandidate& item) {
            return (item.source_card != 0 && item.source_location != 0 &&
                    item.source_location != LOCATION_DECK && item.source_location != LOCATION_EXTRA) ||
                   (item.target_card != 0 && item.target_location != 0 &&
                    item.target_location != LOCATION_DECK && item.target_location != LOCATION_EXTRA);
        };
        if (visible_location(candidate) &&
            !ygo::observation::candidate_observation_consistent(observation, candidate)) {
            if (allow_redacted_xyz_material_candidate &&
                (((candidate.source_location & LOCATION_OVERLAY) != 0) ||
                 ((candidate.target_location & LOCATION_OVERLAY) != 0))) {
                continue;
            }
            throw ygo::protocol::ProtocolError(
                ygo::protocol::ProtocolErrorCode::UnsupportedDecision,
                "visible M3 fixture candidate does not resolve against PlayerObservation: " +
                    candidate.semantic_key,
                request.engine_message_type, request.player);
        }
    }
}

void inspect_observation(const ygo::observation::PlayerObservation& observation, Evidence& evidence) {
    require(observation.observation_hash.size() == 64, "fixture observation hash is missing");
    evidence.observation_hashes.push_back(observation.observation_hash);
    for (const auto& entity : observation.entities) {
        if (entity.identity_known && entity.passcode.has_value()) {
            evidence.observed_codes.insert(*entity.passcode);
        }
    }
}

void inspect_xyz_public_query(const ygo::core::CoreHost& host, Evidence& evidence) {
    std::optional<std::uint32_t> parent_sequence;
    ygo::observation::detail::RawCardQuery parent;
    for (std::uint32_t sequence = 0; sequence < 7; ++sequence) {
        OCG_QueryInfo parent_info{};
        parent_info.flags = QUERY_CODE | QUERY_OVERLAY_CARD | QUERY_END;
        parent_info.con = 0;
        parent_info.loc = LOCATION_MZONE;
        parent_info.seq = sequence;
        const auto parent_bytes = host.query(parent_info);
        if (parent_bytes.empty()) {
            continue;
        }
        auto candidate = ygo::observation::detail::decode_card_query(parent_bytes);
        if (candidate.code.value_or(0) == kMiragestallio) {
            parent_sequence = sequence;
            parent = std::move(candidate);
            break;
        }
    }
    if (!parent_sequence.has_value()) {
        return;
    }
    const auto material_query = [&](std::uint32_t index) {
        OCG_QueryInfo info{};
        info.flags = QUERY_CODE | QUERY_OWNER | QUERY_POSITION | QUERY_IS_PUBLIC |
                     QUERY_IS_HIDDEN | QUERY_END;
        info.con = 0;
        info.loc = LOCATION_MZONE | LOCATION_OVERLAY;
        info.seq = *parent_sequence;
        info.overlay_seq = index;
        return host.query(info);
    };
    if (parent.overlay_codes.size() == 2 && !evidence.xyz_detach_accepted) {
        const auto first = material_query(0);
        const auto second = material_query(1);
        const auto first_card = first.empty()
                                    ? ygo::observation::detail::RawCardQuery{}
                                    : ygo::observation::detail::decode_card_query(first);
        const auto second_card = second.empty()
                                     ? ygo::observation::detail::RawCardQuery{}
                                     : ygo::observation::detail::decode_card_query(second);
        if (!first.empty() && !second.empty()) {
            if (first_card.code.has_value() && second_card.code.has_value() &&
                ((*first_card.code == kFoxy && *second_card.code == kSpinny) ||
                 (*first_card.code == kSpinny && *second_card.code == kFoxy))) {
                evidence.xyz_query_before_detach = true;
            }
        }
    }
    if (parent.overlay_codes.size() == 1 && evidence.xyz_detach_accepted) {
        const auto remaining = material_query(0);
        const auto detached_slot = material_query(1);
        if (!remaining.empty() && detached_slot.empty()) {
            const auto remaining_card = ygo::observation::detail::decode_card_query(remaining);
            if (remaining_card.code.has_value() &&
                (!evidence.xyz_detached_code_known || *remaining_card.code != evidence.xyz_detached_code)) {
                evidence.xyz_query_after_detach = true;
                evidence.xyz_detached_query_empty = true;
            }
        }
    }
}

ygo::core::CoreHostConfig make_config(const std::vector<std::uint32_t>& required_scripts) {
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M3_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = std::string(ygo::m3::canonical_rules().rules_bundle_id);
    config.duel_flags = ygo::m3::canonical_rules().duel_flags;
    config.required_script_codes = required_scripts;
    config.seed = ygo::core::derive_seed_bundle(2);
    return config;
}

FixtureSpec spec_for(const std::string& id) {
    if (id == "ss01") {
        return {id, YGO_M3_SS01_SETUP, {kEmergence, kChixiao, kMoYe}, 140};
    }
    if (id == "ss06") {
        return {id, YGO_M3_SS06_SETUP, {kLongyuan, kQixing, kChixiao}, 160};
    }
    if (id == "ss08") {
        return {id, YGO_M3_SS08_SETUP, {kTaia, kFire, kFoxy, kBaxia, kToken, kMoYe}, 180};
    }
    if (id == "ss07") {
        return {id, YGO_M3_SS07_SETUP, {kMoYe, kChixiao, kBalelynx}, 200};
    }
    if (id == "ss12_condition") {
        return {id, YGO_M3_SS12_CONDITION_SETUP, {kMoYe, kAshuna, kChixiao}, 180};
    }
    if (id == "ss18") {
        return {id, YGO_M3_SS12_CONDITION_SETUP, {kMoYe, kAshuna, kMonk, kShaman, kChixiao}, 180};
    }
    if (id == "ss05") {
        return {id, YGO_M3_SS05_SETUP, {kChixiao, kBalelynx, kSanctuary}, 100};
    }
    if (id == "int01") {
        return {id, YGO_M3_INT01_SETUP, {kAsh, kChixiao, kEmergence, kMoYe}, 100};
    }
    if (id == "int02") {
        return {id, YGO_M3_INT02_SETUP, {kVeiler, kChixiao, kEmergence, kMoYe}, 100};
    }
    if (id == "int03") {
        return {id, YGO_M3_INT03_SETUP, {kImpermanence, kChixiao, kEmergence, kMoYe}, 100};
    }
    if (id == "int04") {
        return {id, YGO_M3_INT04_SETUP, {kCalledBy, kGhostBelle, kAsh}, 100};
    }
    if (id == "int05") {
        return {id, YGO_M3_INT05_SETUP, {kCalledBy, kAsh}, 100};
    }
    if (id == "sg01") {
        return {id, YGO_M3_SG01_SETUP, {kFire, kBalelynx}, 120};
    }
    if (id == "sg02") {
        return {id, YGO_M3_SG02_SETUP, {kBalelynx, kSanctuary, kFire}, 100, true};
    }
    if (id == "sg03") {
        return {id, YGO_M3_SG03_SETUP, {kSanctuary, kBalelynx, kFire}, 75, true};
    }
    if (id == "sg04") {
        return {id, YGO_M3_SG04_SETUP, {kSanctuary, kBalelynx, kFire}, 100, true};
    }
    if (id == "ss10") {
        return {id, YGO_M3_SS10_SETUP, {kBlackout}, 140};
    }
    if (id == "ss10_banish") {
        return {id, YGO_M3_SS10_BANISH_SETUP, {kRage, kChengying, kBlackout, kToken}, 180};
    }
    if (id == "ss12") {
        return {id, YGO_M3_SS12_SETUP, {kCircle, kVishuda, kAshuna, kMoYe}, 100};
    }
    if (id == "ss14") {
        return {id, YGO_M3_SS14_SETUP, {kAshuna, kVishuda, kAdhara, kBalelynx}, 420};
    }
    if (id == "ss15") {
        return {id, YGO_M3_SS15_SETUP, {kVishuda, kFire}, 100};
    }
    if (id == "ss16") {
        return {id, YGO_M3_SS16_SETUP, {kAdhara, kVishuda}, 100};
    }
    if (id == "ss16_chengying") {
        return {id, YGO_M3_SS16_CHENGYING_SETUP, {kRage, kChengying, kBlackout, kToken}, 180};
    }
    if (id == "ss17") {
        return {id, YGO_M3_SS17_SETUP, {kRage, kQixing, kBlackout}, 240};
    }
    if (id == "sg05") {
        return {id, YGO_M3_SG05_SETUP, {kSpinny, kGazelle, kFoxy, kFire, kSanctuary}, 180};
    }
    if (id == "sg06") {
        return {id, YGO_M3_SG06_SETUP, {kFoxy, kSpinny, kGazelle, kFire, kSanctuary}, 180};
    }
    if (id == "sg07_jack") {
        return {id, YGO_M3_SG07_JACK_SETUP, {kJackJaguar, kSpinny, kBalelynx}, 180};
    }
    if (id == "sg07_weasel") {
        return {id, YGO_M3_SG07_WEASEL_SETUP, {kBalelynx, kWeasel, kFire, kSpinny}, 240, true};
    }
    if (id == "sg07_falco") {
        return {id, YGO_M3_SG07_FALCO_SETUP, {kFalco, kSpinny}, 180, true};
    }
    if (id == "sg08_real") {
        return {id, YGO_M3_SG08_REAL_SETUP, {kMiragestallio, kFire, kSpinny, kFoxy}, 320};
    }
    if (id == "sg09_direct") {
        return {id, YGO_M3_SG09_DIRECT_SETUP, {kMiragestallio, kFoxy, kFire, kSpinny}, 400};
    }
    if (id == "sg11") {
        return {id, YGO_M3_SG11_PLAYER0_SETUP, {kSunlightWolf, kFire, kPrometheanPrincess}, 180, true};
    }
    if (id == "sg12") {
        return {id, YGO_M3_SG12_SETUP, {kSanctuary, kSunlightWolf, kRage, kFire, kSpinny}, 240, true};
    }
    if (id == "sg13") {
        return {id, YGO_M3_SG13_SETUP, {kSanctuary, kRagingPhoenix, kFoxy, kFire, kSpinny}, 240, true};
    }
    if (id == "sg14") {
        return {id, YGO_M3_SG14_SETUP, {kSanctuary, kPyroPhoenix, kMoYe, kFire, kSpinny}, 260, true};
    }
    if (id == "sg15") {
        return {id, YGO_M3_SG15_SETUP, {kSanctuary, kHeatleo, kBlackout, kFire, kSpinny}, 240, true};
    }
    if (id == "sg18") {
        return {id, YGO_M3_SG18_SETUP, {kPrometheanPrincess, kFire, kSpinny}, 240, true};
    }
    if (id == "sg19") {
        return {id, YGO_M3_SG19_SETUP, {kHiita, kFire, kSpinny}, 220};
    }
    if (id == "sg19_no_target") {
        return {id, YGO_M3_SG19_NO_TARGET_SETUP, {kHiita, kFire, kSpinny}, 180};
    }
    if (id == "sg16_negate") {
        return {id, YGO_M3_SG16_NEGATE_SETUP, {kRoar, kCalledBy, kBalelynx}, 160};
    }
    if (id == "sg16_recovery") {
        return {id, YGO_M3_SG16_RECOVERY_SETUP, {kRoar, kBalelynx, kSanctuary, kFire}, 260, true};
    }
    if (id == "sg17") {
        return {id, YGO_M3_SG17_SETUP, {kRage, kBalelynx, kFire}, 180, true};
    }
    if (id == "btl01") {
        return {id, YGO_M3_BTL01_SETUP, {kRagingPhoenix, kFire}, 500};
    }
    if (id == "sg20") {
        return {id, YGO_M3_SG20_SETUP, {kSunlightWolf, kFire, kSpinny}, 180};
    }
    throw std::runtime_error("unknown M3 fixture: " + id);
}

bool validate_specialized_observation(const std::string& id,
                                      const ygo::observation::PlayerObservation& observation,
                                      Evidence& evidence) {
    inspect_observation(observation, evidence);
    const auto xyz_material_count_for = [&](const ygo::observation::ObservedCard& parent) {
        return static_cast<std::uint32_t>(std::count_if(
            observation.relationships.begin(), observation.relationships.end(),
            [&](const auto& relationship) {
                return relationship.kind == ygo::observation::RelationshipKind::XyzMaterial &&
                       relationship.target == parent.locator;
            }));
    };
    if (id == "ss01" || id == "ss06") {
        if (id == "ss06" && find_card(observation, kLongyuan,
                                      ygo::observation::SemanticZone::Graveyard) != nullptr) {
            evidence.longyuan_discarded = true;
        }
        const auto* token = find_card(observation, kToken, ygo::observation::SemanticZone::MonsterZone);
        if (token != nullptr) {
            require(token->controller.value_or(255) == 0 && token->current.has_value(),
                    id + " token observation lacks controller/current properties");
            require(token->current->level.value_or(0) == 4 && token->current->attack.value_or(-1) == 0 &&
                        token->current->defense.value_or(-1) == 0,
                    id + " token level/ATK/DEF projection is incorrect");
            require((token->current->type.value_or(0) & TYPE_TUNER) != 0,
                    id + " token Tuner type was not projected");
            evidence.token_state_valid = true;
        }
        const auto* chixiao = find_card(observation, kChixiao, ygo::observation::SemanticZone::MonsterZone);
        if (chixiao != nullptr) {
            require(chixiao->current.has_value(), id + " Chixiao observation lacks current properties");
            require(chixiao->current->level.value_or(0) == 8,
                    id + " Chixiao level projection is incorrect");
            evidence.chixiao_state_valid = true;
        }
        if (id == "ss01" && find_card(observation, kEmergence,
                                      ygo::observation::SemanticZone::Hand) != nullptr) {
            evidence.chixiao_search_state_valid = true;
        }
        if (id == "ss06") {
            const auto* qixing = find_card(observation, kQixing,
                                           ygo::observation::SemanticZone::MonsterZone);
            if (qixing != nullptr) {
                require(qixing->current.has_value(), "ss06 Qixing observation lacks current properties");
                require(qixing->current->level.value_or(0) == 10,
                        "ss06 Qixing level projection is incorrect");
                evidence.qixing_state_valid = true;
            }
        }
        return evidence.token_state_valid && evidence.chixiao_state_valid;
    }
    if (id == "ss10_banish" || id == "ss16_chengying") {
        const auto* chengying = find_card(observation, kChengying,
                                          ygo::observation::SemanticZone::MonsterZone);
        if (chengying != nullptr) {
            require(chengying->current.has_value(), id + " Chengying observation lacks current properties");
            require(chengying->current->attack.has_value(), id + " Chengying attack is not observable");
            evidence.chengying_state_valid = true;
            if (!evidence.blackout_banished_from_grave) {
                evidence.chengying_attack_before_banish = chengying->current->attack.value();
            } else {
                evidence.chengying_attack_after_banish = chengying->current->attack.value();
                evidence.chengying_dynamic_state_valid =
                    evidence.chengying_attack_after_banish > evidence.chengying_attack_before_banish;
            }
        }
        const auto* token = find_card(observation, kToken,
                                      ygo::observation::SemanticZone::MonsterZone);
        if (token != nullptr) {
            require(token->owner.value_or(255) == 0 && token->controller.value_or(255) == 0,
                    id + " Blackout token owner/controller projection is incorrect");
            require(token->current.has_value() && token->current->level.value_or(0) == 4 &&
                        token->current->attack.value_or(-1) == 0 && token->current->defense.value_or(-1) == 0 &&
                        (token->current->type.value_or(0) & TYPE_TUNER) != 0,
                    id + " Blackout token properties are incorrect");
            evidence.blackout_token_state_valid = true;
        }
        return evidence.chengying_state_valid && evidence.blackout_token_state_valid &&
               evidence.blackout_banished_from_grave && evidence.blackout_banish_trigger_activated;
    }
    if (id == "ss17") {
        const auto* qixing = find_card(observation, kQixing,
                                       ygo::observation::SemanticZone::MonsterZone);
        if (qixing != nullptr) {
            require(qixing->current.has_value() && qixing->current->level.value_or(0) == 10,
                    "ss17 Qixing observation is incomplete");
            evidence.qixing_state_valid = true;
        }
        return evidence.qixing_state_valid && evidence.qixing_interaction_activated &&
               evidence.qixing_interaction_damage && evidence.qixing_interaction_chain_resolved;
    }
    if (id == "sg01") {
        const auto* balelynx = find_card(observation, kBalelynx,
                                         ygo::observation::SemanticZone::MonsterZone);
        if (balelynx == nullptr) {
            return evidence.balelynx_state_valid;
        }
        require(balelynx->current.has_value(), "sg01 Balelynx observation lacks current properties");
        require(balelynx->current->link_rating.value_or(0) == 1,
                "sg01 Balelynx Link rating projection is incorrect");
        evidence.balelynx_state_valid = true;
        return true;
    }
    if (id == "ss05") {
        const auto* chixiao = find_card(observation, kChixiao,
                                        ygo::observation::SemanticZone::MonsterZone);
        if (chixiao != nullptr) {
            require(chixiao->current.has_value(), "ss05 Chixiao observation lacks current properties");
            require(chixiao->current->level.value_or(0) == 8,
                    "ss05 Chixiao level projection is incorrect");
            evidence.chixiao_state_valid = true;
        }
        const auto* balelynx = find_card(observation, kBalelynx,
                                         ygo::observation::SemanticZone::MonsterZone);
        if (balelynx != nullptr) {
            require(balelynx->current.has_value(), "ss05 Balelynx observation lacks current properties");
            require(balelynx->current->link_rating.value_or(0) == 1,
                    "ss05 Balelynx Link rating projection is incorrect");
            evidence.balelynx_state_valid = true;
        }
        return evidence.chixiao_state_valid && evidence.balelynx_state_valid;
    }
    if (id == "ss12") {
        if (find_card(observation, kVishuda, ygo::observation::SemanticZone::Hand) != nullptr) {
            evidence.circle_search_state_valid = true;
        }
        return evidence.circle_search_state_valid;
    }
    if (id == "ss14") {
        if (find_card(observation, kVishuda, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.ashuna_deck_state_valid = true;
        }
        return evidence.ashuna_deck_state_valid;
    }
    if (id == "ss15") {
        return evidence.vishuda_target_selected;
    }
    if (id == "ss16") {
        if (find_card(observation, kVishuda, ygo::observation::SemanticZone::Hand) != nullptr) {
            evidence.adhara_recovery_state_valid = true;
        }
        return evidence.adhara_recovery_state_valid;
    }
    if (id == "ss08") {
        const auto* baxia = find_card(observation, kBaxia,
                                      ygo::observation::SemanticZone::MonsterZone);
        if (baxia != nullptr) {
            require(baxia->current.has_value(), "ss08 Baxia observation lacks current properties");
            require(baxia->current->level.value_or(0) == 8,
                    "ss08 Baxia level projection is incorrect");
            evidence.baxia_summoned = true;
        }
        return evidence.baxia_summoned;
    }
    if (id == "ss07") {
        const auto* token = find_card(observation, kToken,
                                      ygo::observation::SemanticZone::MonsterZone);
        if (token != nullptr) {
            evidence.token_state_valid = true;
        }
        if (token == nullptr &&
            find_card(observation, kChixiao, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.token_restriction_expired = true;
        }
        return evidence.token_state_valid && evidence.token_restriction_expired;
    }
    if (id == "ss12_condition") {
        if (find_card(observation, kToken, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.token_state_valid = true;
        }
        if (find_card(observation, kAshuna, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.tenyi_condition_true_ashuna_selected = true;
        }
        return evidence.token_state_valid && evidence.tenyi_condition_true_ashuna_selected;
    }
    if (id == "ss18") {
        if (find_card(observation, kMonk, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.monk_state_valid = true;
        }
        if (find_card(observation, kShaman, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.shaman_state_valid = true;
        }
        return evidence.monk_state_valid && evidence.shaman_state_valid;
    }
    if (id == "sg05") {
        if (find_card(observation, kSpinny, ygo::observation::SemanticZone::Graveyard) != nullptr) {
            evidence.spinny_discarded = true;
            evidence.gazelle_deck_state_valid = true;
        }
        return evidence.gazelle_deck_state_valid;
    }
    if (id == "sg06") {
        if (find_card(observation, kFoxy, ygo::observation::SemanticZone::Graveyard) != nullptr) {
            evidence.gazelle_foxy_gy_state_valid = true;
        }
        return evidence.gazelle_foxy_gy_state_valid;
    }
    if (id == "sg07_jack") {
        const auto* link = find_card(observation, kBalelynx,
                                     ygo::observation::SemanticZone::MonsterZone);
        const auto* jack = find_card(observation, kJackJaguar,
                                     ygo::observation::SemanticZone::Graveyard);
        if (link != nullptr && jack != nullptr) {
            evidence.jack_link_condition_observed = true;
        }
        return evidence.jack_link_condition_observed && evidence.jack_candidate_present_under_mr5;
    }
    if (id == "sg07_weasel") {
        if (find_card(observation, kWeasel, ygo::observation::SemanticZone::MainDeck) != nullptr ||
            find_card(observation, kWeasel, ygo::observation::SemanticZone::Graveyard) != nullptr) {
            evidence.weasel_state_valid = true;
        }
        if (find_card(observation, kFire, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.weasel_target_summoned = true;
        }
        return evidence.weasel_state_valid && evidence.weasel_recycled &&
               evidence.weasel_target_summoned;
    }
    if (id == "sg07_falco") {
        if (find_card(observation, kFalco, ygo::observation::SemanticZone::MonsterZone) != nullptr) {
            evidence.falco_state_valid = true;
        }
        return evidence.falco_state_valid && evidence.falco_summoned && evidence.falco_target_recovered;
    }
    if (id == "sg11") {
        const auto* wolf = find_card(observation, kSunlightWolf,
                                     ygo::observation::SemanticZone::MonsterZone);
        if (wolf != nullptr) {
            require(wolf->current.has_value() && wolf->current->link_rating.value_or(0) == 2,
                    "sg11 Sunlight Wolf observation is incomplete");
            evidence.wolf_state_valid = true;
        }
        return evidence.wolf_state_valid && evidence.wolf_fire_trigger_activated &&
               evidence.wolf_fire_target_selected && evidence.wolf_fire_recovered;
    }
    if (id == "sg12") {
        const auto* wolf = find_card(observation, kSunlightWolf,
                                     ygo::observation::SemanticZone::MonsterZone);
        if (wolf != nullptr) {
            require(wolf->current.has_value() && wolf->current->link_rating.value_or(0) == 2,
                    "sg12 Sunlight Wolf observation is incomplete");
            evidence.wolf_state_valid = true;
        }
        return evidence.wolf_state_valid;
    }
    if (id == "sg13") {
        const auto* phoenix = find_card(observation, kRagingPhoenix,
                                        ygo::observation::SemanticZone::MonsterZone);
        if (phoenix != nullptr) {
            require(phoenix->current.has_value() && phoenix->current->link_rating.value_or(0) == 4,
                    "sg13 Raging Phoenix observation is incomplete");
            evidence.raging_phoenix_state_valid = true;
        }
        return evidence.raging_phoenix_state_valid;
    }
    if (id == "sg14") {
        const auto* phoenix = find_card(observation, kPyroPhoenix,
                                        ygo::observation::SemanticZone::MonsterZone);
        if (phoenix != nullptr) {
            require(phoenix->current.has_value() && phoenix->current->link_rating.value_or(0) == 4,
                    "sg14 Pyro Phoenix observation is incomplete");
            evidence.pyro_phoenix_state_valid = true;
        }
        return evidence.pyro_phoenix_state_valid;
    }
    if (id == "sg15") {
        const auto* heatleo = find_card(observation, kHeatleo,
                                        ygo::observation::SemanticZone::MonsterZone);
        if (heatleo != nullptr) {
            require(heatleo->current.has_value() && heatleo->current->link_rating.value_or(0) == 3,
                    "sg15 Heatleo observation is incomplete");
            evidence.heatleo_state_valid = true;
        }
        return evidence.heatleo_state_valid;
    }
    if (id == "sg18") {
        const auto* princess = find_card(observation, kPrometheanPrincess,
                                         ygo::observation::SemanticZone::MonsterZone);
        if (princess != nullptr) {
            require(princess->current.has_value() && princess->current->link_rating.value_or(0) == 3,
                    "sg18 Promethean Princess observation is incomplete");
            evidence.promethean_state_valid = true;
        }
        return evidence.promethean_state_valid;
    }
    if (id == "sg19" || id == "sg19_no_target") {
        const auto* hiita = find_card(observation, kHiita,
                                      ygo::observation::SemanticZone::MonsterZone);
        if (hiita != nullptr) {
            require(hiita->current.has_value() && hiita->current->link_rating.value_or(0) == 2,
                    id + " Hiita observation is incomplete");
            evidence.hiita_state_valid = true;
        }
        return evidence.hiita_state_valid;
    }
    if (id == "sg16_negate" || id == "sg16_recovery") {
        const auto* link = find_card(observation, kBalelynx,
                                     ygo::observation::SemanticZone::MonsterZone);
        if (link != nullptr) {
            require(link->current.has_value() && link->current->link_rating.value_or(0) == 1,
                    id + " Balelynx observation is incomplete");
            evidence.roar_state_valid = true;
        }
        const bool roar_visible = find_card(observation, kRoar,
                                            ygo::observation::SemanticZone::SpellTrapZone) != nullptr ||
                                  find_card(observation, kRoar,
                                            ygo::observation::SemanticZone::Graveyard) != nullptr;
        return evidence.roar_state_valid && roar_visible;
    }
    if (id == "sg17") {
        const auto* link = find_card(observation, kBalelynx,
                                     ygo::observation::SemanticZone::MonsterZone);
        if (link != nullptr) {
            require(link->current.has_value() && link->current->link_rating.value_or(0) == 1,
                    "sg17 Balelynx observation is incomplete");
            evidence.rage_state_valid = true;
        }
        return evidence.rage_state_valid;
    }
    if (id == "sg08_real" || id == "sg09_direct") {
        const auto* xyz = find_card(observation, kMiragestallio,
                                    ygo::observation::SemanticZone::MonsterZone);
        if (xyz != nullptr) {
            require(xyz->current.has_value() && xyz->current->rank.value_or(0) == 3,
                    "sg08_real Miragestallio rank projection is incorrect");
            evidence.xyz_state_valid = true;
            evidence.xyz_summoned = true;
            const auto count = xyz_material_count_for(*xyz);
            if (count >= 2 && evidence.xyz_material_count_before == 0) {
                evidence.xyz_material_count_before = count;
            }
            if (evidence.xyz_detach_accepted && count < evidence.xyz_material_count_before) {
                evidence.xyz_material_count_after = count;
            }
            if (evidence.xyz_effect_activated && count < evidence.xyz_material_count_before) {
                evidence.xyz_detach_accepted = true;
                evidence.xyz_material_count_after = count;
            }
            for (const auto& relationship : observation.relationships) {
                if (relationship.kind != ygo::observation::RelationshipKind::XyzMaterial ||
                    relationship.target != xyz->locator) {
                    continue;
                }
                const auto source = std::find_if(
                    observation.entities.begin(), observation.entities.end(),
                    [&](const auto& entity) { return entity.locator == relationship.source; });
                if (source == observation.entities.end() ||
                    source->zone != ygo::observation::SemanticZone::Overlay) {
                    continue;
                }
                if (source->identity_known && source->passcode.has_value()) {
                    evidence.xyz_material_identity_verified = true;
                } else {
                    evidence.xyz_material_identity_redacted = true;
                }
            }
        }
        if (id == "sg09_direct") {
            return evidence.xyz_state_valid && evidence.xyz_material_count_before >= 2 &&
                   evidence.xyz_detach_accepted && evidence.xyz_deck_summon_selected &&
                   evidence.xyz_deck_summoned;
        }
        return evidence.xyz_state_valid && evidence.xyz_material_count_before >= 2;
    }
    if (id == "sg20") {
        const auto* wolf = find_card(observation, kSunlightWolf,
                                     ygo::observation::SemanticZone::MonsterZone);
        if (wolf != nullptr) {
            require(wolf->current.has_value(), "sg20 Sunlight Wolf observation lacks current properties");
            require(wolf->current->link_rating.value_or(0) == 2,
                    "sg20 Sunlight Wolf Link rating projection is incorrect");
            return true;
        }
        return false;
    }
    if (id == "int01" || id == "int02" || id == "int03" || id == "int04" || id == "int05") {
        return true;
    }
    if (id == "sg02" || id == "sg03" || id == "sg04") {
        const auto* balelynx = find_card(observation, kBalelynx,
                                         ygo::observation::SemanticZone::MonsterZone);
        if (balelynx != nullptr) {
            require(balelynx->current.has_value(), "sg02 Balelynx observation lacks current properties");
            require(balelynx->current->link_rating.value_or(0) == 1,
                    "sg02 Balelynx Link rating projection is incorrect");
            evidence.balelynx_state_valid = true;
        }
        const auto* sanctuary_hand = find_card(observation, kSanctuary,
                                               ygo::observation::SemanticZone::Hand);
        const auto* sanctuary_field = find_card(observation, kSanctuary,
                                                ygo::observation::SemanticZone::FieldZone);
        if (sanctuary_hand != nullptr || sanctuary_field != nullptr) {
            evidence.sanctuary_state_valid = true;
        }
        return id == "sg02" ? evidence.balelynx_state_valid && evidence.sanctuary_state_valid
                            : evidence.sanctuary_state_valid;
    }
    return true;
}

Evidence run_fixture(const FixtureSpec& spec) {
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M3_DECK_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M3_DECK_B);
    const auto& seat_zero_deck = spec.mirror_seats ? deck_b : deck_a;
    const auto& seat_one_deck = spec.mirror_seats ? deck_a : deck_b;
    auto required_scripts = ygo::core::canonical_required_script_codes(deck_a, deck_b);
    auto config = make_config(required_scripts);
    require(config.duel_flags == ygo::m3::canonical_rules().duel_flags,
            "M3 fixture did not use the canonical duel mode");
    if (spec.id == "btl01") {
        // Use a distinct legal fixture seed so the combat setup reaches both
        // battle destruction and the terminal result before unrelated engine
        // branches; the pinned rules and scripts remain unchanged.
        constexpr std::uint64_t battle_seed = 1;
        config.seed = ygo::core::derive_seed_bundle(battle_seed);
    }
    ygo::core::CoreHost host(config);
    host.load_deck(0, seat_zero_deck);
    host.load_deck(1, seat_one_deck);
    host.start_duel();
    host.load_fixture_script(spec.setup);

    ygo::observation::ObservationSession sessions[] = {
        ygo::observation::ObservationSession(0, static_cast<std::uint32_t>(config.duel_flags)),
        ygo::observation::ObservationSession(1, static_cast<std::uint32_t>(config.duel_flags)),
    };
    ygo::m3::DeterministicConformancePolicy policy(spec.focus_codes);
    Evidence evidence;
    bool specialized_state_checked = false;
    bool baxia_trigger_selection_open = false;
    bool sg11_fire_place_pending = false;

    for (std::uint32_t engine_step = 0; engine_step < spec.max_steps; ++engine_step) {
        const auto result = host.process();
        sessions[0].ingest(result.message, engine_step);
        sessions[1].ingest(result.message, engine_step);
        if (spec.id == "ss06") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::LifePointsChanged &&
                        event.amount.value_or(0) == -1200) {
                        evidence.longyuan_burn = true;
                    }
                    if (event.public_passcode.value_or(0) == kLongyuan &&
                        event.from_zone == ygo::observation::SemanticZone::Hand &&
                        event.to_zone == ygo::observation::SemanticZone::Graveyard) {
                        evidence.longyuan_discarded = true;
                    }
                }
            }
        }
        if (spec.id == "ss10") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed &&
                        event.public_passcode.has_value()) {
                        evidence.blackout_destroyed_codes.insert(*event.public_passcode);
                    }
                }
            }
        }
        if (spec.id == "ss10_banish" || spec.id == "ss16_chengying") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardBanished &&
                        event.public_passcode.value_or(0) == kBlackout &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::Banished) {
                        evidence.blackout_banished_from_grave = true;
                    }
                }
            }
        }
        if (spec.id == "ss17") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardBanished &&
                        (event.public_passcode.value_or(0) == kRage ||
                         event.public_passcode.value_or(0) == kBlackout) &&
                        event.from_zone == ygo::observation::SemanticZone::SpellTrapZone &&
                        event.to_zone == ygo::observation::SemanticZone::Banished) {
                        evidence.qixing_rage_banished = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::LifePointsChanged &&
                        event.amount.value_or(0) == -1200) {
                        evidence.qixing_interaction_damage = true;
                    }
                    if (evidence.qixing_interaction_activated &&
                        event.kind == ygo::observation::VisibleEventKind::ChainResolved) {
                        evidence.qixing_interaction_chain_resolved = true;
                    }
                }
            }
        }
        if (spec.id == "ss14" && evidence.ashuna_activated) {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::PhaseChanged &&
                        event.engine_step_index > evidence.ashuna_activation_engine_step) {
                        evidence.ashuna_phase_boundary_after_activation = true;
                    }
                }
            }
        }
        if (spec.id == "sg07_jack") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kJackJaguar &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.jack_jaguar_summoned = true;
                    }
                    if (event.public_passcode.value_or(0) == kSpinny &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MainDeck) {
                        evidence.jack_jaguar_target_recycled = true;
                    }
                }
            }
        }
        if (spec.id == "sg07_weasel") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kWeasel &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MainDeck) {
                        evidence.weasel_recycled = true;
                    }
                    if (event.public_passcode.value_or(0) == kFire &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.weasel_target_summoned = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::Draw &&
                        event.engine_step_index > evidence.weasel_trigger_engine_step) {
                        evidence.weasel_drawn = true;
                    }
                }
            }
        }
        if (spec.id == "sg07_falco") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kFalco &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.falco_summoned = true;
                    }
                    if (event.public_passcode.value_or(0) == kSpinny &&
                        event.from_zone == ygo::observation::SemanticZone::MonsterZone &&
                        event.to_zone == ygo::observation::SemanticZone::Hand) {
                        evidence.falco_target_recovered = true;
                    }
                }
            }
        }
        if (spec.id == "sg11") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kFire &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::Hand) {
                        evidence.wolf_fire_recovered = true;
                    }
                }
            }
        }
        if (spec.id == "sg12") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kRage &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::Hand) {
                        evidence.wolf_st_recovered = true;
                    }
                }
            }
        }
        if (spec.id == "sg13") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::ChainResolved &&
                        evidence.raging_phoenix_trigger_activated) {
                        evidence.raging_phoenix_search_resolved = true;
                    }
                    if (event.public_passcode.value_or(0) == kFoxy &&
                        event.from_zone == ygo::observation::SemanticZone::MainDeck &&
                        event.to_zone == ygo::observation::SemanticZone::Hand) {
                        evidence.raging_phoenix_search_resolved = true;
                    }
                }
            }
        }
        if (spec.id == "sg14") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed &&
                        event.public_passcode.value_or(0) == kMoYe) {
                        evidence.pyro_phoenix_destroyed_opponent = true;
                    }
                    if (event.public_passcode.value_or(0) == kBalelynx &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.pyro_phoenix_revived_link = true;
                    }
                }
            }
        }
        if (spec.id == "sg15") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.player.value_or(2) == 1 &&
                        event.from_zone == ygo::observation::SemanticZone::SpellTrapZone &&
                        event.to_zone == ygo::observation::SemanticZone::MainDeck) {
                        const auto code = event.public_passcode.value_or(0);
                        if (!event.public_passcode.has_value() || code == kBlackout) {
                            evidence.heatleo_target_returned = true;
                        }
                    }
                }
            }
        }
        if (spec.id == "sg18") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kFire &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.promethean_fire_revived = true;
                    }
                }
            }
        }
        if (spec.id == "sg19") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kFire &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.hiita_revived = true;
                    }
                }
            }
        }
        if (spec.id == "sg09_direct" && evidence.xyz_effect_activated) {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::PhaseChanged &&
                        evidence.xyz_fire_restriction_blocked &&
                        event.engine_step_index > evidence.xyz_effect_engine_step) {
                        evidence.xyz_restriction_phase_boundary = true;
                    }
                }
            }
        }
        if (spec.id == "sg16_negate") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed &&
                        event.public_passcode.value_or(0) == kCalledBy) {
                        evidence.roar_negate_target_destroyed = true;
                    }
                    if (evidence.roar_negate_activated &&
                        event.kind == ygo::observation::VisibleEventKind::ChainResolved) {
                        evidence.roar_negate_chain_resolved = true;
                    }
                }
            }
        }
        if (spec.id == "sg16_recovery") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kRoar &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::SpellTrapZone) {
                        evidence.roar_recovery_set = true;
                    }
                }
            }
        }
        if (spec.id == "sg17") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kBalelynx &&
                        event.from_zone == ygo::observation::SemanticZone::MonsterZone &&
                        event.to_zone == ygo::observation::SemanticZone::Graveyard) {
                        evidence.rage_cost_sent = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed &&
                        event.public_passcode.value_or(0) == kAsh) {
                        evidence.rage_target_destroyed = true;
                    }
                }
            }
        }
        if (spec.id == "sg08_real" || spec.id == "sg09_direct") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.public_passcode.value_or(0) == kFoxy &&
                        event.from_zone == ygo::observation::SemanticZone::MainDeck &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.xyz_deck_summoned = true;
                    }
                    if (event.from_zone == ygo::observation::SemanticZone::Overlay &&
                        event.to_zone == ygo::observation::SemanticZone::Graveyard) {
                        evidence.xyz_detach_accepted = true;
                        if (event.public_passcode.has_value()) {
                            evidence.xyz_detached_code_known = true;
                            evidence.xyz_detached_code = *event.public_passcode;
                        }
                    }
                }
            }
        }
        if (spec.id == "ss01") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::ChainResolved) {
                        evidence.trigger_chain_resolved = true;
                    }
                }
            }
        }
        if (spec.id == "ss05" && evidence.chixiao_negation_activated) {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::ChainResolved) {
                        evidence.chixiao_negation_chain_resolved = true;
                    }
                }
            }
        }
        if ((spec.id == "int01" || spec.id == "int02" || spec.id == "int03" || spec.id == "int04" || spec.id == "int05") &&
            evidence.interaction_chain_activated) {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::ChainResolved) {
                        evidence.interaction_chain_resolved = true;
                    }
                }
            }
        }
        if (spec.id == "int04" && evidence.ghost_belle_chain_activated) {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::ChainResolved) {
                        evidence.ghost_belle_chain_resolved = true;
                    }
                }
            }
        }
        if ((spec.id == "sg05" || spec.id == "sg06") && evidence.gazelle_hand_activated) {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::ChainResolved) {
                        evidence.gazelle_chain_resolved = true;
                    }
                }
            }
        }
        if (spec.id == "sg05") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardMoved &&
                        event.public_passcode.value_or(0) == kSpinny &&
                        event.from_zone == ygo::observation::SemanticZone::Hand &&
                        event.to_zone == ygo::observation::SemanticZone::Graveyard) {
                        evidence.spinny_discarded = true;
                    }
                }
            }
        }
        if (spec.id == "sg06") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardMoved &&
                        event.public_passcode.value_or(0) == kSpinny &&
                        event.from_zone == ygo::observation::SemanticZone::Hand &&
                        event.to_zone == ygo::observation::SemanticZone::Graveyard) {
                        evidence.spinny_discarded = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::CardMoved &&
                        event.public_passcode.value_or(0) == kFoxy &&
                        event.from_zone == ygo::observation::SemanticZone::MainDeck &&
                        event.to_zone == ygo::observation::SemanticZone::Graveyard) {
                        evidence.gazelle_foxy_gy_state_valid = true;
                    }
                }
            }
        }
        if (spec.id == "btl01") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::LifePointsChanged) {
                        evidence.battle_life_points_changed = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed) {
                        evidence.battle_card_destroyed = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::Targeted) {
                        evidence.battle_target_selected = true;
                    }
                }
            }
        }
        if (spec.id == "ss08") {
            for (const auto& session : sessions) {
                for (const auto& event : session.visible_events()) {
                    if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed &&
                        event.public_passcode.value_or(0) == kBaxia) {
                        evidence.baxia_destroyed = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::CardMoved &&
                        (event.public_passcode.value_or(0) == kMoYe ||
                         event.public_passcode.value_or(0) == kTaia) &&
                        event.from_zone == ygo::observation::SemanticZone::Graveyard &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone) {
                        evidence.baxia_revived = true;
                    }
                    if (event.kind == ygo::observation::VisibleEventKind::Summoned &&
                        (event.public_passcode.value_or(0) == kMoYe ||
                         event.public_passcode.value_or(0) == kTaia) &&
                        event.to_zone == ygo::observation::SemanticZone::MonsterZone &&
                        evidence.baxia_ignition_activated) {
                        evidence.baxia_revived = true;
                    }
                }
            }
        }
        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        require(!decoded.retry, spec.id + " emitted MSG_RETRY");
        evidence.engine_steps = engine_step + 1;

        if (decoded.terminal) {
            evidence.terminal = true;
            evidence.winner = decoded.winner;
            if (spec.id == "btl01") {
                evidence.battle_terminal = true;
            }
            break;
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        require(decoded.decisions.size() == 1, spec.id + " emitted multiple interactive decisions");
        auto request = decoded.decisions.front();
        evidence.decision_families.insert(request.engine_message_name);
        ++evidence.decisions;

        for (;;) {
            ygo::protocol::validate_candidate_set(request);
            ygo::observation::ObservationBuildConfig observation_config;
            observation_config.decision_index = evidence.decisions;
            observation_config.engine_step_index = request.engine_step_index;
            observation_config.visible_events = sessions[request.player].visible_events();
            observation_config.knowledge.own_decklist_known = true;
            observation_config.knowledge.opponent_decklist_known = true;
            observation_config.own_deck.known = true;
            observation_config.own_deck.main_deck = request.player == 0 ? seat_zero_deck.main_deck : seat_one_deck.main_deck;
            observation_config.own_deck.extra_deck = request.player == 0 ? seat_zero_deck.extra_deck : seat_one_deck.extra_deck;
            observation_config.opponent_deck.known = true;
            observation_config.opponent_deck.main_deck = request.player == 0 ? seat_one_deck.main_deck : seat_zero_deck.main_deck;
            observation_config.opponent_deck.extra_deck = request.player == 0 ? seat_one_deck.extra_deck : seat_zero_deck.extra_deck;
            auto observation = ygo::observation::build_player_observation(host, request.player,
                                                                           observation_config);
            ygo::observation::attach_decision_context(observation, request);
            if (spec.id == "sg08_real" || spec.id == "sg09_direct") {
                inspect_xyz_public_query(host, evidence);
            }
            validate_observation_candidates(observation, request, spec.id == "sg09_direct");
            const bool balelynx_on_field = find_card(
                observation, kBalelynx, ygo::observation::SemanticZone::MonsterZone) != nullptr;
            if (spec.id == "sg07_jack" && request.player == 1 &&
                request.engine_message_type == MSG_SELECT_PLACE &&
                std::any_of(request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                    return candidate.source_location == LOCATION_MZONE && candidate.source_sequence >= 5;
                })) {
                evidence.jack_emzone_place_domain = true;
            }
            if (spec.id == "sg07_jack" && request.player == 1 &&
                request.engine_message_type == MSG_SELECT_IDLECMD &&
                find_card(observation, kBalelynx,
                          ygo::observation::SemanticZone::MonsterZone) != nullptr &&
                find_card(observation, kJackJaguar,
                          ygo::observation::SemanticZone::Graveyard) != nullptr) {
                evidence.jack_link_condition_observed = true;
                const bool jack_candidate = std::any_of(
                    request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                        return candidate_is_code(candidate, kJackJaguar) &&
                               candidate.source_location == LOCATION_GRAVE;
                    });
                if (jack_candidate) {
                    evidence.jack_candidate_present_under_mr5 = true;
                }
            }
            if (spec.id == "sg09_direct" &&
                (request.engine_message_type == MSG_SELECT_CHAIN ||
                 request.engine_message_type == MSG_SELECT_IDLECMD) &&
                request.player == 1) {
                const bool has_vishuda_candidate = std::any_of(
                    request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                        return candidate.source_card == kVishuda &&
                               candidate.source_location == LOCATION_GRAVE;
                    });
                const bool vishuda_visible =
                    find_card(observation, kVishuda,
                              ygo::observation::SemanticZone::Graveyard) != nullptr;
                if (!evidence.xyz_deck_summon_selected && has_vishuda_candidate) {
                    evidence.xyz_fire_restriction_seen = true;
                }
                if (evidence.xyz_deck_summon_selected && !evidence.xyz_restriction_phase_boundary &&
                    !has_vishuda_candidate && vishuda_visible) {
                    evidence.xyz_fire_restriction_blocked = true;
                }
                if (evidence.xyz_restriction_phase_boundary && has_vishuda_candidate) {
                    evidence.xyz_fire_restriction_expired = true;
                }
            }
            if (spec.id == "sg19_no_target" && request.engine_message_type == MSG_SELECT_IDLECMD) {
                const bool has_hiita_activation = std::any_of(
                    request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                        return candidate.source_card == kHiita && candidate.source_location == LOCATION_MZONE &&
                               candidate.phase == 5;
                    });
                if (!has_hiita_activation) {
                    evidence.hiita_no_target_activation_absent = true;
                }
            }
            if (!specialized_state_checked) {
                specialized_state_checked = validate_specialized_observation(spec.id, observation, evidence);
            } else {
                inspect_observation(observation, evidence);
            }
            if (spec.id == "ss06" && find_card(observation, kQixing,
                                               ygo::observation::SemanticZone::MonsterZone) != nullptr) {
                evidence.qixing_state_valid = true;
            }
            if (spec.id == "ss01" && find_card(observation, kEmergence,
                                               ygo::observation::SemanticZone::Hand) != nullptr) {
                evidence.chixiao_search_state_valid = true;
            }
            if (spec.id == "ss07" && request.engine_message_type == MSG_SELECT_IDLECMD) {
                const bool has_balelynx_extra = std::any_of(
                    request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                        return candidate.source_card == kBalelynx && candidate.source_location == LOCATION_EXTRA;
                    });
                const bool token_visible = find_card(
                    observation, kToken, ygo::observation::SemanticZone::MonsterZone) != nullptr;
                if (!token_visible && !evidence.token_state_valid && has_balelynx_extra) {
                    evidence.token_restriction_extra_available_before = true;
                }
                if (token_visible && !has_balelynx_extra) {
                    evidence.token_restriction_non_synchro_blocked = true;
                }
                if (!token_visible && evidence.token_state_valid && has_balelynx_extra) {
                    evidence.token_restriction_extra_available_after = true;
                }
            }
            if (spec.id == "ss12_condition" && request.engine_message_type == MSG_SELECT_IDLECMD) {
                const bool has_ashuna = std::any_of(
                    request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                        return candidate.source_card == kAshuna &&
                               (candidate.source_location == LOCATION_HAND ||
                                candidate.source_location == LOCATION_GRAVE);
                    });
                const bool token_visible = find_card(
                    observation, kToken, ygo::observation::SemanticZone::MonsterZone) != nullptr;
                if (!token_visible && !has_ashuna) {
                    evidence.tenyi_condition_false_no_ashuna = true;
                }
                if (token_visible && has_ashuna) {
                    evidence.tenyi_condition_true_ashuna_candidate = true;
                }
            }

            const auto& selected = [&]() -> const ActionCandidate& {
                if (spec.id == "sg11" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD &&
                    evidence.wolf_state_valid) {
                    const auto spinny = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kSpinny &&
                                   candidate.source_location == LOCATION_GRAVE &&
                                   candidate.phase == 5;
                        });
                    if (spinny != request.candidates.end()) {
                        sg11_fire_place_pending = true;
                        return *spinny;
                    }
                }
                if (spec.id == "sg11" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_PLACE &&
                    sg11_fire_place_pending) {
                    const auto linked_place = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_sequence == 1;
                        });
                    if (linked_place != request.candidates.end()) {
                        sg11_fire_place_pending = false;
                        return *linked_place;
                    }
                }
                if (spec.id == "sg12" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD) {
                    const auto wolf_effect = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kSunlightWolf &&
                                   candidate.source_location == LOCATION_MZONE && candidate.phase == 5;
                        });
                    if (wolf_effect != request.candidates.end()) {
                        return *wolf_effect;
                    }
                }
                if (spec.id == "sg14" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD) {
                    const auto phoenix_effect = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kPyroPhoenix &&
                                   candidate.source_location == LOCATION_MZONE && candidate.phase == 5;
                        });
                    if (phoenix_effect != request.candidates.end()) {
                        return *phoenix_effect;
                    }
                    if (evidence.pyro_phoenix_reincarnation) {
                        const auto pass = std::find_if(
                            request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                return candidate.source_card == 0 && candidate.phase == 7;
                            });
                        if (pass != request.candidates.end()) {
                            return *pass;
                        }
                    }
                }
                if (spec.id == "sg09_direct" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD &&
                    evidence.xyz_summoned && !evidence.xyz_effect_activated) {
                    const auto activation = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kMiragestallio &&
                                   candidate.source_location == LOCATION_MZONE &&
                                   candidate.phase == 5;
                        });
                    if (activation != request.candidates.end()) {
                        if (!evidence.xyz_fire_restriction_seen) {
                            const auto phase_end = std::find_if(
                                request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                    return candidate.source_card == 0 && candidate.phase == 8;
                                });
                        if (phase_end != request.candidates.end()) {
                            return *phase_end;
                        }
                        const auto pass_phase = std::find_if(
                            request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                return candidate.source_card == 0 && candidate.phase == 7;
                            });
                        if (pass_phase != request.candidates.end()) {
                            return *pass_phase;
                        }
                        const auto earlier_phase = std::find_if(
                            request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                return candidate.source_card == 0 && candidate.phase >= 6;
                            });
                        if (earlier_phase != request.candidates.end()) {
                            return *earlier_phase;
                        }
                        }
                        return *activation;
                    }
                }
                if (spec.id == "sg09_direct" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD &&
                    evidence.xyz_deck_summon_selected &&
                    !evidence.xyz_fire_restriction_blocked) {
                    const auto called_by = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kCalledBy &&
                                   candidate.source_location == LOCATION_HAND;
                        });
                    if (called_by != request.candidates.end()) {
                        return *called_by;
                    }
                }
                if (spec.id == "sg09_direct" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD &&
                    evidence.xyz_fire_restriction_blocked &&
                    !evidence.xyz_restriction_phase_boundary) {
                    const auto phase_end = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.phase == 8;
                        }) != request.candidates.end()
                                             ? std::find_if(
                                                   request.candidates.begin(), request.candidates.end(),
                                                   [](const auto& candidate) { return candidate.phase == 8; })
                                             : std::find_if(
                                                   request.candidates.begin(), request.candidates.end(),
                                                   [](const auto& candidate) {
                                                       return candidate.phase == 7;
                                                   });
                    if (phase_end == request.candidates.end()) {
                        const auto earlier_phase = std::find_if(
                            request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                return candidate.phase == 6;
                                                   });
                        if (earlier_phase != request.candidates.end()) {
                            return *earlier_phase;
                        }
                    }
                    if (phase_end != request.candidates.end()) {
                        return *phase_end;
                    }
                }
                if (spec.id == "sg09_direct" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD &&
                    evidence.xyz_restriction_phase_boundary) {
                    const auto called_by = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kCalledBy &&
                                   candidate.source_location == LOCATION_HAND &&
                                   candidate.source_sequence == 1;
                        });
                    if (called_by != request.candidates.end()) {
                        return *called_by;
                    }
                }
                if (spec.id == "sg09_direct" && request.player == 1 &&
                    (request.engine_message_type == MSG_SELECT_IDLECMD ||
                     request.engine_message_type == MSG_SELECT_CHAIN)) {
                    if (request.engine_message_type == MSG_SELECT_CHAIN) {
                        const auto pass = std::find_if(
                            request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                return candidate.semantic_key == "chain.pass";
                            });
                        if (pass != request.candidates.end()) {
                            return *pass;
                        }
                    }
                    const bool non_fire_candidate_available = std::any_of(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return (candidate.source_card == kVishuda &&
                                    candidate.source_location == LOCATION_GRAVE) ||
                                   (candidate.source_card == kChixiao &&
                                    candidate.source_location == LOCATION_MZONE);
                        });
                    if (non_fire_candidate_available) {
                        const auto pass_phase = std::find_if(
                            request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                return candidate.source_card == 0 && candidate.phase == 7;
                            });
                        if (pass_phase != request.candidates.end()) {
                            return *pass_phase;
                        }
                        const auto earlier_phase = std::find_if(
                            request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                                return candidate.source_card == 0 && candidate.phase >= 6;
                            });
                        if (earlier_phase != request.candidates.end()) {
                            return *earlier_phase;
                        }
                    }
                }
                if (spec.id == "sg17" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD) {
                    const auto rage = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kRage &&
                                   candidate.source_location == LOCATION_SZONE;
                        });
                    if (rage != request.candidates.end()) {
                        return *rage;
                    }
                }
                if (spec.id == "sg16_negate" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD) {
                    const auto called_by = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kCalledBy &&
                                   candidate.source_location == LOCATION_HAND;
                        });
                    if (called_by != request.candidates.end()) {
                        return *called_by;
                    }
                }
                if (spec.id == "sg07_jack" && request.player == 0 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD) {
                    if (request.candidates.back().source_card == 0) {
                        return request.candidates.back();
                    }
                    const auto end_phase = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.phase == 8;
                        });
                    if (end_phase != request.candidates.end()) {
                        return *end_phase;
                    }
                    const auto non_action = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == 0 && candidate.phase >= 6;
                        });
                    if (non_action != request.candidates.end()) {
                        return *non_action;
                    }
                }
                if (spec.id == "sg07_jack" && request.player == 1 &&
                    request.engine_message_type == MSG_SELECT_IDLECMD) {
                    const auto setup_balelynx = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kBalelynx &&
                                   candidate.source_location == LOCATION_EXTRA &&
                                   candidate.source_sequence == 15;
                        });
                    if (setup_balelynx != request.candidates.end()) {
                        return *setup_balelynx;
                    }
                }
                if ((spec.id == "sg08_real" || spec.id == "sg09_direct") &&
                    request.engine_message_type == MSG_SELECT_IDLECMD &&
                    evidence.xyz_summoned) {
                    const auto activation = std::find_if(
                        request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                            return candidate.source_card == kMiragestallio && candidate.phase == 5;
                        });
                    if (activation != request.candidates.end()) {
                        return *activation;
                    }
                }
                return policy.choose(request);
            }();
            if (spec.id == "ss07" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                evidence.token_restriction_extra_available_after && selected.source_card == kBalelynx &&
                selected.source_location == LOCATION_EXTRA) {
                evidence.token_restriction_balelynx_selected_after = true;
            }
            if (spec.id == "ss12_condition" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kAshuna &&
                (selected.source_location == LOCATION_HAND || selected.source_location == LOCATION_GRAVE)) {
                evidence.tenyi_condition_true_ashuna_selected = true;
            }
            if (spec.id == "ss18") {
                if (request.engine_message_type == MSG_SELECT_IDLECMD &&
                    selected.source_location == LOCATION_EXTRA) {
                    if (selected.source_card == kMonk) {
                        evidence.monk_extra_selected = true;
                    }
                    if (selected.source_card == kShaman) {
                        evidence.shaman_extra_selected = true;
                    }
                }
                if (request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                    selected.action_kind == ygo::protocol::ActionKind::CardSelection &&
                    selected.source_location == LOCATION_MZONE) {
                    if (evidence.monk_extra_selected && !evidence.monk_material_selected) {
                        evidence.monk_material_selected = true;
                    } else if (evidence.shaman_extra_selected && !evidence.shaman_material_selected) {
                        evidence.shaman_material_selected = true;
                    }
                }
                if (request.engine_message_type == MSG_SELECT_PLACE) {
                    if (evidence.monk_extra_selected && !evidence.monk_placement_selected) {
                        evidence.monk_placement_selected = true;
                    } else if (evidence.shaman_extra_selected && !evidence.shaman_placement_selected) {
                        evidence.shaman_placement_selected = true;
                    }
                }
            }
            if (spec.id == "ss08") {
                if (request.engine_message_type == MSG_SELECT_IDLECMD &&
                    selected.source_card == kBaxia && selected.source_location == LOCATION_EXTRA) {
                    evidence.baxia_extra_selected = true;
                }
                if (request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                    selected.action_kind == ygo::protocol::ActionKind::CardSelection &&
                    selected.source_location == LOCATION_MZONE) {
                    if (selected.source_card == kToken) {
                        evidence.baxia_token_material_selected = true;
                    }
                    if (selected.source_card == kTaia) {
                        evidence.baxia_taia_material_selected = true;
                    }
                }
                if (request.engine_message_type == MSG_SELECT_POSITION &&
                    selected.source_card == kBaxia) {
                    evidence.baxia_summoned = true;
                }
                if (request.engine_message_type == MSG_SELECT_CHAIN &&
                    selected.source_card == kBaxia && selected.source_location == LOCATION_MZONE) {
                    if (!evidence.baxia_trigger_activated) {
                        evidence.baxia_trigger_activated = true;
                        baxia_trigger_selection_open = true;
                    } else {
                        evidence.baxia_ignition_activated = true;
                    }
                }
                if (request.engine_message_type == MSG_SELECT_IDLECMD &&
                    selected.source_card == kBaxia && selected.source_location == LOCATION_MZONE &&
                    evidence.baxia_trigger_activated) {
                    evidence.baxia_ignition_activated = true;
                }
                if (baxia_trigger_selection_open && request.engine_message_type == MSG_SELECT_CARD &&
                    selected.source_card != 0 && selected.source_location == LOCATION_MZONE &&
                    std::find(evidence.baxia_trigger_targets.begin(), evidence.baxia_trigger_targets.end(),
                              selected.source_card) == evidence.baxia_trigger_targets.end()) {
                    evidence.baxia_trigger_targets.push_back(selected.source_card);
                }
                if (evidence.baxia_ignition_activated && request.engine_message_type == MSG_SELECT_CARD) {
                    if (selected.source_card == kBaxia && selected.source_location == LOCATION_MZONE) {
                        evidence.baxia_destroy_target_selected = true;
                    }
                    if ((selected.source_card == kMoYe || selected.source_card == kTaia) &&
                        selected.source_location == LOCATION_GRAVE) {
                        evidence.baxia_revive_target_selected = true;
                    }
                }
            }
            if (spec.id == "btl01" && request.engine_message_type == MSG_SELECT_BATTLECMD) {
                ++evidence.battle_commands;
                if (selected.action_kind == ygo::protocol::ActionKind::BattleCommand) {
                    evidence.battle_attack_selected = true;
                }
            }
            if (spec.id == "sg20" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kSunlightWolf && selected.source_location == LOCATION_EXTRA) {
                evidence.link_extra_selected = true;
            }
            if (spec.id == "sg20" && request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                selected.action_kind == ygo::protocol::ActionKind::CardSelection) {
                if (selected.source_card == kFire && selected.source_location == LOCATION_MZONE) {
                    evidence.link_material_fire_selected = true;
                }
                if (selected.source_card == kSpinny && selected.source_location == LOCATION_MZONE) {
                    evidence.link_material_spinny_selected = true;
                }
            }
            if (spec.id == "sg20" && request.engine_message_type == MSG_SELECT_PLACE &&
                evidence.link_material_fire_selected && evidence.link_material_spinny_selected) {
                evidence.link_placement_selected = true;
            }
            if ((spec.id == "sg12" || spec.id == "sg13" || spec.id == "sg14" || spec.id == "sg15") &&
                request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                selected.action_kind == ygo::protocol::ActionKind::CardSelection &&
                selected.source_location == LOCATION_MZONE) {
                if (spec.id == "sg12" && evidence.reincarnation_extra_selected &&
                    selected.source_card == kSunlightWolf && selected.source_controller == 0) {
                    evidence.reincarnation_material_selected = true;
                }
                if ((selected.source_card == kFire || selected.source_card == kSpinny) &&
                    spec.id != "sg12") {
                    if (spec.id == "sg13") {
                        evidence.raging_phoenix_reincarnation = true;
                    }
                    if (spec.id == "sg14") {
                        evidence.pyro_phoenix_reincarnation = true;
                    }
                    if (spec.id == "sg15") {
                        evidence.heatleo_reincarnation = true;
                    }
                }
            }
            if ((spec.id == "sg12" || spec.id == "sg13" || spec.id == "sg14" || spec.id == "sg15") &&
                request.engine_message_type == MSG_SELECT_PLACE &&
                (evidence.reincarnation_material_selected || evidence.raging_phoenix_reincarnation ||
                 evidence.pyro_phoenix_reincarnation || evidence.heatleo_reincarnation)) {
                if (spec.id == "sg12") {
                    evidence.reincarnation_placement_selected = true;
                }
            }
            if (spec.id == "sg12" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kSunlightWolf && selected.source_location == LOCATION_EXTRA) {
                evidence.reincarnation_extra_selected = true;
            }
            if (spec.id == "sg13" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kRagingPhoenix && selected.source_location == LOCATION_EXTRA) {
                evidence.raging_phoenix_reincarnation = true;
            }
            if (spec.id == "sg14" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kPyroPhoenix && selected.source_location == LOCATION_EXTRA) {
                evidence.pyro_phoenix_reincarnation = true;
            }
            if (spec.id == "sg15" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kHeatleo && selected.source_location == LOCATION_EXTRA) {
                evidence.heatleo_reincarnation = true;
            }
            if ((spec.id == "sg08_real" || spec.id == "sg09_direct") &&
                request.engine_message_type == MSG_SELECT_IDLECMD &&
                candidate_is_code(selected, kMiragestallio) && selected.source_location == LOCATION_EXTRA) {
                evidence.xyz_extra_selected = true;
            }
            if ((spec.id == "sg08_real" || spec.id == "sg09_direct") &&
                request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                selected.action_kind == ygo::protocol::ActionKind::CardSelection &&
                selected.source_location == LOCATION_MZONE) {
                if (selected.source_card != 0) {
                    evidence.xyz_material_selected_codes.insert(selected.source_card);
                }
                if (candidate_is_code(selected, kFire)) {
                    evidence.xyz_material_one_selected = true;
                }
                if (candidate_is_code(selected, kSpinny)) {
                    evidence.xyz_material_two_selected = true;
                }
            }
            if ((spec.id == "sg08_real" || spec.id == "sg09_direct") &&
                request.engine_message_type == MSG_SELECT_IDLECMD &&
                candidate_is_code(selected, kMiragestallio) && selected.source_location == LOCATION_MZONE &&
                selected.phase == 5 &&
                evidence.xyz_summoned) {
                evidence.xyz_effect_activated = true;
                evidence.xyz_effect_engine_step = request.engine_step_index;
            }
            if ((spec.id == "sg08_real" || spec.id == "sg09_direct") &&
                request.engine_message_type == MSG_SELECT_CARD &&
                candidate_is_code(selected, kFoxy) && selected.source_location == LOCATION_DECK &&
                evidence.xyz_effect_activated) {
                evidence.xyz_deck_summon_selected = true;
            }
            if (spec.id == "ss06" && selected.source_card == kLongyuan &&
                selected.source_location == LOCATION_HAND) {
                evidence.longyuan_discarded = true;
            }
            if (spec.id == "ss01" && selected.source_card == kEmergence &&
                selected.source_location == LOCATION_DECK) {
                evidence.chixiao_search_selected = true;
            }
            if ((spec.id == "ss01" || spec.id == "ss05") &&
                request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card != 0) {
                evidence.selected_trigger_chain_sources.push_back(selected.source_card);
            }
            if (spec.id == "ss05" && request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kChixiao) {
                evidence.chixiao_negation_activated = true;
            }
            if (spec.id == "ss05" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kBalelynx && selected.source_controller == 1 &&
                selected.source_location == LOCATION_MZONE) {
                evidence.chixiao_negation_targeted = true;
            }
            if (spec.id == "int01" && request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kAsh && selected.source_controller == 1) {
                evidence.interaction_chain_activated = true;
            }
            if (spec.id == "int02" && request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kVeiler && selected.source_controller == 1) {
                evidence.interaction_chain_activated = true;
            }
            if (spec.id == "int03" && request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kImpermanence && selected.source_controller == 1) {
                evidence.interaction_chain_activated = true;
            }
            if (spec.id == "int04" && request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kGhostBelle && selected.source_controller == 1) {
                evidence.ghost_belle_chain_activated = true;
            }
            if (spec.id == "int05" && request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kCalledBy) {
                evidence.interaction_chain_activated = true;
            }
            if ((spec.id == "int02" || spec.id == "int03") &&
                request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kMoYe && selected.source_controller == 0 &&
                selected.source_location == LOCATION_MZONE) {
                evidence.interaction_target_selected = true;
            }
            if (spec.id == "int05" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kAsh && selected.source_controller == 1 &&
                selected.source_location == LOCATION_GRAVE) {
                evidence.interaction_target_selected = true;
            }
            if (spec.id == "int01" && evidence.interaction_chain_activated) {
                // Ash Blossom has no target; its legal hand-trigger domain is
                // the chain candidate itself.
                evidence.interaction_target_selected = true;
            }
            if ((spec.id == "ss10" || spec.id == "ss10_banish" || spec.id == "ss16_chengying") &&
                request.engine_message_type == MSG_SELECT_CHAIN &&
                (selected.source_card == kBlackout ||
                 selected.semantic_key.find(std::to_string(kBlackout)) != std::string::npos)) {
                evidence.blackout_activated = true;
            }
            if ((spec.id == "ss10_banish" || spec.id == "ss16_chengying") &&
                request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kBlackout) && evidence.blackout_banished_from_grave) {
                evidence.blackout_banish_trigger_activated = true;
            }
            if ((spec.id == "ss10_banish" || spec.id == "ss16_chengying") &&
                request.engine_message_type == MSG_SELECT_EFFECTYN &&
                candidate_is_code(selected, kBlackout) && selected.semantic_key == "yes_no.yes" &&
                evidence.blackout_banished_from_grave) {
                evidence.blackout_banish_trigger_activated = true;
            }
            if ((spec.id == "ss10_banish" || spec.id == "ss16_chengying") &&
                request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                candidate_is_code(selected, kChengying) && selected.source_controller == 0 &&
                selected.source_location == LOCATION_MZONE && evidence.blackout_activated) {
                evidence.chengying_replacement_selected = true;
            }
            if ((spec.id == "ss10_banish" || spec.id == "ss16_chengying") &&
                request.engine_message_type == MSG_SELECT_EFFECTYN &&
                selected.semantic_key == "yes_no.yes" && evidence.chengying_replacement_selected) {
                evidence.chengying_replacement_selected = true;
            }
            if (spec.id == "ss17" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kQixing)) {
                evidence.qixing_rage_activation = true;
                evidence.qixing_interaction_activated = true;
            }
            if (spec.id == "ss12" && request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kCircle) {
                evidence.circle_activated = true;
            }
            if (spec.id == "ss12" && request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                selected.source_card == kVishuda && selected.source_controller == 0 &&
                selected.source_location == LOCATION_MZONE) {
                evidence.circle_release_selected = true;
            }
            if (spec.id == "ss12" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kVishuda && selected.source_controller == 0 &&
                selected.source_location == LOCATION_DECK) {
                evidence.circle_search_selected = true;
            }
            if (spec.id == "ss14" && request.engine_message_type == MSG_SELECT_IDLECMD) {
                const bool has_non_wyrm_extra = std::any_of(
                    request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                        return candidate.source_card == kBalelynx && candidate.source_location == LOCATION_EXTRA;
                    });
                const bool has_extra_candidate = std::any_of(
                    request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                        return candidate.source_location == LOCATION_EXTRA;
                    });
                if (!evidence.ashuna_activated && has_non_wyrm_extra) {
                    evidence.ashuna_extra_available_before = true;
                }
                if (evidence.ashuna_activated && has_extra_candidate && !has_non_wyrm_extra) {
                    evidence.ashuna_non_wyrm_extra_blocked = true;
                }
                if (evidence.ashuna_activated && evidence.ashuna_phase_boundary_after_activation &&
                    has_non_wyrm_extra) {
                    evidence.ashuna_extra_available_after_expiry = true;
                }
            }
            if (spec.id == "ss14" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kAshuna && selected.source_location == LOCATION_GRAVE) {
                evidence.ashuna_activated = true;
                evidence.ashuna_activation_engine_step = request.engine_step_index;
            }
            if (spec.id == "ss14" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kVishuda && selected.source_controller == 0 &&
                selected.source_location == LOCATION_DECK) {
                evidence.ashuna_deck_target_selected = true;
            }
            if (spec.id == "ss15" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kVishuda && selected.source_location == LOCATION_GRAVE) {
                evidence.vishuda_activated = true;
            }
            if (spec.id == "ss15" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kFire && selected.source_controller == 1 &&
                selected.source_location == LOCATION_MZONE) {
                evidence.vishuda_target_selected = true;
            }
            if (spec.id == "ss16" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kAdhara && selected.source_location == LOCATION_GRAVE) {
                evidence.adhara_activated = true;
            }
            if (spec.id == "ss16" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kVishuda && selected.source_controller == 0 &&
                selected.source_location == LOCATION_REMOVED) {
                evidence.adhara_target_selected = true;
            }
            if ((spec.id == "sg05" || spec.id == "sg06") &&
                request.engine_message_type == MSG_SELECT_CHAIN &&
                selected.source_card == kGazelle && selected.source_controller == 1 &&
                selected.source_location == LOCATION_HAND) {
                evidence.gazelle_hand_activated = true;
            }
            if (spec.id == "sg07_jack" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                candidate_is_code(selected, kJackJaguar) && selected.source_location == LOCATION_GRAVE) {
                evidence.jack_jaguar_activated = true;
            }
            if (spec.id == "sg07_jack" && request.engine_message_type == MSG_SELECT_CARD &&
                candidate_is_code(selected, kSpinny) && selected.source_location == LOCATION_GRAVE &&
                evidence.jack_jaguar_activated) {
                evidence.jack_jaguar_target_selected = true;
            }
            if (spec.id == "sg07_weasel" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kWeasel) && selected.source_location == LOCATION_GRAVE) {
                evidence.weasel_trigger_activated = true;
            }
            if (spec.id == "sg07_weasel" && request.engine_message_type == MSG_SELECT_EFFECTYN &&
                candidate_is_code(selected, kWeasel) && selected.semantic_key == "yes_no.yes") {
                evidence.weasel_trigger_activated = true;
                evidence.weasel_trigger_engine_step = request.engine_step_index;
            }
            if (spec.id == "sg07_weasel" && request.engine_message_type == MSG_SELECT_CARD &&
                candidate_is_code(selected, kFire) && selected.source_location == LOCATION_GRAVE &&
                evidence.weasel_trigger_activated) {
                evidence.weasel_selected = true;
            }
            if (spec.id == "sg07_falco" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                candidate_is_code(selected, kFalco) && selected.source_location == LOCATION_GRAVE) {
                evidence.falco_activated = true;
            }
            if (spec.id == "sg07_falco" && request.engine_message_type == MSG_SELECT_CARD &&
                candidate_is_code(selected, kSpinny) && selected.source_location == LOCATION_MZONE &&
                evidence.falco_activated) {
                evidence.falco_target_selected = true;
            }
            if (spec.id == "sg11" && request.engine_message_type == MSG_SELECT_CARD &&
                candidate_is_code(selected, kFire) && selected.source_location == LOCATION_GRAVE &&
                evidence.wolf_fire_trigger_activated) {
                evidence.wolf_fire_target_selected = true;
            }
            if (spec.id == "sg11" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kSunlightWolf)) {
                evidence.wolf_fire_trigger_activated = true;
            }
            if (spec.id == "sg11" && request.engine_message_type == MSG_SELECT_EFFECTYN &&
                candidate_is_code(selected, kSunlightWolf) && selected.semantic_key == "yes_no.yes") {
                evidence.wolf_fire_trigger_activated = true;
            }
            if (spec.id == "sg16_negate" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kRoar) && selected.source_controller == 1) {
                evidence.roar_negate_activated = true;
            }
            if (spec.id == "sg16_recovery" &&
                (request.engine_message_type == MSG_SELECT_EFFECTYN ||
                 request.engine_message_type == MSG_SELECT_CHAIN) &&
                candidate_is_code(selected, kRoar) &&
                (request.engine_message_type == MSG_SELECT_CHAIN ||
                 selected.semantic_key == "yes_no.yes")) {
                evidence.roar_recovery_trigger_activated = true;
            }
            if (spec.id == "sg17" &&
                (request.engine_message_type == MSG_SELECT_IDLECMD ||
                 request.engine_message_type == MSG_SELECT_CHAIN) &&
                candidate_is_code(selected, kRage) && selected.source_location == LOCATION_SZONE) {
                evidence.rage_activated = true;
            }
            if (spec.id == "sg17" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kBalelynx && selected.source_controller == 1 &&
                selected.source_location == LOCATION_MZONE && evidence.rage_activated) {
                evidence.rage_cost_sent = true;
            }
            if (spec.id == "sg17" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_controller == 0 &&
                (selected.source_location == LOCATION_MZONE || selected.source_location == LOCATION_SZONE) &&
                evidence.rage_activated) {
                evidence.rage_target_selected = true;
                evidence.rage_target_count = 1;
            }
            if (spec.id == "sg12" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kSunlightWolf)) {
                evidence.wolf_st_trigger_activated = true;
            }
            if (spec.id == "sg12" && request.engine_message_type == MSG_SELECT_EFFECTYN &&
                candidate_is_code(selected, kSunlightWolf) && selected.semantic_key == "yes_no.yes") {
                evidence.wolf_st_trigger_activated = true;
            }
            if (spec.id == "sg12" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kSunlightWolf && selected.source_location == LOCATION_MZONE &&
                selected.phase == 5) {
                evidence.wolf_st_trigger_activated = true;
            }
            if (spec.id == "sg12" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kRage && selected.source_location == LOCATION_GRAVE &&
                evidence.wolf_st_trigger_activated) {
                evidence.wolf_st_target_selected = true;
            }
            if (spec.id == "sg13" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kRagingPhoenix && selected.source_location == LOCATION_EXTRA) {
                evidence.raging_phoenix_reincarnation = true;
            }
            if (spec.id == "sg13" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kRagingPhoenix)) {
                evidence.raging_phoenix_trigger_activated = true;
            }
            if (spec.id == "sg13" && request.engine_message_type == MSG_SELECT_EFFECTYN &&
                candidate_is_code(selected, kRagingPhoenix) && selected.semantic_key == "yes_no.yes") {
                evidence.raging_phoenix_trigger_activated = true;
            }
            if (spec.id == "sg13" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_location == LOCATION_DECK &&
                (selected.source_card == kFoxy || selected.source_card == kRoar) &&
                evidence.raging_phoenix_trigger_activated) {
                evidence.raging_phoenix_search_selected = true;
            }
            if (spec.id == "sg14" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kPyroPhoenix && selected.source_location == LOCATION_EXTRA) {
                evidence.pyro_phoenix_reincarnation = true;
            }
            if (spec.id == "sg14" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kPyroPhoenix)) {
                evidence.pyro_phoenix_trigger_activated = true;
            }
            if (spec.id == "sg14" && request.engine_message_type == MSG_SELECT_EFFECTYN &&
                candidate_is_code(selected, kPyroPhoenix) && selected.semantic_key == "yes_no.yes") {
                evidence.pyro_phoenix_trigger_activated = true;
            }
            if (spec.id == "sg14" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kPyroPhoenix && selected.source_location == LOCATION_MZONE &&
                selected.phase == 5 && evidence.pyro_phoenix_trigger_activated) {
                evidence.pyro_phoenix_revive_activated = true;
            }
            if (spec.id == "sg14" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kBalelynx && selected.source_location == LOCATION_GRAVE &&
                evidence.pyro_phoenix_revive_activated) {
                evidence.pyro_phoenix_revive_target_selected = true;
            }
            if (spec.id == "sg15" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kHeatleo && selected.source_location == LOCATION_EXTRA) {
                evidence.heatleo_reincarnation = true;
            }
            if (spec.id == "sg15" && request.engine_message_type == MSG_SELECT_CHAIN &&
                candidate_is_code(selected, kHeatleo)) {
                evidence.heatleo_trigger_activated = true;
            }
            if (spec.id == "sg15" && request.engine_message_type == MSG_SELECT_EFFECTYN &&
                candidate_is_code(selected, kHeatleo) && selected.semantic_key == "yes_no.yes") {
                evidence.heatleo_trigger_activated = true;
            }
            if (spec.id == "sg15" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kBlackout && selected.source_controller == 1 &&
                selected.source_location == LOCATION_SZONE) {
                evidence.heatleo_trigger_activated = true;
                evidence.heatleo_target_selected = true;
            }
            if (spec.id == "sg18" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kPrometheanPrincess && selected.source_location == LOCATION_EXTRA) {
                evidence.promethean_extra_selected = true;
            }
            if (spec.id == "sg18" && request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                selected.action_kind == ygo::protocol::ActionKind::CardSelection &&
                selected.source_location == LOCATION_MZONE && evidence.promethean_extra_selected) {
                evidence.promethean_material_selected = true;
            }
            if (spec.id == "sg18" && request.engine_message_type == MSG_SELECT_PLACE &&
                evidence.promethean_material_selected) {
                evidence.promethean_placement_selected = true;
            }
            if (spec.id == "sg18" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kPrometheanPrincess && selected.source_location == LOCATION_MZONE &&
                selected.phase == 5) {
                evidence.promethean_fire_revival_activated = true;
            }
            if (spec.id == "sg18" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kFire && selected.source_location == LOCATION_GRAVE &&
                evidence.promethean_fire_revival_activated) {
                evidence.promethean_fire_target_selected = true;
            }
            if ((spec.id == "sg19" || spec.id == "sg19_no_target") &&
                request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kHiita && selected.source_location == LOCATION_EXTRA) {
                evidence.hiita_extra_selected = true;
            }
            if ((spec.id == "sg19" || spec.id == "sg19_no_target") &&
                request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                selected.action_kind == ygo::protocol::ActionKind::CardSelection &&
                selected.source_location == LOCATION_MZONE && evidence.hiita_extra_selected) {
                evidence.hiita_material_selected = true;
            }
            if ((spec.id == "sg19" || spec.id == "sg19_no_target") &&
                request.engine_message_type == MSG_SELECT_PLACE &&
                evidence.hiita_material_selected) {
                evidence.hiita_placement_selected = true;
            }
            if (spec.id == "sg19" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kHiita && selected.source_location == LOCATION_MZONE &&
                selected.phase == 5) {
                evidence.hiita_activation_activated = true;
            }
            if (spec.id == "sg19" && request.engine_message_type == MSG_SELECT_CARD &&
                selected.source_card == kFire && selected.source_controller == 1 &&
                selected.source_location == LOCATION_GRAVE && evidence.hiita_activation_activated) {
                evidence.hiita_target_selected = true;
            }
            if (spec.id == "sg19_no_target" && request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kHiita && selected.source_location == LOCATION_MZONE &&
                selected.phase == 5) {
                evidence.hiita_no_target_activation_absent = false;
            }
        if (spec.id == "sg05" && request.engine_message_type == MSG_SELECT_CARD &&
            selected.source_card == kSpinny && selected.source_controller == 1 &&
            selected.source_location == LOCATION_DECK) {
            evidence.gazelle_deck_send_selected = true;
        }
        if (spec.id == "sg06" && request.engine_message_type == MSG_SELECT_CARD) {
            for (const auto& candidate : request.candidates) {
                if (candidate.source_location == LOCATION_DECK &&
                    (candidate.source_card == kFoxy || candidate.source_card == kSpinny ||
                     candidate.source_card == kFire)) {
                    ++evidence.gazelle_deck_salamegreat_candidate_count;
                }
            }
            if (selected.source_card == kFoxy && selected.source_controller == 1 &&
                selected.source_location == LOCATION_DECK) {
                evidence.gazelle_foxy_deck_send_selected = true;
            }
        }
            if (spec.id == "sg02" && request.engine_message_type == MSG_SELECT_EFFECTYN &&
                balelynx_on_field && selected.semantic_key == "yes_no.yes") {
                evidence.balelynx_effect_yes = true;
            }
            if (spec.id == "sg02" && selected.source_card == kSanctuary) {
                evidence.sanctuary_searched = true;
            }
            if ((spec.id == "sg03" || spec.id == "sg04" || spec.id == "sg16_recovery") &&
                request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kSanctuary && selected.source_location == LOCATION_SZONE) {
                evidence.sanctuary_activated = true;
            }
            if ((spec.id == "sg04" || spec.id == "sg16_recovery") &&
                request.engine_message_type == MSG_SELECT_IDLECMD &&
                selected.source_card == kBalelynx && selected.source_location == LOCATION_EXTRA) {
                evidence.reincarnation_extra_selected = true;
            }
            if (request.engine_message_type == MSG_SELECT_UNSELECT_CARD &&
                selected.action_kind == ygo::protocol::ActionKind::CardSelection) {
                evidence.unselect_sources.push_back(selected.source_card);
                if ((spec.id == "sg04" || spec.id == "sg16_recovery") &&
                    evidence.reincarnation_extra_selected &&
                    selected.source_card == kBalelynx && selected.source_controller == 0 &&
                    selected.source_location == LOCATION_MZONE) {
                    evidence.reincarnation_material_selected = true;
                }
                if (spec.id == "ss10" && evidence.selected_blackout_sources.empty()) {
                    // This fixture has one constrained unselect sequence. The
                    // first such request is the Blackout resolution domain;
                    // later unselect requests belong to ordinary gameplay.
                    evidence.blackout_activated = true;
                }
                if (spec.id == "ss10" && evidence.blackout_activated &&
                    evidence.selected_blackout_sources.size() < 3) {
                    evidence.selected_blackout_sources.push_back(selected.source_card);
                }
            }
            if ((spec.id == "sg04" || spec.id == "sg16_recovery") &&
                evidence.reincarnation_material_selected &&
                request.engine_message_type == MSG_SELECT_PLACE) {
                evidence.reincarnation_placement_selected = true;
            }

            if (!request.continuation.has_value()) {
                require(selected.submits_engine_response && !selected.exact_response_bytes.empty(),
                        spec.id + " atomic policy candidate did not submit one response");
                host.submit_response(selected.exact_response_bytes);
                break;
            }

            const auto process_calls_before = host.process_call_count();
            const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
            if (!transition.terminal) {
                ++evidence.continuation_intermediate_steps;
                require(!transition.engine_advanced && transition.engine_response.empty(),
                        spec.id + " continuation intermediate action advanced the engine");
                require(host.process_call_count() == process_calls_before,
                        spec.id + " continuation intermediate action called the engine");
                request = transition.request;
                continue;
            }
            require(transition.engine_advanced && !transition.engine_response.empty(),
                    spec.id + " continuation terminal action did not produce one response");
            host.submit_response(transition.engine_response);
            if (spec.id == "ss08" && baxia_trigger_selection_open) {
                baxia_trigger_selection_open = false;
            }
            break;
        }
    }

    if (spec.id == "ss01") {
        require(evidence.observed_codes.count(kToken) != 0, "ss01 never observed token identity");
        require(evidence.observed_codes.count(kChixiao) != 0, "ss01 never observed Chixiao identity");
        require(evidence.decision_families.count("MSG_SELECT_POSITION") != 0,
                "ss01 did not exercise position selection");
        require(evidence.chixiao_search_selected && evidence.chixiao_search_state_valid,
                "ss01 did not prove Chixiao search of Emergence into the hand");
        require(evidence.selected_trigger_chain_sources.size() >= 2 &&
                    evidence.selected_trigger_chain_sources[0] == kChixiao &&
                    evidence.selected_trigger_chain_sources[1] == kMoYe,
                "ss01 did not preserve the Chixiao/Mo Ye trigger order");
        require(evidence.trigger_chain_resolved, "ss01 did not expose trigger chain resolution");
    } else if (spec.id == "ss06") {
        require(evidence.observed_codes.count(kToken) != 0, "ss06 never observed token identity");
        require(evidence.observed_codes.count(kChixiao) != 0, "ss06 never observed Chixiao identity");
        require(evidence.decision_families.count("MSG_SELECT_POSITION") != 0,
                "ss06 did not exercise position selection");
        require(evidence.longyuan_discarded, "ss06 did not submit Longyuan as the hand discard");
        require(evidence.longyuan_burn, "ss06 did not expose the Longyuan 1200 LP burn");
        require(evidence.qixing_state_valid, "ss06 did not expose the level-10 Qixing state");
    } else if (spec.id == "ss05") {
        require(evidence.chixiao_negation_activated,
                "ss05 did not activate Chixiao in response to the opposing effect monster trigger");
        require(evidence.chixiao_negation_targeted,
                "ss05 did not target the opposing face-up effect monster with Chixiao");
        require(evidence.chixiao_negation_chain_resolved,
                "ss05 did not expose Chixiao negation chain resolution");
    } else if (spec.id == "int01" || spec.id == "int02" || spec.id == "int03" || spec.id == "int05") {
        require(evidence.interaction_chain_activated,
                spec.id + " did not activate the required interaction card from the legal chain domain");
        require(evidence.interaction_chain_resolved,
                spec.id + " did not expose interaction chain resolution");
        if (spec.id != "int01") {
            require(evidence.interaction_target_selected,
                    spec.id + " did not select the required legal interaction target");
        }
    } else if (spec.id == "int04") {
        require(evidence.ghost_belle_chain_activated,
                "int04 did not activate Ghost Belle from the legal chain domain");
        require(evidence.ghost_belle_chain_resolved,
                "int04 did not expose Ghost Belle chain resolution");
    } else if (spec.id == "sg01") {
        require(evidence.observed_codes.count(kBalelynx) != 0, "sg01 never observed Balelynx identity");
        require(std::find(evidence.unselect_sources.begin(), evidence.unselect_sources.end(), kFire) !=
                evidence.unselect_sources.end(),
                "sg01 did not submit Fire as Link material");
    } else if (spec.id == "sg02") {
        require(evidence.decision_families.count("MSG_SELECT_EFFECTYN") != 0,
                "sg02 did not exercise the Balelynx optional trigger");
        require(evidence.decision_families.count("MSG_SELECT_CARD") != 0,
                "sg02 did not exercise Sanctuary card selection");
        require(evidence.balelynx_effect_yes, "sg02 did not accept the Balelynx optional trigger");
        require(evidence.sanctuary_searched, "sg02 did not submit Sanctuary as the search result");
    } else if (spec.id == "sg03") {
        require(evidence.sanctuary_activated,
                "sg03 did not activate the real Sanctuary field card from the legal idle domain");
        require(evidence.sanctuary_state_valid,
                "sg03 did not expose Sanctuary in the Field Zone");
    } else if (spec.id == "sg04") {
        require(evidence.sanctuary_activated,
                "sg04 did not activate Sanctuary before the reincarnation path");
        require(evidence.reincarnation_extra_selected,
                "sg04 did not select the same-name Balelynx from the Extra Deck");
        require(evidence.reincarnation_material_selected,
                "sg04 did not select the existing Balelynx as reincarnation material");
        require(evidence.reincarnation_placement_selected,
                "sg04 did not exercise the reincarnation Link placement domain");
    } else if (spec.id == "ss08") {
        require(evidence.baxia_extra_selected,
                "ss08 did not select Baxia from the Extra Deck");
        require(evidence.baxia_token_material_selected && evidence.baxia_taia_material_selected,
                "ss08 did not select the Token and Taia as Baxia Synchro materials");
        require(evidence.baxia_summoned,
                "ss08 did not expose the real Baxia Synchro summon");
        require(evidence.baxia_trigger_activated && evidence.baxia_trigger_targets.size() >= 2,
                "ss08 did not exercise Baxia's multi-target return trigger");
        require(evidence.baxia_ignition_activated,
                "ss08 did not activate Baxia's destruction/revival ignition effect");
        require(evidence.baxia_destroy_target_selected && evidence.baxia_revive_target_selected,
                "ss08 did not select Baxia's destruction and Graveyard revival targets");
        require(evidence.baxia_destroyed && evidence.baxia_revived,
                "ss08 did not expose Baxia destruction and revival results (destroyed=" +
                    std::string(evidence.baxia_destroyed ? "true" : "false") +
                    ", revived=" + (evidence.baxia_revived ? "true" : "false") + ")");
    } else if (spec.id == "ss07") {
        require(evidence.token_restriction_extra_available_before,
                "ss07 did not expose the non-Synchro Extra candidate before the Token restriction");
        require(evidence.token_restriction_non_synchro_blocked,
                "ss07 did not show the non-Synchro Extra candidate absent while the Token was present");
        require(evidence.token_restriction_expired && evidence.token_restriction_extra_available_after,
                "ss07 did not show the Extra Deck legality change after the Token left");
        require(evidence.token_restriction_balelynx_selected_after,
                "ss07 did not submit Balelynx after the Token restriction expired");
    } else if (spec.id == "ss12_condition") {
        require(evidence.tenyi_condition_false_no_ashuna,
                "ss12_condition did not expose Ashuna absent while only an effect monster was face-up");
        require(evidence.tenyi_condition_true_ashuna_candidate &&
                    evidence.tenyi_condition_true_ashuna_selected,
                "ss12_condition did not expose and select Ashuna after the official non-effect Token appeared");
    } else if (spec.id == "ss18") {
        require(evidence.monk_extra_selected && evidence.shaman_extra_selected,
                "ss18 did not select both Monk and Shaman from the Extra Deck");
        require(evidence.monk_material_selected && evidence.shaman_material_selected,
                "ss18 did not select engine-provided Link materials for both Tenyi Links");
        require(evidence.monk_placement_selected && evidence.shaman_placement_selected,
                "ss18 did not submit both Extra Monster Zone/link placement decisions");
        require(evidence.monk_state_valid && evidence.shaman_state_valid,
                "ss18 did not expose both summoned Tenyi Link states");
    } else if (spec.id == "ss10") {
        require(evidence.decision_families.count("MSG_SELECT_UNSELECT_CARD") != 0,
                "ss10 did not exercise Blackout target selection");
        if (evidence.selected_blackout_sources.size() < 3) {
            std::ostringstream failure;
            failure << "ss10 did not submit three Blackout target selections; activated="
                    << (evidence.blackout_activated ? "true" : "false") << " sources=";
            for (const auto code : evidence.unselect_sources) {
                failure << code << ',';
            }
            throw std::runtime_error(failure.str());
        }
        const std::set<std::uint32_t> expected = {kMoYe, kFire, kFoxy};
        const std::set<std::uint32_t> actual(evidence.selected_blackout_sources.begin(),
                                             evidence.selected_blackout_sources.end());
        if (actual != expected) {
            std::ostringstream failure;
            failure << "ss10 Blackout selection did not match the exact one-own/two-opponent set; actual=";
            for (const auto code : actual) {
                failure << code << ',';
            }
            throw std::runtime_error(failure.str());
        }
        for (const auto code : expected) {
            require(evidence.blackout_destroyed_codes.count(code) != 0,
                    "ss10 did not expose one of the three Blackout destruction results");
        }
    } else if (spec.id == "ss10_banish" || spec.id == "ss16_chengying") {
        require(evidence.blackout_activated,
                spec.id + " did not enter the official Blackout destruction effect");
        require(evidence.chengying_replacement_selected,
                spec.id + " did not accept Chengying's official destruction replacement");
        require(evidence.blackout_banished_from_grave,
                spec.id + " did not expose Blackout moving from the Graveyard to the banished zone");
        require(evidence.blackout_banish_trigger_activated,
                spec.id + " did not enter Blackout's official banished trigger");
        require(evidence.blackout_token_state_valid,
                spec.id + " did not expose the official Swordsoul Token result");
        require(evidence.chengying_dynamic_state_valid,
                spec.id + " did not expose Chengying's banished-card-dependent ATK change");
    } else if (spec.id == "ss17") {
        require(evidence.qixing_interaction_activated,
                "ss17 did not enter Qixing's official EVENT_CHAINING interaction");
        require(evidence.qixing_rage_banished,
                "ss17 did not expose Qixing banishing the activated opposing Spell/Trap");
        require(evidence.qixing_interaction_damage,
                "ss17 did not expose Qixing's official 1200 LP consequence");
        require(evidence.qixing_interaction_chain_resolved,
                "ss17 did not expose Qixing interaction chain resolution");
    } else if (spec.id == "ss12") {
        require(evidence.circle_activated, "ss12 did not activate Heavenly Dragon Circle");
        require(evidence.circle_release_selected, "ss12 did not submit the Wyrm release cost");
        require(evidence.circle_search_selected, "ss12 did not select the Wyrm Deck search result");
        require(evidence.circle_search_state_valid,
                "ss12 did not expose the searched Wyrm in the hand");
    } else if (spec.id == "ss14") {
        require(evidence.ashuna_activated, "ss14 did not activate Ashuna from the Graveyard");
        require(evidence.ashuna_deck_target_selected,
                "ss14 did not select a Tenyi Deck summon target");
        require(evidence.ashuna_deck_state_valid,
                "ss14 did not expose the Tenyi Deck summon result");
        require(evidence.ashuna_extra_available_before,
                "ss14 did not expose the non-Wyrm Extra Deck control candidate before Ashuna");
        require(evidence.ashuna_non_wyrm_extra_blocked,
                "ss14 did not enforce Ashuna's Wyrm-only Extra Deck restriction");
        require(evidence.ashuna_phase_boundary_after_activation,
                "ss14 did not observe an engine-defined phase boundary after Ashuna activation");
        require(evidence.ashuna_extra_available_after_expiry,
                "ss14 did not expose the non-Wyrm Extra Deck candidate after Ashuna's restriction expired");
    } else if (spec.id == "ss15") {
        require(evidence.vishuda_activated, "ss15 did not activate Vishuda from the Graveyard");
        require(evidence.vishuda_target_selected,
                "ss15 did not select the opposing on-field return target");
    } else if (spec.id == "ss16") {
        require(evidence.adhara_activated, "ss16 did not activate Adhara from the Graveyard");
        require(evidence.adhara_target_selected,
                "ss16 did not select the banished Wyrm recovery target");
        require(evidence.adhara_recovery_state_valid,
                "ss16 did not expose the recovered Wyrm in the hand");
    } else if (spec.id == "sg05") {
        require(evidence.gazelle_hand_activated,
                "sg05 did not activate Gazelle from the hand after a Salamangreat card entered the Graveyard");
        require(evidence.spinny_discarded,
                "sg05 did not expose Spinny moving from the hand to the Graveyard as the trigger cost");
        require(evidence.gazelle_deck_send_selected,
                "sg05 did not select a Salamangreat Deck-to-Graveyard result for Gazelle");
        require(evidence.gazelle_chain_resolved, "sg05 did not expose Gazelle chain resolution");
        require(evidence.gazelle_deck_state_valid,
                "sg05 did not expose Sanctuary in the Graveyard after Gazelle resolved");
    } else if (spec.id == "sg06") {
        require(evidence.gazelle_hand_activated,
                "sg06 did not activate Gazelle from the hand after the Spinny cost");
        require(evidence.spinny_discarded,
                "sg06 did not expose Spinny moving from the hand to the Graveyard");
        require(evidence.gazelle_deck_salamegreat_candidate_count >= 2,
                "sg06 did not expose at least two legal Salamangreat Deck-to-Graveyard candidates");
        require(evidence.gazelle_foxy_deck_send_selected,
                "sg06 did not select Foxy from the complete Deck-to-Graveyard domain");
        require(evidence.gazelle_chain_resolved && evidence.gazelle_foxy_gy_state_valid,
                "sg06 did not expose Foxy in the Graveyard after Gazelle resolved");
    } else if (spec.id == "sg07_jack") {
        require(evidence.jack_link_condition_observed,
                "sg07_jack did not expose a real face-up Salamangreat Link condition");
        require(evidence.jack_emzone_place_domain,
                "sg07_jack did not expose the canonical MR5 Extra Monster Zone placement domain");
        require(evidence.jack_candidate_present_under_mr5 && evidence.jack_jaguar_activated,
                "sg07_jack did not expose/activate the official Graveyard ignition under canonical MR5");
        require(evidence.jack_jaguar_target_selected && evidence.jack_jaguar_summoned &&
                    evidence.jack_jaguar_target_recycled,
                "sg07_jack did not expose the resolved official summon/recycle path under canonical MR5");
    } else if (spec.id == "sg07_weasel") {
        require(evidence.weasel_trigger_activated,
                "sg07_weasel did not enter Weasel's official Graveyard trigger");
        require(evidence.weasel_selected,
                "sg07_weasel did not select Weasel's official trigger path");
        require(evidence.weasel_recycled && evidence.weasel_target_summoned &&
                    evidence.weasel_drawn && evidence.weasel_state_valid,
                "sg07_weasel did not expose Weasel's resolved recycle/target-summon/draw state");
    } else if (spec.id == "sg07_falco") {
        require(evidence.falco_activated,
                "sg07_falco did not activate Falco's official Graveyard effect");
        require(evidence.falco_target_selected,
                "sg07_falco did not select the official Salamangreat return target");
        require(evidence.falco_target_recovered && evidence.falco_summoned && evidence.falco_state_valid,
                "sg07_falco did not expose the resolved return and Falco summon");
    } else if (spec.id == "sg11") {
        require(evidence.wolf_fire_trigger_activated,
                "sg11 did not enter Sunlight Wolf's official FIRE recovery trigger");
        require(evidence.wolf_fire_target_selected,
                "sg11 did not select the FIRE recovery target from the Graveyard");
        require(evidence.wolf_fire_recovered && evidence.wolf_state_valid,
                "sg11 did not expose Sunlight Wolf's resolved FIRE recovery state");
    } else if (spec.id == "sg12") {
        require(evidence.reincarnation_extra_selected,
                "sg12 did not select the same-name Sunlight Wolf from the Extra Deck");
        require(evidence.reincarnation_material_selected && evidence.reincarnation_placement_selected,
                "sg12 did not complete Sunlight Wolf's real reincarnation Link procedure");
        require(evidence.wolf_st_trigger_activated && evidence.wolf_st_target_selected,
                "sg12 did not activate/select Sunlight Wolf's official Spell/Trap recovery trigger");
        require(evidence.wolf_st_recovered && evidence.wolf_state_valid,
                "sg12 did not expose the resolved Salamangreat Spell/Trap hand recovery state");
    } else if (spec.id == "sg13") {
        require(evidence.raging_phoenix_reincarnation,
                "sg13 did not complete the real Raging Phoenix reincarnation procedure");
        require(evidence.raging_phoenix_trigger_activated && evidence.raging_phoenix_search_selected,
                "sg13 did not enter Raging Phoenix's official reincarnation search path");
        require(evidence.raging_phoenix_search_resolved && evidence.raging_phoenix_state_valid,
                "sg13 did not expose the resolved Raging Phoenix search state");
    } else if (spec.id == "sg14") {
        require(evidence.pyro_phoenix_reincarnation,
                "sg14 did not complete the real Pyro Phoenix reincarnation procedure");
        require(evidence.pyro_phoenix_trigger_activated && evidence.pyro_phoenix_destroyed_opponent,
                "sg14 did not expose Pyro Phoenix's official reincarnation destruction payoff");
        require(evidence.pyro_phoenix_state_valid,
                "sg14 did not expose Pyro Phoenix's resolved public state");
    } else if (spec.id == "sg15") {
        require(evidence.heatleo_reincarnation,
                "sg15 did not complete the real Heatleo reincarnation procedure");
        require(evidence.heatleo_trigger_activated && evidence.heatleo_target_selected &&
                    evidence.heatleo_target_returned && evidence.heatleo_state_valid,
                "sg15 did not expose Heatleo's official Spell/Trap target and resolution");
    } else if (spec.id == "sg18") {
        require(evidence.promethean_extra_selected && evidence.promethean_material_selected &&
                    evidence.promethean_placement_selected,
                "sg18 did not complete Promethean Princess's real Link procedure");
        require(evidence.promethean_fire_revival_activated && evidence.promethean_fire_target_selected &&
                    evidence.promethean_fire_revived && evidence.promethean_state_valid,
                "sg18 did not expose Promethean Princess's official FIRE revival path");
    } else if (spec.id == "sg19") {
        require(evidence.hiita_extra_selected && evidence.hiita_material_selected &&
                    evidence.hiita_placement_selected,
                "sg19 did not complete Hiita's real Link procedure");
        require(evidence.hiita_activation_activated && evidence.hiita_target_selected &&
                    evidence.hiita_revived && evidence.hiita_state_valid,
                "sg19 did not expose Hiita's official opponent-owned FIRE revival");
    } else if (spec.id == "sg19_no_target") {
        require(evidence.hiita_extra_selected && evidence.hiita_material_selected &&
                    evidence.hiita_placement_selected && evidence.hiita_no_target_activation_absent &&
                    !evidence.hiita_activation_activated,
                "sg19_no_target exposed Hiita activation without a legal opponent FIRE target");
    } else if (spec.id == "sg16_negate") {
        require(evidence.roar_negate_activated,
                "sg16_negate did not activate Roar from the legal chain domain");
        require(evidence.roar_negate_chain_resolved,
                "sg16_negate did not expose Roar chain resolution");
        require(evidence.roar_negate_target_destroyed,
                "sg16_negate did not expose Roar's official negation/destruction result");
        require(evidence.roar_state_valid,
                "sg16_negate did not expose the Salamangreat Link-backed Roar state");
    } else if (spec.id == "sg16_recovery") {
        require(evidence.roar_recovery_trigger_activated,
                "sg16_recovery did not enter Roar's official reincarnation recovery trigger");
        require(evidence.roar_recovery_set,
                "sg16_recovery did not expose Roar moving from the Graveyard to the Spell/Trap Zone");
        require(evidence.roar_state_valid,
                "sg16_recovery did not expose the resolved Roar/Link state");
    } else if (spec.id == "sg17") {
        require(evidence.rage_activated,
                "sg17 did not activate Rage from the legal idle domain");
        require(evidence.rage_cost_sent,
                "sg17 did not submit the Salamangreat monster cost for Rage");
        require(evidence.rage_target_selected && evidence.rage_target_count == 1,
                "sg17 did not select exactly one Rage target");
        require(evidence.rage_target_destroyed,
                "sg17 did not expose Rage's official target destruction");
        require(evidence.rage_state_valid,
                "sg17 did not expose the resolved Rage/Link state");
    } else if (spec.id == "sg08_real") {
        require(evidence.xyz_extra_selected && evidence.xyz_material_selected_codes.size() >= 2,
                "sg08_real did not submit the real Miragestallio Xyz procedure");
        require(evidence.xyz_state_valid && evidence.xyz_material_count_before == 2 &&
                    evidence.xyz_material_identity_verified && evidence.xyz_query_before_detach,
                std::string("sg08_real did not expose rank/material state with verified visible identity (state=") +
                    (evidence.xyz_state_valid ? "true" : "false") + ", count=" +
                    std::to_string(evidence.xyz_material_count_before) + ", identity=" +
                    (evidence.xyz_material_identity_verified ? "true" : "false") + ", query=" +
                    (evidence.xyz_query_before_detach ? "true" : "false") + ", selected=" +
                    std::to_string(evidence.xyz_material_selected_codes.size()) + ")");
    } else if (spec.id == "sg09_direct") {
        require(evidence.xyz_state_valid && evidence.xyz_material_count_before == 2 &&
                    evidence.xyz_material_identity_verified && evidence.xyz_query_before_detach,
                std::string("sg09_direct did not expose the preconditioned Xyz state with verified visible identity (state=") +
                    (evidence.xyz_state_valid ? "true" : "false") + ", count=" +
                    std::to_string(evidence.xyz_material_count_before) + ", identity=" +
                    (evidence.xyz_material_identity_verified ? "true" : "false") + ", query=" +
                    (evidence.xyz_query_before_detach ? "true" : "false") + ", selected=" +
                    std::to_string(evidence.xyz_material_selected_codes.size()) + ")");
        require(evidence.xyz_effect_activated && evidence.xyz_detach_accepted &&
                    evidence.xyz_material_count_after == 1,
                "sg09_direct did not prove detach acceptance and material-count decrement");
        require(evidence.xyz_query_after_detach && evidence.xyz_detached_query_empty,
                "sg09_direct did not prove remaining/detached overlay query behavior after detach");
        require(evidence.xyz_deck_summon_selected && evidence.xyz_deck_summoned,
                "sg09_direct did not prove Miragestallio's official Deck summon");
        require(evidence.xyz_fire_restriction_seen,
                "sg09_direct did not expose a legal non-FIRE monster-effect domain before Miragestallio");
        require(evidence.xyz_fire_restriction_blocked,
                "sg09_direct did not remove the non-FIRE monster-effect candidate during Miragestallio's restriction");
        require(evidence.xyz_fire_restriction_expired,
                "sg09_direct did not expose the non-FIRE monster-effect candidate after the engine-defined expiry boundary");
    } else if (spec.id == "btl01") {
        require(evidence.battle_commands > 0,
                "btl01 did not expose a real MSG_SELECT_BATTLECMD decision");
        require(evidence.battle_attack_selected,
                "btl01 did not submit a legal battle attack command");
        require(evidence.battle_life_points_changed,
                "btl01 did not expose Battle Phase life-point damage (steps=" +
                    std::to_string(evidence.engine_steps) + ", battle_commands=" +
                    std::to_string(evidence.battle_commands) + ", terminal=" +
                    (evidence.battle_terminal ? "true" : "false") + ")");
        require(evidence.battle_card_destroyed,
                "btl01 did not expose Battle Phase card destruction");
        require(evidence.battle_terminal,
                "btl01 did not reach the pinned MSG_WIN terminal path (steps=" +
                    std::to_string(evidence.engine_steps) + ", battle_commands=" +
                    std::to_string(evidence.battle_commands) + ", lp=" +
                    (evidence.battle_life_points_changed ? "true" : "false") + ", destroyed=" +
                    (evidence.battle_card_destroyed ? "true" : "false") + ")");
    } else if (spec.id == "sg20") {
        require(evidence.link_extra_selected,
                "sg20 did not select Sunlight Wolf from the Extra Deck");
        require(evidence.link_material_fire_selected && evidence.link_material_spinny_selected,
                "sg20 did not select both real Link materials");
        require(evidence.link_placement_selected,
                "sg20 did not submit the Link Monster zone placement continuation");
    }
    if (spec.id == "ss01" || spec.id == "ss06") {
        require(evidence.token_state_valid && evidence.chixiao_state_valid,
                spec.id + " never exposed both token and Chixiao public states");
    } else if (spec.id == "sg01") {
        require(evidence.balelynx_state_valid, "sg01 never exposed Balelynx public state");
    } else if (spec.id == "sg02") {
        require(evidence.balelynx_state_valid && evidence.sanctuary_state_valid,
                "sg02 never exposed Balelynx and Sanctuary public states");
    } else if (spec.id == "sg03" || spec.id == "sg04") {
        require(evidence.balelynx_state_valid && evidence.sanctuary_state_valid,
                spec.id + " never exposed the required Balelynx/Sanctuary public states");
    } else if (spec.id == "ss05") {
        require(evidence.chixiao_state_valid && evidence.balelynx_state_valid,
                "ss05 never exposed Chixiao and opposing Balelynx public states");
    } else if (spec.id == "ss14" || spec.id == "ss15" || spec.id == "ss16") {
        require(specialized_state_checked,
                spec.id + " never exposed its expected public fixture state");
    } else {
        require(specialized_state_checked, spec.id + " never exposed its expected public fixture state");
    }
    return evidence;
}

void print_evidence(const FixtureSpec& spec, const Evidence& evidence) {
    std::ostringstream families;
    for (const auto& family : evidence.decision_families) {
        if (families.tellp() != std::streampos(0)) {
            families << ',';
        }
        families << family;
    }
    std::ostringstream observations;
    for (const auto& hash : evidence.observation_hashes) {
        observations << hash;
    }
    const auto observation_bytes = observations.str();
    std::cout << "fixture=" << spec.id << '\n'
              << "engine_steps=" << evidence.engine_steps << '\n'
              << "decisions=" << evidence.decisions << '\n'
              << "terminal=" << (evidence.terminal ? "true" : "false") << '\n'
              << "decision_families=" << families.str() << '\n'
              << "continuation_intermediate_steps=" << evidence.continuation_intermediate_steps << '\n'
              << "balelynx_effect_yes=" << (evidence.balelynx_effect_yes ? "true" : "false") << '\n'
              << "sanctuary_searched=" << (evidence.sanctuary_searched ? "true" : "false") << '\n'
              << "sanctuary_activated=" << (evidence.sanctuary_activated ? "true" : "false") << '\n'
              << "reincarnation_extra_selected=" <<
                     (evidence.reincarnation_extra_selected ? "true" : "false") << '\n'
              << "reincarnation_material_selected=" <<
                     (evidence.reincarnation_material_selected ? "true" : "false") << '\n'
               << "reincarnation_placement_selected=" <<
                      (evidence.reincarnation_placement_selected ? "true" : "false") << '\n'
              << "wolf_st_trigger_activated=" <<
                     (evidence.wolf_st_trigger_activated ? "true" : "false") << '\n'
              << "wolf_st_target_selected=" <<
                     (evidence.wolf_st_target_selected ? "true" : "false") << '\n'
              << "wolf_st_recovered=" <<
                     (evidence.wolf_st_recovered ? "true" : "false") << '\n'
              << "raging_phoenix_reincarnation=" <<
                     (evidence.raging_phoenix_reincarnation ? "true" : "false") << '\n'
              << "raging_phoenix_trigger_activated=" <<
                     (evidence.raging_phoenix_trigger_activated ? "true" : "false") << '\n'
              << "raging_phoenix_search_selected=" <<
                     (evidence.raging_phoenix_search_selected ? "true" : "false") << '\n'
              << "raging_phoenix_search_resolved=" <<
                     (evidence.raging_phoenix_search_resolved ? "true" : "false") << '\n'
              << "pyro_phoenix_reincarnation=" <<
                     (evidence.pyro_phoenix_reincarnation ? "true" : "false") << '\n'
              << "pyro_phoenix_trigger_activated=" <<
                     (evidence.pyro_phoenix_trigger_activated ? "true" : "false") << '\n'
              << "pyro_phoenix_destroyed_opponent=" <<
                     (evidence.pyro_phoenix_destroyed_opponent ? "true" : "false") << '\n'
              << "pyro_phoenix_revive_activated=" <<
                     (evidence.pyro_phoenix_revive_activated ? "true" : "false") << '\n'
              << "pyro_phoenix_revive_target_selected=" <<
                     (evidence.pyro_phoenix_revive_target_selected ? "true" : "false") << '\n'
              << "pyro_phoenix_revived_link=" <<
                     (evidence.pyro_phoenix_revived_link ? "true" : "false") << '\n'
              << "heatleo_reincarnation=" <<
                     (evidence.heatleo_reincarnation ? "true" : "false") << '\n'
              << "heatleo_trigger_activated=" <<
                     (evidence.heatleo_trigger_activated ? "true" : "false") << '\n'
              << "heatleo_target_selected=" <<
                     (evidence.heatleo_target_selected ? "true" : "false") << '\n'
              << "heatleo_target_returned=" <<
                     (evidence.heatleo_target_returned ? "true" : "false") << '\n'
              << "promethean_extra_selected=" <<
                     (evidence.promethean_extra_selected ? "true" : "false") << '\n'
              << "promethean_material_selected=" <<
                     (evidence.promethean_material_selected ? "true" : "false") << '\n'
              << "promethean_placement_selected=" <<
                     (evidence.promethean_placement_selected ? "true" : "false") << '\n'
              << "promethean_fire_revival_activated=" <<
                     (evidence.promethean_fire_revival_activated ? "true" : "false") << '\n'
              << "promethean_fire_target_selected=" <<
                     (evidence.promethean_fire_target_selected ? "true" : "false") << '\n'
              << "promethean_fire_revived=" <<
                     (evidence.promethean_fire_revived ? "true" : "false") << '\n'
              << "hiita_extra_selected=" <<
                     (evidence.hiita_extra_selected ? "true" : "false") << '\n'
              << "hiita_material_selected=" <<
                     (evidence.hiita_material_selected ? "true" : "false") << '\n'
              << "hiita_placement_selected=" <<
                     (evidence.hiita_placement_selected ? "true" : "false") << '\n'
              << "hiita_activation_activated=" <<
                     (evidence.hiita_activation_activated ? "true" : "false") << '\n'
              << "hiita_target_selected=" <<
                     (evidence.hiita_target_selected ? "true" : "false") << '\n'
              << "hiita_revived=" <<
                     (evidence.hiita_revived ? "true" : "false") << '\n'
              << "hiita_no_target_activation_absent=" <<
                     (evidence.hiita_no_target_activation_absent ? "true" : "false") << '\n'
               << "longyuan_discarded=" << (evidence.longyuan_discarded ? "true" : "false") << '\n'
              << "longyuan_burn=" << (evidence.longyuan_burn ? "true" : "false") << '\n'
              << "qixing_state_valid=" << (evidence.qixing_state_valid ? "true" : "false") << '\n'
              << "roar_negate_activated=" <<
                     (evidence.roar_negate_activated ? "true" : "false") << '\n'
              << "roar_negate_chain_resolved=" <<
                     (evidence.roar_negate_chain_resolved ? "true" : "false") << '\n'
              << "roar_negate_target_destroyed=" <<
                     (evidence.roar_negate_target_destroyed ? "true" : "false") << '\n'
              << "roar_recovery_trigger_activated=" <<
                     (evidence.roar_recovery_trigger_activated ? "true" : "false") << '\n'
              << "roar_recovery_set=" <<
                     (evidence.roar_recovery_set ? "true" : "false") << '\n'
              << "rage_activated=" << (evidence.rage_activated ? "true" : "false") << '\n'
              << "rage_cost_sent=" << (evidence.rage_cost_sent ? "true" : "false") << '\n'
              << "rage_target_selected=" <<
                     (evidence.rage_target_selected ? "true" : "false") << '\n'
              << "rage_target_destroyed=" <<
                     (evidence.rage_target_destroyed ? "true" : "false") << '\n'
              << "rage_target_count=" << evidence.rage_target_count << '\n'
              << "chixiao_negation_activated=" <<
                     (evidence.chixiao_negation_activated ? "true" : "false") << '\n'
              << "chixiao_negation_targeted=" <<
                     (evidence.chixiao_negation_targeted ? "true" : "false") << '\n'
              << "chixiao_negation_chain_resolved=" <<
                     (evidence.chixiao_negation_chain_resolved ? "true" : "false") << '\n'
              << "interaction_chain_activated=" <<
                     (evidence.interaction_chain_activated ? "true" : "false") << '\n'
              << "interaction_target_selected=" <<
                     (evidence.interaction_target_selected ? "true" : "false") << '\n'
              << "interaction_chain_resolved=" <<
                     (evidence.interaction_chain_resolved ? "true" : "false") << '\n'
              << "ghost_belle_chain_activated=" <<
                     (evidence.ghost_belle_chain_activated ? "true" : "false") << '\n'
              << "ghost_belle_chain_resolved=" <<
                     (evidence.ghost_belle_chain_resolved ? "true" : "false") << '\n'
              << "chixiao_search_selected=" << (evidence.chixiao_search_selected ? "true" : "false") << '\n'
              << "chixiao_search_state_valid=" << (evidence.chixiao_search_state_valid ? "true" : "false") << '\n'
              << "trigger_chain_resolved=" << (evidence.trigger_chain_resolved ? "true" : "false") << '\n'
              << "blackout_destroyed=" << evidence.blackout_destroyed_codes.size() << '\n'
              << "circle_activated=" << (evidence.circle_activated ? "true" : "false") << '\n'
              << "circle_release_selected=" <<
                     (evidence.circle_release_selected ? "true" : "false") << '\n'
              << "circle_search_selected=" <<
                     (evidence.circle_search_selected ? "true" : "false") << '\n'
              << "circle_search_state_valid=" <<
                     (evidence.circle_search_state_valid ? "true" : "false") << '\n'
              << "ashuna_activated=" << (evidence.ashuna_activated ? "true" : "false") << '\n'
              << "ashuna_deck_target_selected=" <<
                     (evidence.ashuna_deck_target_selected ? "true" : "false") << '\n'
              << "ashuna_deck_state_valid=" <<
                     (evidence.ashuna_deck_state_valid ? "true" : "false") << '\n'
              << "ashuna_extra_available_before=" <<
                     (evidence.ashuna_extra_available_before ? "true" : "false") << '\n'
              << "ashuna_non_wyrm_extra_blocked=" <<
                     (evidence.ashuna_non_wyrm_extra_blocked ? "true" : "false") << '\n'
              << "vishuda_activated=" << (evidence.vishuda_activated ? "true" : "false") << '\n'
              << "vishuda_target_selected=" <<
                     (evidence.vishuda_target_selected ? "true" : "false") << '\n'
              << "adhara_activated=" << (evidence.adhara_activated ? "true" : "false") << '\n'
              << "adhara_target_selected=" <<
                     (evidence.adhara_target_selected ? "true" : "false") << '\n'
              << "adhara_recovery_state_valid=" <<
                     (evidence.adhara_recovery_state_valid ? "true" : "false") << '\n'
              << "gazelle_hand_activated=" <<
                     (evidence.gazelle_hand_activated ? "true" : "false") << '\n'
              << "spinny_discarded=" <<
                     (evidence.spinny_discarded ? "true" : "false") << '\n'
              << "gazelle_deck_send_selected=" <<
                     (evidence.gazelle_deck_send_selected ? "true" : "false") << '\n'
              << "gazelle_deck_state_valid=" <<
                     (evidence.gazelle_deck_state_valid ? "true" : "false") << '\n'
              << "gazelle_chain_resolved=" <<
                     (evidence.gazelle_chain_resolved ? "true" : "false") << '\n'
              << "battle_commands=" << evidence.battle_commands << '\n'
              << "battle_attack_selected=" <<
                     (evidence.battle_attack_selected ? "true" : "false") << '\n'
              << "battle_target_selected=" <<
                     (evidence.battle_target_selected ? "true" : "false") << '\n'
              << "battle_life_points_changed=" <<
                     (evidence.battle_life_points_changed ? "true" : "false") << '\n'
              << "battle_card_destroyed=" <<
                     (evidence.battle_card_destroyed ? "true" : "false") << '\n'
              << "battle_terminal=" << (evidence.battle_terminal ? "true" : "false") << '\n'
              << "link_extra_selected=" <<
                     (evidence.link_extra_selected ? "true" : "false") << '\n'
              << "link_material_fire_selected=" <<
                     (evidence.link_material_fire_selected ? "true" : "false") << '\n'
              << "link_material_spinny_selected=" <<
                     (evidence.link_material_spinny_selected ? "true" : "false") << '\n'
               << "link_placement_selected=" <<
                      (evidence.link_placement_selected ? "true" : "false") << '\n'
              << "jack_link_condition_observed=" <<
                     (evidence.jack_link_condition_observed ? "true" : "false") << '\n'
              << "jack_candidate_present_under_mr5=" <<
                     (evidence.jack_candidate_present_under_mr5 ? "true" : "false") << '\n'
              << "jack_jaguar_activated=" <<
                     (evidence.jack_jaguar_activated ? "true" : "false") << '\n'
              << "jack_jaguar_target_selected=" <<
                     (evidence.jack_jaguar_target_selected ? "true" : "false") << '\n'
              << "jack_jaguar_summoned=" <<
                     (evidence.jack_jaguar_summoned ? "true" : "false") << '\n'
              << "jack_jaguar_target_recycled=" <<
                     (evidence.jack_jaguar_target_recycled ? "true" : "false") << '\n'
              << "xyz_state_valid=" << (evidence.xyz_state_valid ? "true" : "false") << '\n'
              << "xyz_material_count_before=" << evidence.xyz_material_count_before << '\n'
              << "xyz_material_count_after=" << evidence.xyz_material_count_after << '\n'
               << "xyz_material_identity_verified=" <<
                      (evidence.xyz_material_identity_verified ? "true" : "false") << '\n'
               << "xyz_material_identity_redacted=" <<
                      (evidence.xyz_material_identity_redacted ? "true" : "false") << '\n'
               << "xyz_query_before_detach=" <<
                      (evidence.xyz_query_before_detach ? "true" : "false") << '\n'
               << "xyz_query_after_detach=" <<
                      (evidence.xyz_query_after_detach ? "true" : "false") << '\n'
               << "xyz_detached_query_empty=" <<
                      (evidence.xyz_detached_query_empty ? "true" : "false") << '\n'
              << "xyz_detach_accepted=" <<
                     (evidence.xyz_detach_accepted ? "true" : "false") << '\n'
              << "xyz_effect_activated=" <<
                     (evidence.xyz_effect_activated ? "true" : "false") << '\n'
              << "xyz_deck_summon_selected=" <<
                     (evidence.xyz_deck_summon_selected ? "true" : "false") << '\n'
              << "xyz_deck_summoned=" <<
                     (evidence.xyz_deck_summoned ? "true" : "false") << '\n'
              << "xyz_fire_restriction_seen=" <<
                     (evidence.xyz_fire_restriction_seen ? "true" : "false") << '\n'
              << "xyz_fire_restriction_blocked=" <<
                     (evidence.xyz_fire_restriction_blocked ? "true" : "false") << '\n'
              << "xyz_fire_restriction_expired=" <<
                     (evidence.xyz_fire_restriction_expired ? "true" : "false") << '\n'
               << "baxia_extra_selected=" <<
                     (evidence.baxia_extra_selected ? "true" : "false") << '\n'
              << "baxia_token_material_selected=" <<
                     (evidence.baxia_token_material_selected ? "true" : "false") << '\n'
              << "baxia_taia_material_selected=" <<
                     (evidence.baxia_taia_material_selected ? "true" : "false") << '\n'
              << "baxia_summoned=" << (evidence.baxia_summoned ? "true" : "false") << '\n'
              << "baxia_trigger_activated=" <<
                     (evidence.baxia_trigger_activated ? "true" : "false") << '\n'
              << "baxia_trigger_target_count=" << evidence.baxia_trigger_targets.size() << '\n'
              << "baxia_ignition_activated=" <<
                     (evidence.baxia_ignition_activated ? "true" : "false") << '\n'
              << "baxia_destroy_target_selected=" <<
                     (evidence.baxia_destroy_target_selected ? "true" : "false") << '\n'
              << "baxia_revive_target_selected=" <<
                     (evidence.baxia_revive_target_selected ? "true" : "false") << '\n'
              << "baxia_destroyed=" << (evidence.baxia_destroyed ? "true" : "false") << '\n'
              << "baxia_revived=" << (evidence.baxia_revived ? "true" : "false") << '\n'
              << "selected_trigger_chain_sources=";
    for (std::size_t index = 0; index < evidence.selected_trigger_chain_sources.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << evidence.selected_trigger_chain_sources[index];
    }
    std::cout << '\n'
              << "observation_hash_chain_sha256=" << ygo::trace::sha256_bytes(
                     std::vector<std::uint8_t>(observation_bytes.begin(), observation_bytes.end()))
              << '\n';
    if (!evidence.unselect_sources.empty()) {
        std::cout << "unselect_sources=";
        for (std::size_t index = 0; index < evidence.unselect_sources.size(); ++index) {
            if (index != 0) {
                std::cout << ',';
            }
            std::cout << evidence.unselect_sources[index];
        }
        std::cout << '\n';
    }
}

int run(const std::string& id) {
    const auto spec = spec_for(id);
    const auto evidence = run_fixture(spec);
    print_evidence(spec, evidence);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: m3_fixture_test <ss01|ss05|ss06|ss07|ss08|ss10|ss12|ss12_condition|ss14|ss15|ss16|ss18|sg01|sg02|sg03|sg04|sg05|sg06|sg07_jack|sg07_weasel|sg07_falco|sg08_real|sg09_direct|sg11|sg12|sg13|sg14|sg15|sg16_negate|sg16_recovery|sg17|sg18|sg19|sg19_no_target|sg20|btl01|int01|int02|int03|int04|int05>");
        }
        return run(argv[1]);
    } catch (const ygo::protocol::ProtocolError& error) {
        std::cerr << "protocol error: " << error.what() << " type="
                  << static_cast<unsigned>(error.message_type()) << " player="
                  << static_cast<unsigned>(error.player()) << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
