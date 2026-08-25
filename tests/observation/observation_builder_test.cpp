#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "ocgapi_constants.h"
#include "common.h"
#include "ygo/core/core_host.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/trace/sha256.hpp"

#ifndef YGO_M2_PLAYER_A
#error "YGO_M2_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M2_PLAYER_B
#error "YGO_M2_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M2_CARDSCRIPTS
#error "YGO_M2_CARDSCRIPTS must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
void check_shape_collection_equivalence(const ygo::observation::PlayerObservation& observation,
                                        const char* fixture_name) {
    const auto plain_without_hash = ygo::observation::canonical_serialize_without_hash(observation);
    const auto plain_hash = ygo::observation::observation_hash(observation);
    const auto plain_canonical = ygo::observation::canonical_serialize(observation);
    require(plain_hash == ygo::trace::sha256_string(plain_without_hash),
            "unshaped fixture hash did not match canonical bytes");

    ygo::observation::PerformanceAuditCollector audit;
    std::string shaped_without_hash;
    std::string shaped_hash;
    std::string shaped_canonical;
    {
        auto scope = audit.observation_scope();
        shaped_without_hash = ygo::observation::canonical_serialize_without_hash(observation, &audit);
    }
    {
        auto scope = audit.observation_scope();
        shaped_hash = ygo::observation::observation_hash(observation, &audit);
    }
    {
        auto scope = audit.observation_scope();
        shaped_canonical = ygo::observation::canonical_serialize(observation, &audit);
    }
    require(plain_without_hash == shaped_without_hash,
            "shape instrumentation changed canonical-without-hash fixture bytes");
    require(plain_hash == shaped_hash,
            "shape instrumentation changed fixture observation hash");
    require(plain_canonical == shaped_canonical,
            "shape instrumentation changed final canonical fixture bytes");
    require(shaped_hash == ygo::trace::sha256_string(shaped_without_hash),
            "shaped fixture hash did not match canonical bytes");
    const auto snapshot = audit.snapshot();
    require(snapshot.serialization_shape.lifecycle_context_complete,
            "focused shape fixture lacked an observation lifecycle context");
    require(snapshot.serialization_shape.records_complete,
            "focused shape fixture records were incomplete");

    constexpr std::size_t kTimingRepetitions = 8;
    std::size_t plain_size_sum = 0;
    const auto plain_start = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < kTimingRepetitions; ++index) {
        plain_size_sum += ygo::observation::canonical_serialize_without_hash(observation).size();
    }
    const auto plain_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - plain_start).count();

    std::size_t shaped_size_sum = 0;
    ygo::observation::PerformanceAuditCollector timing_audit;
    const auto shaped_start = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < kTimingRepetitions; ++index) {
        auto scope = timing_audit.observation_scope();
        shaped_size_sum +=
            ygo::observation::canonical_serialize_without_hash(observation, &timing_audit).size();
    }
    const auto shaped_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - shaped_start).count();
    require(plain_size_sum == shaped_size_sum,
            "focused shape timing fixture produced different byte totals");
    std::cout << "m4_3_4_shape_fixture=" << fixture_name
              << " plain_us=" << plain_elapsed
              << " shaped_us=" << shaped_elapsed
              << " repetitions=" << kTimingRepetitions << '\n';
}
#endif

ygo::observation::ObservationBuildConfig make_observation_config() {
    ygo::observation::ObservationBuildConfig config;
    config.own_deck.known = true;
    config.own_deck.main_deck = {32864, 549481, 732302};
    config.own_deck.extra_deck = {146746};
    config.opponent_deck.known = false;
    return config;
}

std::unique_ptr<ygo::core::CoreHost> make_world(std::uint32_t code, std::uint32_t location,
                                                std::uint32_t position, std::uint8_t team = 1) {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 5;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    auto host = std::make_unique<ygo::core::CoreHost>(core_config);
    host->load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host->load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host->load_fixture_card(team, code, location, 0, position);
    host->start_duel();
    (void)host->process();
    return host;
}

