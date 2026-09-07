#include "i6c6_native_oracle_test.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_host.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/observation_session.hpp"
#include "ygo/trace/sha256.hpp"

#ifndef YGO_I6C6_PLAYER_A
#error "YGO_I6C6_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_I6C6_PLAYER_B
#error "YGO_I6C6_PLAYER_B must be supplied by CMake"
#endif

#ifndef YGO_M0_CARD_DATA_TSV
#error "YGO_M0_CARD_DATA_TSV must be supplied by the native rules bundle"
#endif
#ifndef YGO_M0_CARDSCRIPTS_ROOT
#error "YGO_M0_CARDSCRIPTS_ROOT must be supplied by the native rules bundle"
#endif
#ifndef YGO_M3_RULES_BUNDLE_ID
#error "YGO_M3_RULES_BUNDLE_ID must be supplied by the native rules bundle"
#endif
#ifndef YGO_M3_CORE_PATCHSET_ID
#error "YGO_M3_CORE_PATCHSET_ID must be supplied by the native rules bundle"
#endif
#ifndef YGO_M3_CORE_PATCHSET_SHA256
#error "YGO_M3_CORE_PATCHSET_SHA256 must be supplied by the native rules bundle"
#endif
#ifndef YGO_M0_CORE_COMMIT
#error "YGO_M0_CORE_COMMIT must be supplied by the native rules bundle"
#endif
#ifndef YGO_M0_CARDSCRIPTS_COMMIT
#error "YGO_M0_CARDSCRIPTS_COMMIT must be supplied by the native rules bundle"
#endif
#ifndef YGO_M0_DATABASE_COMMIT
#error "YGO_M0_DATABASE_COMMIT must be supplied by the native rules bundle"
#endif

namespace ygo::i6c6_test {
namespace {

constexpr char kScenarioContractId[] =
    "ocgforge-ignis.i6c6.same-scenario-replay.v1";
constexpr char kScenarioId[] = "ocgforge-ignis.i6c6.startup.fixture.v1";
constexpr char kSetupDescriptorId[] =
    "ocgforge-ignis.i6c6.setup.start-only.v1";
constexpr char kMessageFamilyEnvelopeId[] =
    "ocgforge-ignis.i6c6.supported-message-family-envelope.v1";
constexpr char kIgnisEdoproCommit[] =
    "30935e847165a9ef0e547fb51a43f36168fab7c7";
constexpr char kIgnisEdoproCoreCommit[] =
    "46779fbe40e6a9bd8967f5dc6a03f4eaa6550d57";
constexpr char kIgnisCardscriptsCommit[] =
    "00a828b79303d047d6905f528857cc287ad3a84e";
constexpr char kIgnisBabelcdbCommit[] =
    "2142b4b45e7963fd944940f144951177e87eb15c";
constexpr std::array<std::uint64_t, 4> kSeedWords = {
    0x0123456789abcdefULL,
    0xfedcba9876543210ULL,
    0x13579bdf2468ace0ULL,
    0x0eca8642fdb97531ULL};

class ByteWriter final {
public:
    void u8(const std::uint8_t value) { bytes_.push_back(value); }

