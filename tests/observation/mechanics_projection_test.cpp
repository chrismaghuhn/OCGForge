#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

#include "common.h"
#include "ocgapi_constants.h"
#include "ygo/core/core_host.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/serialization.hpp"

#ifndef YGO_M2_PLAYER_A
#error "YGO_M2_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M2_PLAYER_B
#error "YGO_M2_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M2_CARDSCRIPTS
#error "YGO_M2_CARDSCRIPTS must be supplied by CMake"
#endif
#ifndef YGO_M2_SETUP
#error "YGO_M2_SETUP must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const ygo::observation::ObservedCard* find_card(const ygo::observation::PlayerObservation& observation,
                                                std::uint32_t code,
                                                ygo::observation::SemanticZone zone) {
    for (const auto& entity : observation.entities) {
        if (entity.passcode.value_or(0) == code && entity.zone == zone) {
            return &entity;
        }
    }
    return nullptr;
}

const ygo::observation::ObservedZone* find_zone(const ygo::observation::PlayerObservation& observation,
                                                std::uint8_t player,
                                                ygo::observation::SemanticZone kind) {
    for (const auto& zone : observation.zones) {
        if (zone.player == player && zone.kind == kind) {
            return &zone;
        }
    }
    return nullptr;
}

void verify_null_audit_collector_path(const ygo::core::CoreHost& host) {
    ygo::observation::ObservationBuildConfig observation_config;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    require(observation_config.performance_audit == nullptr,
            "native regression fixture unexpectedly enabled the audit collector");
#endif
    const auto observation = ygo::observation::build_player_observation(host, 0, observation_config);
    require(observation.observation_hash.size() == 64,
            "null audit collector path did not produce a valid observation hash");
}

int run() {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.duel_flags = DUEL_PZONE | DUEL_SEPARATE_PZONE;
    core_config.starting_draw_count = 0;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    ygo::core::CoreHost host(core_config);
    host.load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host.load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host.load_fixture_script(YGO_M2_SETUP);
    host.start_duel();
    (void)host.process();

    verify_null_audit_collector_path(host);
    ygo::observation::ObservationBuildConfig observation_config;
    const auto observation = ygo::observation::build_player_observation(host, 0, observation_config);
    const auto* xyz = find_card(observation, 359563, ygo::observation::SemanticZone::MonsterZone);
    const auto* fusion = find_card(observation, 43227, ygo::observation::SemanticZone::MonsterZone);
    const auto* synchro = find_card(observation, 109401, ygo::observation::SemanticZone::MonsterZone);
    const auto* link = find_card(observation, 146746, ygo::observation::SemanticZone::MonsterZone);
    const auto* pendulum = find_card(observation, 41546, ygo::observation::SemanticZone::PendulumRelevant);
    const auto* pendulum_extra = find_card(observation, 41546, ygo::observation::SemanticZone::ExtraDeck);
    require(xyz != nullptr && fusion != nullptr && synchro != nullptr && link != nullptr && pendulum != nullptr,
            "mechanics fixture cards were not projected");
    require(pendulum_extra != nullptr, "face-up Pendulum Extra Deck card was not projected");
    require(xyz->current.has_value() && xyz->current->rank.value_or(0) == 4 &&
                xyz->current->level == std::nullopt,
            "Xyz rank/level representation is incorrect");
    require(fusion->current->type.value_or(0) & TYPE_FUSION, "Fusion type was not preserved");
    require(synchro->current->level.value_or(0) == 5, "Synchro level was not projected");
    require(link->current->link_rating.value_or(0) == 2 && !link->current->defense.has_value() &&
                link->current->link_markers.size() == 2,
            "Link rating/markers/DEF representation is incorrect");
    require(pendulum->current->left_scale.value_or(0) == 6 && pendulum->current->right_scale.value_or(0) == 6,
            "Pendulum scales were not projected");
    require(xyz->current->counters.size() == 2, "typed counter collection was not projected");
    bool saw_xyz_material = false;
    bool saw_equip = false;
    bool saw_target = false;
    for (const auto& relationship : observation.relationships) {
        saw_xyz_material = saw_xyz_material || relationship.kind == ygo::observation::RelationshipKind::XyzMaterial;
        saw_equip = saw_equip || relationship.kind == ygo::observation::RelationshipKind::Equip;
        saw_target = saw_target || relationship.kind == ygo::observation::RelationshipKind::Target;
    }
    require(saw_xyz_material && saw_equip && saw_target,
            "overlay/equip/target relationship graph was not projected");
    std::size_t known_material_count = 0;
    for (const auto& entity : observation.entities) {
        if (entity.zone == ygo::observation::SemanticZone::Overlay && entity.identity_known &&
            entity.passcode.has_value()) {
            ++known_material_count;
        }
    }
    require(known_material_count == 2,
            "public Xyz material query did not resolve both visible material identities");
    const auto* overlay_zone = find_zone(observation, 0, ygo::observation::SemanticZone::Overlay);
    require(overlay_zone != nullptr && overlay_zone->total_count == 2 &&
                overlay_zone->public_identity_count == 2 && overlay_zone->hidden_count == 0,
            "visible Xyz material entities disagreed with aggregate overlay visibility");
    require(observation.observation_hash.size() == 64, "mechanics observation hash was not produced");
    std::cout << "mechanics_projection=ok\n"
              << "observation_hash=" << observation.observation_hash << '\n';
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