std::unique_ptr<ygo::core::CoreHost> make_hidden_deck_world(bool reverse_opponent_deck) {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 0;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    auto host = std::make_unique<ygo::core::CoreHost>(core_config);
    const auto player_a = ygo::core::load_fixture_deck(YGO_M2_PLAYER_A);
    auto player_b = ygo::core::load_fixture_deck(YGO_M2_PLAYER_B);
    if (reverse_opponent_deck) {
        std::reverse(player_b.main_deck.begin(), player_b.main_deck.end());
    }
    host->load_deck(0, player_a);
    host->load_deck(1, player_b);
    host->start_duel();
    (void)host->process();
    return host;
}

std::unique_ptr<ygo::core::CoreHost> make_hidden_hand_world(std::uint32_t code) {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 0;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    auto host = std::make_unique<ygo::core::CoreHost>(core_config);
    host->load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host->load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host->load_fixture_card(1, code, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    host->start_duel();
    (void)host->process();
    return host;
}

int run() {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 5;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    ygo::core::CoreHost host(core_config);
    host.load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host.load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host.start_duel();
    (void)host.process();

    const auto before_engine = ygo::trace::sha256_bytes(host.query_field());
    const auto before_process_count = host.process_call_count();
    const auto config = make_observation_config();
    const auto first = ygo::observation::build_player_observation(host, 0, config);
    const auto second = ygo::observation::build_player_observation(host, 0, config);
    require(ygo::observation::canonical_serialize(first) == ygo::observation::canonical_serialize(second),
            "repeated observations were not canonical-deterministic");
    require(first.observation_hash == second.observation_hash, "repeated observation hash changed");
#ifdef YGO_M4_PERFORMANCE_AUDIT
    {
        ygo::observation::PerformanceAuditCollector lifecycle_audit;
        {
            auto lifecycle_scope = lifecycle_audit.observation_scope();
            (void)ygo::observation::observation_hash(first, &lifecycle_audit);
            (void)ygo::observation::canonical_serialize(first, &lifecycle_audit);
        }
        {
            auto lifecycle_scope = lifecycle_audit.observation_scope();
            (void)ygo::observation::observation_hash(first, &lifecycle_audit);
            lifecycle_audit.record_observation_mutation();
            (void)ygo::observation::observation_hash(first, &lifecycle_audit);
        }
        const auto snapshot = lifecycle_audit.snapshot();
        require(snapshot.serialization_lifecycles.size() == 2,
                "serialization lifecycle records did not preserve observation boundaries");
        require(snapshot.serialization_lifecycles[0].lifecycle_id != 0 &&
                    snapshot.serialization_lifecycles[0].lifecycle_id !=
                        snapshot.serialization_lifecycles[1].lifecycle_id,
                "serialization lifecycle IDs were not unique and nonzero");
        require(snapshot.serialization_lifecycles[0].serialize_without_hash_calls == 2 &&
                    snapshot.serialization_lifecycles[0].canonical_serialize_calls == 1 &&
                    snapshot.serialization_lifecycles[0].sha256_calls == 2,
                "serialization consumer calls were not counted separately");
        require(snapshot.serialization_lifecycles[0].same_mutation_epoch_duplicate_calls == 1,
                "same-state serialization was not classified as duplicate materialization");
        require(snapshot.serialization_lifecycles[1].serialize_without_hash_calls == 2 &&
                    snapshot.serialization_lifecycles[1].same_mutation_epoch_duplicate_calls == 0,
                "mutation epoch did not separate repeated serialization from duplicate work");
        std::cout << "m4_3_2_lifecycle_diagnostics=ok\n"
                  << "direct_lifecycle_id=" << snapshot.serialization_lifecycles[0].lifecycle_id << '\n'
                  << "direct_serialize_without_hash_calls="
                  << snapshot.serialization_lifecycles[0].serialize_without_hash_calls << '\n'
                  << "direct_sha256_calls=" << snapshot.serialization_lifecycles[0].sha256_calls << '\n'
                  << "direct_canonical_serialize_calls="
                  << snapshot.serialization_lifecycles[0].canonical_serialize_calls << '\n'
                  << "direct_same_epoch_duplicate_calls="
                  << snapshot.serialization_lifecycles[0].same_mutation_epoch_duplicate_calls << '\n'
                  << "mutated_lifecycle_id=" << snapshot.serialization_lifecycles[1].lifecycle_id << '\n'
                  << "mutated_same_epoch_duplicate_calls="
                  << snapshot.serialization_lifecycles[1].same_mutation_epoch_duplicate_calls << '\n';
    }
    {
        ygo::observation::PerformanceAuditCollector contract_audit;
        auto lifecycle_scope = contract_audit.observation_scope();
        const auto without_hash = ygo::observation::canonical_serialize_without_hash(first, &contract_audit);
        const auto observed_hash = ygo::observation::observation_hash(first, &contract_audit);
        const auto canonical = ygo::observation::canonical_serialize(first, &contract_audit);
        const auto expected_hash = ygo::trace::sha256_string(without_hash);
        const auto expected_canonical =
            without_hash.substr(0, without_hash.size() - 1) + ",\"observation_hash\":" +
            "\"" + expected_hash + "\"}\n";
        require(observed_hash == expected_hash,
                "observation_hash did not hash the exact canonical-without-hash bytes");
        require(canonical == expected_canonical,
                "canonical_serialize did not append the hash to the exact canonical-without-hash bytes");
    }
#endif
    require(before_engine == ygo::trace::sha256_bytes(host.query_field()),
            "observation changed engine state");
    require(before_process_count == host.process_call_count(), "observation advanced engine processing");

    // M4.3.1 characterization: the eager builder hash is valid for the
    // pre-context observation, then attach_decision_context mutates the
    // observation and produces a different final hash. The intermediate hash
    // is intentionally recorded as evidence only; the regression contract is
    // the final bytes/hash after context attachment.
    auto characterized = ygo::observation::build_player_observation(host, 0, config);
    const auto intermediate_canonical_bytes = ygo::observation::canonical_serialize(characterized);
    const auto intermediate_hash = characterized.observation_hash;
    ygo::protocol::DecisionRequest characterization_request;
    characterization_request.kind = ygo::protocol::DecisionRequestKind::BattleCommand;
    characterization_request.decision_id = "m4-3-1-characterization";
    characterization_request.engine_step_index = 17;
    characterization_request.player = 0;
    characterization_request.engine_message_type = MSG_SELECT_BATTLECMD;
    characterization_request.engine_message_name = "select_battlecmd";
    ygo::protocol::ActionCandidate characterization_candidate;
    characterization_candidate.semantic_key = "m4-3-1-visible-decision";
    const auto visible_hand = std::find_if(first.entities.begin(), first.entities.end(), [](const auto& entity) {
        return entity.controller.value_or(2) == 0 &&
               entity.zone == ygo::observation::SemanticZone::Hand && entity.identity_known &&
               entity.passcode.has_value() && entity.sequence.has_value();
    });
    require(visible_hand != first.entities.end(), "characterization fixture lacks a visible hand entity");
    characterization_candidate.source_card = visible_hand->passcode.value();
    characterization_candidate.source_controller = 0;
    characterization_candidate.source_location = LOCATION_HAND;
    characterization_candidate.source_sequence = visible_hand->sequence.value();
    characterization_request.candidates.push_back(characterization_candidate);
    require(ygo::observation::candidate_observation_consistent(characterized, characterization_candidate),
            "characterization candidate did not resolve against the eager observation");
    ygo::observation::attach_decision_context(characterized, characterization_request);
    const auto final_canonical_bytes = ygo::observation::canonical_serialize(characterized);
    const auto final_hash = characterized.observation_hash;
    require(intermediate_hash != final_hash,
            "decision context attachment did not invalidate the eager intermediate hash");
    require(intermediate_canonical_bytes != final_canonical_bytes,
            "decision context attachment did not change canonical observation bytes");
    require(final_hash == ygo::observation::observation_hash(characterized),
            "characterized final observation hash did not match the final observation");
    std::cout << "m4_3_1_characterization=ok\n"
              << "intermediate_hash=" << intermediate_hash << '\n'
              << "final_hash=" << final_hash << '\n'
              << "final_canonical_bytes_length=" << final_canonical_bytes.size() << '\n'
              << "final_canonical_bytes_sha256=" << ygo::trace::sha256_string(final_canonical_bytes) << '\n'
              << "final_canonical_bytes=" << final_canonical_bytes;

    auto deferred_config = config;
    deferred_config.finalization = ygo::observation::ObservationFinalization::Deferred;
    auto deferred = ygo::observation::build_player_observation(host, 0, deferred_config);
    require(deferred.observation_hash.empty(),
            "deferred observation was indistinguishable from an already-finalized observation");
    ygo::observation::attach_decision_context(deferred, characterization_request);
    require(ygo::observation::canonical_serialize(deferred) == final_canonical_bytes,
            "deferred decision observation changed final canonical bytes");
    require(deferred.observation_hash == final_hash,
            "deferred decision observation changed final observation hash");
    require(deferred.observation_hash == ygo::observation::observation_hash(deferred),
            "deferred final observation hash did not match its canonical bytes");
    std::cout << "m4_3_1_deferred_equivalence=ok\n";

    const auto compare_finalization = [&](const ygo::core::CoreHost& fixture,
                                          std::uint8_t perspective,
                                          const ygo::observation::ObservationBuildConfig& base_config,
                                          const ygo::protocol::DecisionRequest& request) {
        auto eager_config = base_config;
        eager_config.finalization = ygo::observation::ObservationFinalization::Immediate;
        auto eager_observation = ygo::observation::build_player_observation(fixture, perspective, eager_config);
        ygo::observation::attach_decision_context(eager_observation, request);
        const auto eager_bytes = ygo::observation::canonical_serialize(eager_observation);

        auto deferred_build_config = base_config;
        deferred_build_config.finalization = ygo::observation::ObservationFinalization::Deferred;
        auto deferred_observation =
            ygo::observation::build_player_observation(fixture, perspective, deferred_build_config);
        require(deferred_observation.observation_hash.empty(),
                "deferred fixture observation exposed a non-final hash before context attachment");
        ygo::observation::attach_decision_context(deferred_observation, request);
        const auto deferred_bytes = ygo::observation::canonical_serialize(deferred_observation);
        require(eager_bytes == deferred_bytes, "eager and deferred final observation bytes diverged");
        require(eager_observation.observation_hash == deferred_observation.observation_hash,
                "eager and deferred final observation hashes diverged");
        require(eager_observation.observation_hash == ygo::observation::observation_hash(eager_observation) &&
                    deferred_observation.observation_hash == ygo::observation::observation_hash(deferred_observation),
                "final observation hash did not match canonical hash input");
    };

    auto perspective_request = characterization_request;
    perspective_request.decision_id = "m4-3-1-perspective-1";
    perspective_request.player = 1;
    perspective_request.candidates.clear();
    perspective_request.kind = ygo::protocol::DecisionRequestKind::Option;
    compare_finalization(host, 0, config, characterization_request);
    compare_finalization(host, 1, config, perspective_request);

    auto continuation_request = perspective_request;
    continuation_request.decision_id = "m4-3-1-continuation";
    continuation_request.kind = ygo::protocol::DecisionRequestKind::CardSelection;
    ygo::protocol::SelectionContinuation continuation;
    continuation.continuation_id = "m4-3-1-continuation-id";
    continuation.continuation_kind = ygo::protocol::ContinuationKind::UnorderedSelection;
    continuation.continuation_step = 1;
    continuation.original_message_type = MSG_SELECT_CARD;
    continuation_request.continuation = continuation;
    compare_finalization(host, 1, config, continuation_request);
    require(config.finalization == ygo::observation::ObservationFinalization::Immediate,
            "ObservationBuildConfig default finalization changed from Immediate");

    bool own_hand_visible = false;
    bool opponent_hand_entity = false;
    for (const auto& entity : first.entities) {
        require(entity.zone != ygo::observation::SemanticZone::MainDeck,
                "current Main Deck entity leaked into observation");
        if (entity.zone == ygo::observation::SemanticZone::Hand && entity.controller.value_or(2) == 0) {
            own_hand_visible = own_hand_visible || entity.identity_known;
        }
        if (entity.zone == ygo::observation::SemanticZone::Hand && entity.controller.value_or(2) == 1) {
            opponent_hand_entity = true;
        }
    }
    require(own_hand_visible, "own hand identity was over-redacted");
    require(!opponent_hand_entity, "opponent hidden hand entity leaked");
    require(!first.match_context.opponent_deck.known, "opponent decklist defaulted to omniscient");
    require(first.match_context.own_deck.known, "own static deck knowledge was not retained");

    const auto mirrored = ygo::observation::build_player_observation(host, 1, config);
    require(mirrored.perspective_player == 1, "mirrored perspective was not explicit");
    require(mirrored.observation_hash != first.observation_hash,
            "opposite perspective collapsed to the same observation");

    const auto hidden_field_a = make_world(146746, LOCATION_MZONE, POS_FACEDOWN_DEFENSE);
    const auto hidden_field_b = make_world(41546, LOCATION_MZONE, POS_FACEDOWN_DEFENSE);
    const auto hidden_a = ygo::observation::build_player_observation(*hidden_field_a, 0, config);
    const auto hidden_b = ygo::observation::build_player_observation(*hidden_field_b, 0, config);
    require(ygo::observation::canonical_serialize(hidden_a) == ygo::observation::canonical_serialize(hidden_b),
            "hidden opponent field identity changed the player observation");
    require(hidden_a.observation_hash == hidden_b.observation_hash,
            "hidden opponent field identity changed the observation hash");

    const auto hidden_hand_a = make_hidden_hand_world(146746);
    const auto hidden_hand_b = make_hidden_hand_world(359563);
    const auto hidden_hand_observation_a = ygo::observation::build_player_observation(*hidden_hand_a, 0, config);
    const auto hidden_hand_observation_b = ygo::observation::build_player_observation(*hidden_hand_b, 0, config);
    require(ygo::observation::canonical_serialize(hidden_hand_observation_a) ==
                ygo::observation::canonical_serialize(hidden_hand_observation_b),
            "hidden opponent Hand identity changed the player observation");
    require(hidden_hand_observation_a.observation_hash == hidden_hand_observation_b.observation_hash,
            "hidden opponent Hand identity changed the observation hash");
    for (const auto perspective : {std::uint8_t{0}, std::uint8_t{1}}) {
        auto hidden_request = perspective_request;
        hidden_request.decision_id = "m4-3-1-hidden-perspective-" + std::to_string(perspective);
        hidden_request.player = perspective;
        compare_finalization(*hidden_hand_a, perspective, config, hidden_request);
    }

    const auto revealed_field_a = make_world(146746, LOCATION_MZONE, POS_FACEUP_ATTACK);
    const auto revealed_field_b = make_world(41546, LOCATION_MZONE, POS_FACEUP_ATTACK);
    const auto revealed_a = ygo::observation::build_player_observation(*revealed_field_a, 0, config);
    const auto revealed_b = ygo::observation::build_player_observation(*revealed_field_b, 0, config);
    require(ygo::observation::canonical_serialize(revealed_a) != ygo::observation::canonical_serialize(revealed_b),
            "public field reveal did not change the player observation");

    const auto hidden_extra_a = make_world(146746, LOCATION_EXTRA, POS_FACEDOWN_DEFENSE);
    const auto hidden_extra_b = make_world(359563, LOCATION_EXTRA, POS_FACEDOWN_DEFENSE);
    const auto extra_a = ygo::observation::build_player_observation(*hidden_extra_a, 0, config);
    const auto extra_b = ygo::observation::build_player_observation(*hidden_extra_b, 0, config);
    const auto extra_bytes_a = ygo::observation::canonical_serialize(extra_a);
    const auto extra_bytes_b = ygo::observation::canonical_serialize(extra_b);
    require(extra_bytes_a == extra_bytes_b,
            "hidden opponent Extra Deck identity changed the player observation");
    const auto public_extra_a = make_world(146746, LOCATION_EXTRA, POS_FACEUP_ATTACK);
    const auto public_extra_b = make_world(359563, LOCATION_EXTRA, POS_FACEUP_ATTACK);
    const auto public_a = ygo::observation::build_player_observation(*public_extra_a, 0, config);
    const auto public_b = ygo::observation::build_player_observation(*public_extra_b, 0, config);
    require(ygo::observation::canonical_serialize(public_a) != ygo::observation::canonical_serialize(public_b),
            "public Extra Deck reveal did not change the player observation");

    const auto hidden_deck_a = make_hidden_deck_world(false);
    const auto hidden_deck_b = make_hidden_deck_world(true);
    for (const auto perspective : {std::uint8_t{0}, std::uint8_t{1}}) {
        const auto observation_a = ygo::observation::build_player_observation(*hidden_deck_a, perspective, config);
        const auto observation_b = ygo::observation::build_player_observation(*hidden_deck_b, perspective, config);
        require(ygo::observation::canonical_serialize(observation_a) ==
                    ygo::observation::canonical_serialize(observation_b),
                "hidden deck order changed the perspective-safe observation");
        require(observation_a.observation_hash == observation_b.observation_hash,
                "hidden deck order changed the perspective-safe observation hash");

        auto paired_request = perspective_request;
        paired_request.decision_id = "m4-3-1-paired-world-" + std::to_string(perspective);
        paired_request.player = perspective;
        compare_finalization(*hidden_deck_a, perspective, config, paired_request);
        auto paired_config = config;
        paired_config.finalization = ygo::observation::ObservationFinalization::Immediate;
        auto paired_a = ygo::observation::build_player_observation(*hidden_deck_a, perspective, paired_config);
        auto paired_b = ygo::observation::build_player_observation(*hidden_deck_b, perspective, paired_config);
        ygo::observation::attach_decision_context(paired_a, paired_request);
        ygo::observation::attach_decision_context(paired_b, paired_request);
        require(ygo::observation::canonical_serialize(paired_a) ==
                    ygo::observation::canonical_serialize(paired_b),
                "paired hidden worlds diverged after decision context attachment");
        require(paired_a.observation_hash == paired_b.observation_hash,
                "paired hidden worlds changed final observation hash after context attachment");
    }

#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
    // M4.3.4 focused instrumentation gate. The shaped overload is compared
    // with the same serializer with collection disabled; only timing and
    // diagnostics are allowed to differ.
    check_shape_collection_equivalence(first, "ordinary_visible");
    check_shape_collection_equivalence(mirrored, "ordinary_visible_perspective_1");
    check_shape_collection_equivalence(hidden_hand_observation_a, "hidden_information_perspective_0");
    const auto hidden_hand_perspective_1 =
        ygo::observation::build_player_observation(*hidden_hand_a, 1, config);
    check_shape_collection_equivalence(hidden_hand_perspective_1, "hidden_information_perspective_1");

    auto paired_shape = ygo::observation::build_player_observation(*hidden_deck_a, 0, config);
    ygo::observation::attach_decision_context(paired_shape, perspective_request);
    check_shape_collection_equivalence(paired_shape, "paired_world_privacy");

    auto terminal_shape = first;
    terminal_shape.globals.terminal = true;
    terminal_shape.globals.winner = std::uint8_t{0};
    terminal_shape.globals.win_reason = std::uint8_t{0};
    terminal_shape.decision_context = {};
    terminal_shape.observation_hash.clear();
    check_shape_collection_equivalence(terminal_shape, "terminal");
    std::cout << "m4_3_4_shape_equivalence=ok\n";
#endif

    const auto own_set = make_world(146746, LOCATION_MZONE, POS_FACEDOWN_DEFENSE, 0);
    const auto own_set_observation = ygo::observation::build_player_observation(*own_set, 0, config);
    const auto own_set_card = std::find_if(
        own_set_observation.entities.begin(), own_set_observation.entities.end(), [](const auto& entity) {
            return entity.zone == ygo::observation::SemanticZone::MonsterZone &&
                   entity.controller.value_or(2) == 0 && entity.identity_known &&
                   entity.passcode.value_or(0) == 146746;
        });
    require(own_set_card != own_set_observation.entities.end(),
            "own face-down card identity was over-redacted");
    std::cout << "observation_builder=ok\n"
              << "player0_hash=" << first.observation_hash << '\n'
              << "player1_hash=" << mirrored.observation_hash << '\n';
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