    void u32be(const std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void u64be(const std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void string(const std::string& value) {
        u32be(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void raw(const std::vector<std::uint8_t>& value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> take() && { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

template <typename T, typename WriteValue>
void write_optional(ByteWriter& writer, const std::optional<T>& value,
                    WriteValue write_value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        write_value(writer, *value);
    }
}

std::uint8_t zone_code(const observation::SemanticZone value) {
    switch (value) {
    case observation::SemanticZone::Unknown:
        return 0;
    case observation::SemanticZone::MainDeck:
        return 1;
    case observation::SemanticZone::Hand:
        return 2;
    case observation::SemanticZone::MonsterZone:
        return 3;
    case observation::SemanticZone::SpellTrapZone:
        return 4;
    case observation::SemanticZone::Graveyard:
        return 5;
    case observation::SemanticZone::Banished:
        return 6;
    case observation::SemanticZone::ExtraDeck:
        return 7;
    case observation::SemanticZone::FieldZone:
        return 8;
    case observation::SemanticZone::PendulumRelevant:
        return 9;
    case observation::SemanticZone::Overlay:
        return 10;
    }
    throw std::invalid_argument("unknown public semantic zone");
}

std::uint8_t event_code(const observation::VisibleEventKind value) {
    switch (value) {
    case observation::VisibleEventKind::Unknown:
        return 0;
    case observation::VisibleEventKind::TurnStarted:
        return 1;
    case observation::VisibleEventKind::PhaseChanged:
        return 2;
    case observation::VisibleEventKind::CardMoved:
        return 3;
    case observation::VisibleEventKind::CardRevealed:
        return 4;
    case observation::VisibleEventKind::Summoned:
        return 5;
    case observation::VisibleEventKind::Set:
        return 6;
    case observation::VisibleEventKind::Draw:
        return 7;
    case observation::VisibleEventKind::Shuffle:
        return 8;
    case observation::VisibleEventKind::RandomizationBoundary:
        return 9;
    case observation::VisibleEventKind::LifePointsChanged:
        return 10;
    case observation::VisibleEventKind::ChainActivated:
        return 11;
    case observation::VisibleEventKind::ChainResolved:
        return 12;
    case observation::VisibleEventKind::ChainEnded:
        return 13;
    case observation::VisibleEventKind::CardDestroyed:
        return 14;
    case observation::VisibleEventKind::CardBanished:
        return 15;
    case observation::VisibleEventKind::CardReturned:
        return 16;
    case observation::VisibleEventKind::PositionChanged:
        return 17;
    case observation::VisibleEventKind::CounterChanged:
        return 18;
    case observation::VisibleEventKind::Equipped:
        return 19;
    case observation::VisibleEventKind::Unequipped:
        return 20;
    case observation::VisibleEventKind::Targeted:
        return 21;
    case observation::VisibleEventKind::Win:
        return 22;
    }
    throw std::invalid_argument("unknown public visible event kind");
}

void validate_locator(const observation::ObservationLocator& locator) {
    if (locator.value.empty()) {
        throw std::invalid_argument("public locator is not canonical");
    }
    for (const char value : locator.value) {
        if (value == '\0' || value == '\r' || value == '\n') {
            throw std::invalid_argument("public locator is not canonical");
        }
    }
}

void write_optional_locator(
    ByteWriter& writer,
    const std::optional<observation::ObservationLocator>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        validate_locator(*value);
        writer.string(value->value);
    }
}

void write_optional_zone(
    ByteWriter& writer,
    const std::optional<observation::SemanticZone>& value) {
    write_optional(writer, value,
                   [](ByteWriter& target, const observation::SemanticZone zone) {
                       target.u8(zone_code(zone));
                   });
}

std::uint32_t read_u32le(const std::vector<std::uint8_t>& bytes,
                         const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<std::uint32_t>(bytes[offset + 2]) << 16 |
           static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}

struct RawMessage final {
    std::uint64_t ordinal = 0;
    std::uint8_t message_id = 0;
    std::vector<std::uint8_t> payload;
};

std::vector<RawMessage> decode_raw_messages(
    const std::vector<std::vector<std::uint8_t>>& framed_messages) {
    std::vector<RawMessage> result;
    for (const auto& framed : framed_messages) {
        std::size_t offset = 0;
        while (offset < framed.size()) {
            if (framed.size() - offset < sizeof(std::uint32_t)) {
                throw std::invalid_argument("native transcript frame length is truncated");
            }
            const auto frame_length = read_u32le(framed, offset);
            offset += sizeof(std::uint32_t);
            if (frame_length == 0 || frame_length > framed.size() - offset) {
                throw std::invalid_argument("native transcript frame length is invalid");
            }
            RawMessage message;
            message.message_id = framed[offset];
            if (frame_length > 1) {
                message.payload.assign(
                    framed.begin() + static_cast<std::ptrdiff_t>(offset + 1),
                    framed.begin() + static_cast<std::ptrdiff_t>(offset + frame_length));
            }
            result.push_back(std::move(message));
            offset += frame_length;
        }
    }
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index].ordinal = static_cast<std::uint64_t>(index);
    }
    return result;
}

}  // namespace

std::vector<std::uint8_t> canonical_public_event_transcript_bytes(
    const std::vector<observation::VisibleGameEvent>& events) {
    ByteWriter writer;
    const std::string domain(
        "OCGFORGE-IGNIS-I6C6-PUBLIC-EVENT-TRANSCRIPT-V1\0",
        sizeof("OCGFORGE-IGNIS-I6C6-PUBLIC-EVENT-TRANSCRIPT-V1\0") - 1);
    writer.raw(std::vector<std::uint8_t>(domain.begin(), domain.end()));
    writer.u32be(static_cast<std::uint32_t>(events.size()));

    std::optional<std::uint64_t> previous_index;
    for (const auto& event : events) {
        if (previous_index.has_value() && event.event_index <= *previous_index) {
            throw std::invalid_argument("public event indices are not increasing");
        }
        writer.u64be(event.event_index);
        writer.u8(event_code(event.kind));
        write_optional(writer, event.player,
                       [](ByteWriter& target, const std::uint8_t value) {
                           target.u8(value);
                       });
        write_optional_locator(writer, event.entity);
        write_optional(writer, event.public_passcode,
                       [](ByteWriter& target, const std::uint32_t value) {
                           target.u32be(value);
                       });
        write_optional_zone(writer, event.from_zone);
        write_optional_zone(writer, event.to_zone);
        write_optional(writer, event.count,
                       [](ByteWriter& target, const std::uint32_t value) {
                           target.u32be(value);
                       });
        write_optional(writer, event.amount,
                       [](ByteWriter& target, const std::int32_t value) {
                           target.u32be(static_cast<std::uint32_t>(value));
                       });
        write_optional(writer, event.counter_type,
                       [](ByteWriter& target, const std::uint32_t value) {
                           target.u32be(value);
                       });
        write_optional(writer, event.phase,
                       [](ByteWriter& target, const std::uint32_t value) {
                           target.u32be(value);
                       });
        write_optional(writer, event.winner,
                       [](ByteWriter& target, const std::uint8_t value) {
                           target.u8(value);
                       });
        write_optional(writer, event.win_reason,
                       [](ByteWriter& target, const std::uint8_t value) {
                           target.u8(value);
                       });
        write_optional(writer, event.effect_description,
                       [](ByteWriter& target, const std::uint64_t value) {
                           target.u64be(value);
                       });

        std::vector<std::string> targets;
        targets.reserve(event.targets.size());
        for (const auto& target : event.targets) {
            validate_locator(target);
            targets.push_back(target.value);
        }
        std::sort(targets.begin(), targets.end());
        writer.u32be(static_cast<std::uint32_t>(targets.size()));
        for (const auto& target : targets) {
            writer.string(target);
        }
        previous_index = event.event_index;
    }
    return std::move(writer).take();
}

std::vector<std::uint8_t> canonical_restricted_raw_transcript_bytes(
    const std::vector<std::vector<std::uint8_t>>& framed_messages) {
    ByteWriter writer;
    const std::string domain(
        "OCGFORGE-IGNIS-I6C6-GAMEPLAY-TRANSCRIPT-V1\0",
        sizeof("OCGFORGE-IGNIS-I6C6-GAMEPLAY-TRANSCRIPT-V1\0") - 1);
    writer.raw(std::vector<std::uint8_t>(domain.begin(), domain.end()));
    const auto messages = decode_raw_messages(framed_messages);
    writer.u32be(static_cast<std::uint32_t>(messages.size()));
    for (const auto& message : messages) {
        writer.u64be(message.ordinal);
        writer.u8(message.message_id);
        writer.u32be(static_cast<std::uint32_t>(message.payload.size()));
        writer.raw(message.payload);
    }
    return std::move(writer).take();
}

NativeScenarioEvidenceV1 build_native_scenario_evidence(
    const std::uint8_t perspective_player) {
    if (perspective_player > 1) {
        throw std::invalid_argument("I6C6 native perspective must be player 0 or 1");
    }

    const auto deck_a = core::load_fixture_deck(YGO_I6C6_PLAYER_A);
    const auto deck_b = core::load_fixture_deck(YGO_I6C6_PLAYER_B);

    core::CoreHostConfig config;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.card_scripts_root = YGO_M0_CARDSCRIPTS_ROOT;
    config.rules.bundle_id = YGO_M3_RULES_BUNDLE_ID;
    config.rules.core_commit = YGO_M0_CORE_COMMIT;
    config.rules.core_patchset_id = YGO_M3_CORE_PATCHSET_ID;
    config.rules.core_patchset_sha256 = YGO_M3_CORE_PATCHSET_SHA256;
    config.rules.cardscripts_commit = YGO_M0_CARDSCRIPTS_COMMIT;
    config.rules.database_commit = YGO_M0_DATABASE_COMMIT;
    config.duel_flags = 0;
    config.starting_player = 0;
    config.starting_draw_count = 0;
    config.draw_count_per_turn = 0;
    config.seed.words = kSeedWords;

    auto host = std::make_unique<core::CoreHost>(config);
    host->load_deck(0, deck_a);
    host->load_deck(1, deck_b);
    host->start_duel();
    const auto process_result = host->process();
    const std::vector<std::vector<std::uint8_t>> raw_messages = {
        process_result.message};

    observation::ObservationSession event_session(
        perspective_player,
        static_cast<std::uint32_t>(config.duel_flags));
    event_session.ingest(process_result.message, 0);

    observation::ObservationBuildConfig observation_config;
    observation_config.knowledge.own_decklist_known = true;
    observation_config.own_deck.known = true;
    observation_config.own_deck.main_deck = perspective_player == 0
                                                ? deck_a.main_deck
                                                : deck_b.main_deck;
    observation_config.own_deck.extra_deck = perspective_player == 0
                                                 ? deck_a.extra_deck
                                                 : deck_b.extra_deck;
    observation_config.opponent_deck.known = false;
    observation_config.visible_events = event_session.visible_events();
    observation_config.finalization = observation::ObservationFinalization::Immediate;
    const auto observation = observation::build_player_observation(
        *host,
        perspective_player,
        observation_config);
    if (observation.globals.player_to_act.has_value() ||
        observation.decision_context.player.has_value() ||
        observation.decision_context.decision_id.has_value()) {
        throw std::logic_error(
            "I6C6 native boundary unexpectedly contains decision context");
    }

    NativeScenarioEvidenceV1 result;
    result.manifest.scenario_contract_id = kScenarioContractId;
    result.manifest.scenario_id = kScenarioId;
    result.manifest.perspective_player = perspective_player;
    result.manifest.starting_player = config.effective_starting_player();
    result.manifest.duel_flags = config.duel_flags;
    result.manifest.seed_words = config.seed.words;
    result.manifest.seat0_deck_id = "ocgforge.fixture.player_a.deck.v1";
    result.manifest.seat0_deck_sha256 = deck_a.sha256;
    result.manifest.seat1_deck_id = "ocgforge.fixture.player_b.deck.v1";
    result.manifest.seat1_deck_sha256 = deck_b.sha256;
    result.manifest.ocgforge_semantic_commit =
        "f929de0b4d4157327dba003067d2e21e42f7ad75";
    result.manifest.rules_bundle_id = config.rules.bundle_id;
    result.manifest.ocgforge_core_commit = config.rules.core_commit;
    result.manifest.ocgforge_core_patchset_id = config.rules.core_patchset_id;
    result.manifest.ocgforge_core_patchset_sha256 =
        config.rules.core_patchset_sha256;
    result.manifest.ocgforge_cardscripts_commit =
        config.rules.cardscripts_commit;
    result.manifest.native_setup_descriptor_id = kSetupDescriptorId;
    result.manifest.supported_message_family_envelope_id =
        kMessageFamilyEnvelopeId;

    const auto raw_transcript =
        canonical_restricted_raw_transcript_bytes(raw_messages);
    result.manifest.native_raw_transcript_sha256 =
        trace::sha256_bytes(raw_transcript);
    result.canonical_public_event_transcript =
        canonical_public_event_transcript_bytes(event_session.visible_events());
    result.manifest.native_public_event_transcript_sha256 =
        trace::sha256_bytes(result.canonical_public_event_transcript);
    result.manifest.comparison_boundary_public_event_prefix_count =
        static_cast<std::uint64_t>(event_session.visible_events().size());
    result.runtime_provenance.ocgforge_babelcdb_commit =
        config.rules.database_commit;
    result.runtime_provenance.ignis_edopro_commit = kIgnisEdoproCommit;
    result.runtime_provenance.ignis_edopro_core_commit =
        kIgnisEdoproCoreCommit;
    result.runtime_provenance.ignis_cardscripts_commit =
        kIgnisCardscriptsCommit;
    result.runtime_provenance.ignis_babelcdb_commit = kIgnisBabelcdbCommit;
    result.public_observation = observation;
    return result;
}

}  // namespace ygo::i6c6_test

int main() {
    try {
        for (const std::uint8_t perspective : {std::uint8_t{0}, std::uint8_t{1}}) {
            const auto evidence = ygo::i6c6_test::build_native_scenario_evidence(
                perspective);
            if (evidence.public_observation.perspective_player != perspective ||
                evidence.manifest.perspective_player != perspective ||
                evidence.manifest.comparison_boundary_public_event_prefix_count !=
                    evidence.public_observation.visible_events.size()) {
                throw std::logic_error("native scenario perspective binding failed");
            }
            if (evidence.manifest.native_public_event_transcript_sha256 !=
                ygo::trace::sha256_bytes(evidence.canonical_public_event_transcript)) {
                throw std::logic_error("native public event transcript hash failed");
            }
            if (evidence.manifest.scenario_contract_id !=
                    "ocgforge-ignis.i6c6.same-scenario-replay.v1" ||
                evidence.manifest.scenario_id !=
                    "ocgforge-ignis.i6c6.startup.fixture.v1" ||
                evidence.manifest.native_setup_descriptor_id !=
                    "ocgforge-ignis.i6c6.setup.start-only.v1" ||
                evidence.manifest.supported_message_family_envelope_id !=
                    "ocgforge-ignis.i6c6.supported-message-family-envelope.v1" ||
                evidence.manifest.rules_bundle_id != YGO_M3_RULES_BUNDLE_ID ||
                evidence.manifest.ocgforge_core_commit != YGO_M0_CORE_COMMIT ||
                evidence.manifest.ocgforge_cardscripts_commit !=
                    YGO_M0_CARDSCRIPTS_COMMIT ||
                evidence.runtime_provenance.ignis_edopro_commit !=
                    "30935e847165a9ef0e547fb51a43f36168fab7c7" ||
                evidence.runtime_provenance.ignis_edopro_core_commit !=
                    "46779fbe40e6a9bd8967f5dc6a03f4eaa6550d57" ||
                evidence.runtime_provenance.ignis_cardscripts_commit !=
                    "00a828b79303d047d6905f528857cc287ad3a84e" ||
                evidence.runtime_provenance.ignis_babelcdb_commit !=
                    "2142b4b45e7963fd944940f144951177e87eb15c") {
                throw std::logic_error("native scenario provenance binding failed");
            }
            std::cout << "i6c6_native_scenario_evidence_v1"
                      << " scenario_id=" << evidence.manifest.scenario_id
                      << " perspective_player="
                      << static_cast<unsigned>(evidence.manifest.perspective_player)
                      << " public_event_prefix_count="
                      << evidence.manifest.comparison_boundary_public_event_prefix_count
                      << " native_public_event_transcript_sha256="
                      << evidence.manifest.native_public_event_transcript_sha256
                      << '\n';
        }
        std::cout << "i6c6_native_scenario_bridge=pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "i6c6_native_scenario_bridge=fail " << error.what() << '\n';
        return 1;
    }
}
