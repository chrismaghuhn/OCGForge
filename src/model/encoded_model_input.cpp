#include "ygo/model/encoded_model_input.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::model {
namespace {

enum class FailureReason : std::uint8_t {
    InvalidLogical,
    UnknownPublicPasscode,
    InvalidEncoded,
};

class EncodingFailure final {
public:
    explicit EncodingFailure(const FailureReason reason) : reason_(reason) {}

    FailureReason reason() const noexcept { return reason_; }

private:
    FailureReason reason_;
};

[[noreturn]] void fail(const FailureReason reason) {
    throw EncodingFailure(reason);
}

[[noreturn]] void fail_logical() { fail(FailureReason::InvalidLogical); }
[[noreturn]] void fail_unknown_passcode() { fail(FailureReason::UnknownPublicPasscode); }
[[noreturn]] void fail_encoded() { fail(FailureReason::InvalidEncoded); }

class Writer final {
public:
    void u8(const std::uint8_t value) { bytes_.push_back(value); }

    void u16be(const std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void u32be(const std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 24));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void u64be(const std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void i32(const std::int32_t value) { u32be(static_cast<std::uint32_t>(value)); }
    void boolean(const bool value) { u8(value ? 1 : 0); }

    void string(const std::string_view value) {
        count(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void bytes(const std::vector<std::uint8_t>& value) {
        count(value.size());
        raw(value);
    }

    void raw(const std::vector<std::uint8_t>& value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> take() && noexcept { return std::move(bytes_); }

private:
    void count(const std::size_t value) {
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("model input field exceeds u32 length");
        }
        u32be(static_cast<std::uint32_t>(value));
    }

    std::vector<std::uint8_t> bytes_;
};

bool valid_utf8(const std::string_view value) noexcept {
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }

        std::size_t width = 0;
        std::uint32_t code_point = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
            code_point = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            code_point = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            code_point = first & 0x07;
        } else {
            return false;
        }

        if (width > value.size() - index) {
            return false;
        }
        for (std::size_t continuation = 1; continuation < width; ++continuation) {
            const auto byte = static_cast<unsigned char>(value[index + continuation]);
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (byte & 0x3f);
        }
        if ((width == 3 && code_point < 0x800) ||
            (width == 4 && code_point < 0x10000) || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        index += width;
    }
    return true;
}

bool valid_locator(const std::string_view value) noexcept {
    return !value.empty() && valid_utf8(value) &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               const auto byte = static_cast<unsigned char>(character);
               return byte >= 0x20 && byte != 0x7f;
           });
}

bool valid_digest(const std::string_view value) noexcept {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool byte_less(const std::string_view left, const std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](const char left_byte, const char right_byte) {
            return static_cast<unsigned char>(left_byte) <
                   static_cast<unsigned char>(right_byte);
        });
}

bool fits_u32_count(const std::size_t value) noexcept {
    return value <= std::numeric_limits<std::uint32_t>::max();
}

template <typename T>
void optional_u8(Writer& writer, const std::optional<T>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.u8(static_cast<std::uint8_t>(*value));
    }
}

void optional_u16(Writer& writer, const std::optional<std::uint16_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.u16be(*value);
    }
}

void optional_u32(Writer& writer, const std::optional<std::uint32_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.u32be(*value);
    }
}

void optional_u64(Writer& writer, const std::optional<std::uint64_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.u64be(*value);
    }
}

void optional_i32(Writer& writer, const std::optional<std::int32_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.i32(*value);
    }
}

void optional_string(Writer& writer, const std::optional<std::string>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.string(*value);
    }
}

std::uint8_t semantic_zone_code(const ygo::observation::SemanticZone value) {
    using ygo::observation::SemanticZone;
    switch (value) {
    case SemanticZone::Unknown:
        return 0;
    case SemanticZone::MainDeck:
        return 1;
    case SemanticZone::Hand:
        return 2;
    case SemanticZone::MonsterZone:
        return 3;
    case SemanticZone::SpellTrapZone:
        return 4;
    case SemanticZone::Graveyard:
        return 5;
    case SemanticZone::Banished:
        return 6;
    case SemanticZone::ExtraDeck:
        return 7;
    case SemanticZone::FieldZone:
        return 8;
    case SemanticZone::PendulumRelevant:
        return 9;
    case SemanticZone::Overlay:
        return 10;
    }
    fail_logical();
}

std::uint8_t position_code(const ygo::observation::Position value) {
    using ygo::observation::Position;
    switch (value) {
    case Position::Unknown:
        return 0;
    case Position::FaceUpAttack:
        return 1;
    case Position::FaceDownAttack:
        return 2;
    case Position::FaceUpDefense:
        return 4;
    case Position::FaceDownDefense:
        return 8;
    }
    fail_logical();
}

std::uint8_t link_marker_code(const ygo::observation::LinkMarker value) {
    using ygo::observation::LinkMarker;
    switch (value) {
    case LinkMarker::BottomLeft:
        return 0;
    case LinkMarker::Bottom:
        return 1;
    case LinkMarker::BottomRight:
        return 2;
    case LinkMarker::Left:
        return 3;
    case LinkMarker::Right:
        return 4;
    case LinkMarker::TopLeft:
        return 5;
    case LinkMarker::Top:
        return 6;
    case LinkMarker::TopRight:
        return 7;
    }
    fail_logical();
}

std::uint8_t relationship_kind_code(const ygo::observation::RelationshipKind value) {
    using ygo::observation::RelationshipKind;
    switch (value) {
    case RelationshipKind::XyzMaterial:
        return 0;
    case RelationshipKind::Equip:
        return 1;
    case RelationshipKind::Target:
        return 2;
    }
    fail_logical();
}

std::uint8_t visible_event_kind_code(const ygo::observation::VisibleEventKind value) {
    using ygo::observation::VisibleEventKind;
    switch (value) {
    case VisibleEventKind::Unknown:
        return 0;
    case VisibleEventKind::TurnStarted:
        return 1;
    case VisibleEventKind::PhaseChanged:
        return 2;
    case VisibleEventKind::CardMoved:
        return 3;
    case VisibleEventKind::CardRevealed:
        return 4;
    case VisibleEventKind::Summoned:
        return 5;
    case VisibleEventKind::Set:
        return 6;
    case VisibleEventKind::Draw:
        return 7;
    case VisibleEventKind::Shuffle:
        return 8;
    case VisibleEventKind::RandomizationBoundary:
        return 9;
    case VisibleEventKind::LifePointsChanged:
        return 10;
    case VisibleEventKind::ChainActivated:
        return 11;
    case VisibleEventKind::ChainResolved:
        return 12;
    case VisibleEventKind::ChainEnded:
        return 13;
    case VisibleEventKind::CardDestroyed:
        return 14;
    case VisibleEventKind::CardBanished:
        return 15;
    case VisibleEventKind::CardReturned:
        return 16;
    case VisibleEventKind::PositionChanged:
        return 17;
    case VisibleEventKind::CounterChanged:
        return 18;
    case VisibleEventKind::Equipped:
        return 19;
    case VisibleEventKind::Unequipped:
        return 20;
    case VisibleEventKind::Targeted:
        return 21;
    case VisibleEventKind::Win:
        return 22;
    }
    fail_logical();
}

std::uint8_t public_choice_code(const ygo::environment::PublicChoiceKind value) {
    using ygo::environment::PublicChoiceKind;
    switch (value) {
    case PublicChoiceKind::YesNo:
        return 1;
    case PublicChoiceKind::EffectYesNo:
        return 2;
    case PublicChoiceKind::EffectChoice:
        return 3;
    case PublicChoiceKind::OptionValue:
        return 4;
    case PublicChoiceKind::AnnouncementNumber:
        return 5;
    }
    fail_logical();
}

std::uint8_t public_reference_code(const ygo::environment::PublicCardReferenceKind value) {
    using ygo::environment::PublicCardReferenceKind;
    switch (value) {
    case PublicCardReferenceKind::VisibleCard:
        return 0;
    case PublicCardReferenceKind::RedactedSlot:
        return 1;
    }
    fail_logical();
}

std::uint16_t action_kind_code(const ygo::environment::EnvironmentActionKind value) {
    using ygo::environment::EnvironmentActionKind;
    switch (value) {
    case EnvironmentActionKind::IdleCommand:
        return 1;
    case EnvironmentActionKind::BattleCommand:
        return 2;
    case EnvironmentActionKind::Chain:
        return 3;
    case EnvironmentActionKind::Option:
        return 4;
    case EnvironmentActionKind::CardSelection:
        return 5;
    case EnvironmentActionKind::Announcement:
        return 6;
    case EnvironmentActionKind::Place:
        return 7;
    case EnvironmentActionKind::Position:
        return 8;
    case EnvironmentActionKind::YesNo:
        return 9;
    case EnvironmentActionKind::Pick:
        return 10;
    case EnvironmentActionKind::Finish:
        return 11;
    case EnvironmentActionKind::Cancel:
        return 12;
    case EnvironmentActionKind::AssignAmount:
        return 13;
    case EnvironmentActionKind::Unsupported:
        break;
    }
    fail_logical();
}

std::string_view action_kind_token(const ygo::environment::EnvironmentActionKind value) {
    using ygo::environment::EnvironmentActionKind;
    switch (value) {
    case EnvironmentActionKind::IdleCommand:
        return "idle_command";
    case EnvironmentActionKind::BattleCommand:
        return "battle_command";
    case EnvironmentActionKind::Chain:
        return "chain";
    case EnvironmentActionKind::Option:
        return "option";
    case EnvironmentActionKind::CardSelection:
        return "card_selection";
    case EnvironmentActionKind::Announcement:
        return "announcement";
    case EnvironmentActionKind::Place:
        return "place";
    case EnvironmentActionKind::Position:
        return "position";
    case EnvironmentActionKind::YesNo:
        return "yes_no";
    case EnvironmentActionKind::Pick:
        return "pick";
    case EnvironmentActionKind::Finish:
        return "finish";
    case EnvironmentActionKind::Cancel:
        return "cancel";
    case EnvironmentActionKind::AssignAmount:
        return "assign_amount";
    case EnvironmentActionKind::Unsupported:
        break;
    }
    fail_logical();
}

std::string_view action_kind_token_from_code(const std::uint16_t value) {
    switch (value) {
    case 1:
        return "idle_command";
    case 2:
        return "battle_command";
    case 3:
        return "chain";
    case 4:
        return "option";
    case 5:
        return "card_selection";
    case 6:
        return "announcement";
    case 7:
        return "place";
    case 8:
        return "position";
    case 9:
        return "yes_no";
    case 10:
        return "pick";
    case 11:
        return "finish";
    case 12:
        return "cancel";
    case 13:
        return "assign_amount";
    default:
        fail_encoded();
    }
}

std::optional<std::uint16_t> request_kind_code(const std::string_view value) {
    if (value == "idle_command") return std::uint16_t{1};
    if (value == "battle_command") return std::uint16_t{2};
    if (value == "chain") return std::uint16_t{3};
    if (value == "option") return std::uint16_t{4};
    if (value == "card_selection") return std::uint16_t{5};
    if (value == "tribute") return std::uint16_t{6};
    if (value == "sum") return std::uint16_t{7};
    if (value == "place") return std::uint16_t{8};
    if (value == "counter") return std::uint16_t{9};
    if (value == "ordering") return std::uint16_t{10};
    if (value == "announcement") return std::uint16_t{11};
    if (value == "unselect_card") return std::uint16_t{12};
    if (value == "position") return std::uint16_t{13};
    if (value == "yes_no") return std::uint16_t{14};
    return std::nullopt;
}

std::string_view request_kind_token(const std::uint16_t value) {
    switch (value) {
    case 1:
        return "idle_command";
    case 2:
        return "battle_command";
    case 3:
        return "chain";
    case 4:
        return "option";
    case 5:
        return "card_selection";
    case 6:
        return "tribute";
    case 7:
        return "sum";
    case 8:
        return "place";
    case 9:
        return "counter";
    case 10:
        return "ordering";
    case 11:
        return "announcement";
    case 12:
        return "unselect_card";
    case 13:
        return "position";
    case 14:
        return "yes_no";
    default:
        fail_encoded();
    }
}

std::uint8_t continuation_code(const std::string_view value) {
    if (value.empty()) return 0;
    if (value == "pick") return 1;
    if (value == "amount") return 2;
    if (value == "finish") return 3;
    if (value == "cancel") return 4;
    if (value == "bypass") return 5;
    fail_logical();
}

ygo::environment::PublicChoiceKind public_choice_kind_from_code(
    const std::uint8_t value) {
    using ygo::environment::PublicChoiceKind;
    switch (value) {
    case 1:
        return PublicChoiceKind::YesNo;
    case 2:
        return PublicChoiceKind::EffectYesNo;
    case 3:
        return PublicChoiceKind::EffectChoice;
    case 4:
        return PublicChoiceKind::OptionValue;
    case 5:
        return PublicChoiceKind::AnnouncementNumber;
    default:
        fail_encoded();
    }
}

ygo::environment::PublicCardReferenceKind public_reference_kind_from_code(
    const std::uint8_t value) {
    using ygo::environment::PublicCardReferenceKind;
    switch (value) {
    case 0:
        return PublicCardReferenceKind::VisibleCard;
    case 1:
        return PublicCardReferenceKind::RedactedSlot;
    default:
        fail_encoded();
    }
}

std::string_view continuation_token(const std::uint8_t value) {
    switch (value) {
    case 0:
        return {};
    case 1:
        return "pick";
    case 2:
        return "amount";
    case 3:
        return "finish";
    case 4:
        return "cancel";
    case 5:
        return "bypass";
    default:
        fail_encoded();
    }
}

bool valid_choice(const ygo::environment::PublicChoice& choice) noexcept {
    using ygo::environment::PublicChoiceKind;
    switch (choice.kind) {
    case PublicChoiceKind::YesNo:
    case PublicChoiceKind::EffectYesNo:
        return choice.value <= 1 && !choice.response_index.has_value();
    case PublicChoiceKind::EffectChoice:
        return !choice.response_index.has_value();
    case PublicChoiceKind::OptionValue:
    case PublicChoiceKind::AnnouncementNumber:
        return choice.response_index.has_value();
    }
    return false;
}

bool valid_reference_kind(const ygo::environment::PublicCardReferenceKind value) noexcept {
    return value == ygo::environment::PublicCardReferenceKind::VisibleCard ||
           value == ygo::environment::PublicCardReferenceKind::RedactedSlot;
}

bool valid_action_kind(const ygo::environment::EnvironmentActionKind value) noexcept {
    using ygo::environment::EnvironmentActionKind;
    switch (value) {
    case EnvironmentActionKind::IdleCommand:
    case EnvironmentActionKind::BattleCommand:
    case EnvironmentActionKind::Chain:
    case EnvironmentActionKind::Option:
    case EnvironmentActionKind::CardSelection:
    case EnvironmentActionKind::Announcement:
    case EnvironmentActionKind::Place:
    case EnvironmentActionKind::Position:
    case EnvironmentActionKind::YesNo:
    case EnvironmentActionKind::Pick:
    case EnvironmentActionKind::Finish:
    case EnvironmentActionKind::Cancel:
    case EnvironmentActionKind::AssignAmount:
        return true;
    case EnvironmentActionKind::Unsupported:
        return false;
    }
    return false;
}

bool valid_optional_player(const std::optional<std::uint8_t>& value) noexcept {
    return !value.has_value() || *value <= 1;
}

bool valid_continuation(const std::string_view value) noexcept {
    return value.empty() || value == "pick" || value == "amount" || value == "finish" ||
           value == "cancel" || value == "bypass";
}

const std::string& locator_at(const std::vector<LogicalPublicLocator>& table,
                              const std::uint32_t ordinal) {
    if (ordinal >= table.size()) {
        fail_logical();
    }
    return table[ordinal].value;
}

void validate_properties(const std::optional<ygo::observation::CardProperties>& value) {
    if (!value.has_value()) {
        return;
    }
    std::uint8_t previous_marker = 0;
    bool have_marker = false;
    for (const auto marker : value->link_markers) {
        const auto code = link_marker_code(marker);
        if (have_marker && code < previous_marker) {
            fail_logical();
        }
        previous_marker = code;
        have_marker = true;
    }
    std::pair<std::uint32_t, std::uint32_t> previous_counter{};
    bool have_counter = false;
    for (const auto& counter : value->counters) {
        const auto current = std::make_pair(counter.type, counter.count);
        if (have_counter && current < previous_counter) {
            fail_logical();
        }
        previous_counter = current;
        have_counter = true;
    }
}

void validate_logical_card(const LogicalEntity& entity,
                           const std::vector<LogicalPublicLocator>& table) {
    const auto& card = entity.card;
    if (!valid_locator(card.locator.value) ||
        entity.public_locator_ordinal >= table.size() ||
        table[entity.public_locator_ordinal].value != card.locator.value ||
        !valid_optional_player(card.owner) || !valid_optional_player(card.controller)) {
        fail_logical();
    }
    (void)semantic_zone_code(card.zone);
    (void)position_code(card.position);
    if (card.face_up && card.face_down) {
        fail_logical();
    }
    if (!card.identity_known &&
        (card.passcode.has_value() || card.printed.has_value() || card.current.has_value())) {
        fail_logical();
    }
    validate_properties(card.printed);
    validate_properties(card.current);
}

void validate_current_reference(const LogicalCurrentReference& reference,
                                const std::vector<LogicalPublicLocator>& table,
                                const std::vector<LogicalEntity>& entities) {
    if (!valid_locator(reference.locator.value) ||
        reference.locator.public_locator_ordinal >= table.size() ||
        table[reference.locator.public_locator_ordinal].value != reference.locator.value) {
        fail_logical();
    }
    if (reference.current_entity_ordinal.has_value()) {
        const auto ordinal = *reference.current_entity_ordinal;
        if (ordinal >= entities.size() || entities[ordinal].card.locator.value != reference.locator.value) {
            fail_logical();
        }
    }
}

void validate_historical_reference(const LogicalHistoricalReference& reference,
                                  const std::vector<LogicalPublicLocator>& table) {
    if (!valid_locator(reference.locator.value) ||
        reference.locator.public_locator_ordinal >= table.size() ||
        table[reference.locator.public_locator_ordinal].value != reference.locator.value) {
        fail_logical();
    }
}

void validate_logical_candidate_reference(
    const std::optional<LogicalPublicCardReference>& reference,
    const std::vector<LogicalPublicLocator>& table,
    const std::vector<LogicalEntity>& entities) {
    if (!reference.has_value()) {
        return;
    }
    if (!valid_reference_kind(reference->kind)) {
        fail_logical();
    }
    validate_current_reference(reference->reference, table, entities);
}

bool relationship_less(const LogicalRelationship& left, const LogicalRelationship& right) {
    return std::tie(left.kind, left.source.locator.value, left.target.locator.value) <
           std::tie(right.kind, right.source.locator.value, right.target.locator.value);
}

bool zone_less(const ygo::observation::ObservedZone& left,
               const ygo::observation::ObservedZone& right) {
    return std::tie(left.player, left.kind, left.total_count, left.public_identity_count,
                    left.hidden_count, left.player_observable_order) <
           std::tie(right.player, right.kind, right.total_count, right.public_identity_count,
                    right.hidden_count, right.player_observable_order);
}

void validate_logical(const LogicalModelInputV1& logical) {
    if (logical.schema_id != kLogicalModelInputSchemaId ||
        !valid_digest(logical.public_observation_digest) || logical.perspective_player > 1 ||
        logical.candidate_features.empty() ||
        !fits_u32_count(logical.referenced_public_entities.size()) ||
        !fits_u32_count(logical.public_locator_table.size()) ||
        !fits_u32_count(logical.candidate_features.size()) ||
        logical.candidate_features.size() != logical.candidate_routing.size()) {
        fail_logical();
    }
    if (logical.public_candidate_domain_digest.has_value() &&
        !valid_digest(*logical.public_candidate_domain_digest)) {
        fail_logical();
    }
    if (logical.public_observation_context_player.has_value() &&
        *logical.public_observation_context_player > 1) {
        fail_logical();
    }
    if (logical.public_observation_context_kind.has_value() &&
        (!valid_utf8(*logical.public_observation_context_kind) ||
         !request_kind_code(*logical.public_observation_context_kind).has_value())) {
        fail_logical();
    }

    for (std::size_t index = 0; index < logical.public_locator_table.size(); ++index) {
        const auto& locator = logical.public_locator_table[index];
        if (!valid_locator(locator.value) || locator.public_locator_ordinal != index ||
            (index != 0 && !byte_less(logical.public_locator_table[index - 1].value,
                                      locator.value))) {
            fail_logical();
        }
    }
    for (const auto& reference : logical.referenced_public_entities) {
        if (!valid_locator(reference.value) ||
            reference.public_locator_ordinal >= logical.public_locator_table.size() ||
            locator_at(logical.public_locator_table, reference.public_locator_ordinal) !=
                reference.value) {
            fail_logical();
        }
    }

    const auto& state = logical.public_safe_state;
    if (!valid_optional_player(state.globals.player_to_act) ||
        !valid_optional_player(state.globals.turn_player) ||
        !fits_u32_count(state.globals.life_points.size()) ||
        !fits_u32_count(state.zones.size()) || !fits_u32_count(state.entities.size()) ||
        !fits_u32_count(state.relationships.size()) ||
        !fits_u32_count(state.chain.links.size()) ||
        !fits_u32_count(state.visible_events.size())) {
        fail_logical();
    }
    for (std::size_t index = 0; index < state.zones.size(); ++index) {
        const auto& zone = state.zones[index];
        if (zone.player > 1 || (index != 0 && zone_less(zone, state.zones[index - 1]))) {
            fail_logical();
        }
        (void)semantic_zone_code(zone.kind);
    }
    for (std::size_t index = 0; index < state.entities.size(); ++index) {
        const auto& entity = state.entities[index];
        validate_logical_card(entity, logical.public_locator_table);
        if (entity.current_entity_ordinal != index ||
            (index != 0 && entity.card.locator.value <= state.entities[index - 1].card.locator.value)) {
            fail_logical();
        }
    }
    for (std::size_t index = 0; index < state.relationships.size(); ++index) {
        const auto& relationship = state.relationships[index];
        (void)relationship_kind_code(relationship.kind);
        validate_current_reference(relationship.source, logical.public_locator_table,
                                   state.entities);
        validate_current_reference(relationship.target, logical.public_locator_table,
                                   state.entities);
        if (index != 0 && relationship_less(relationship, state.relationships[index - 1])) {
            fail_logical();
        }
    }
    for (const auto& link : state.chain.links) {
        if (!valid_optional_player(link.activating_player) ||
            !fits_u32_count(link.targets.size())) {
            fail_logical();
        }
        if (link.source.has_value()) {
            validate_current_reference(*link.source, logical.public_locator_table,
                                       state.entities);
        }
        if (link.activation_zone.has_value()) {
            (void)semantic_zone_code(*link.activation_zone);
        }
        for (std::size_t index = 0; index < link.targets.size(); ++index) {
            validate_current_reference(link.targets[index], logical.public_locator_table,
                                       state.entities);
            if (index != 0 && link.targets[index].locator.value <
                                  link.targets[index - 1].locator.value) {
                fail_logical();
            }
        }
    }
    for (std::size_t index = 0; index < state.visible_events.size(); ++index) {
        const auto& event = state.visible_events[index];
        (void)visible_event_kind_code(event.kind);
        if (!valid_optional_player(event.player)) {
            fail_logical();
        }
        if (event.entity.has_value()) {
            validate_historical_reference(*event.entity, logical.public_locator_table);
        }
        if (!fits_u32_count(event.targets.size())) {
            fail_logical();
        }
        if (event.from_zone.has_value()) {
            (void)semantic_zone_code(*event.from_zone);
        }
        if (event.to_zone.has_value()) {
            (void)semantic_zone_code(*event.to_zone);
        }
        for (std::size_t target = 0; target < event.targets.size(); ++target) {
            validate_historical_reference(event.targets[target], logical.public_locator_table);
            if (target != 0 && event.targets[target].locator.value <
                                   event.targets[target - 1].locator.value) {
                fail_logical();
            }
        }
        if (index != 0 && event.event_index <= state.visible_events[index - 1].event_index) {
            fail_logical();
        }
    }
    if (state.match_context.perspective_player != logical.perspective_player ||
        (!state.match_context.own_deck.known &&
         (!state.match_context.own_deck.main_deck.empty() ||
          !state.match_context.own_deck.extra_deck.empty())) ||
        (!state.match_context.opponent_deck.known &&
         (!state.match_context.opponent_deck.main_deck.empty() ||
          !state.match_context.opponent_deck.extra_deck.empty()))) {
        fail_logical();
    }
    const auto validate_sorted_codes = [](const std::vector<std::uint32_t>& codes) {
        return std::is_sorted(codes.begin(), codes.end());
    };
    if (!validate_sorted_codes(state.match_context.own_deck.main_deck) ||
        !validate_sorted_codes(state.match_context.own_deck.extra_deck) ||
        !validate_sorted_codes(state.match_context.opponent_deck.main_deck) ||
        !validate_sorted_codes(state.match_context.opponent_deck.extra_deck)) {
        fail_logical();
    }
    if (!fits_u32_count(state.match_context.own_deck.main_deck.size()) ||
        !fits_u32_count(state.match_context.own_deck.extra_deck.size()) ||
        !fits_u32_count(state.match_context.opponent_deck.main_deck.size()) ||
        !fits_u32_count(state.match_context.opponent_deck.extra_deck.size())) {
        fail_logical();
    }
    for (const auto& entity : state.entities) {
        if (entity.card.printed.has_value() &&
                (!fits_u32_count(entity.card.printed->link_markers.size()) ||
                 !fits_u32_count(entity.card.printed->counters.size())) ||
            entity.card.current.has_value() &&
                (!fits_u32_count(entity.card.current->link_markers.size()) ||
                 !fits_u32_count(entity.card.current->counters.size()))) {
            fail_logical();
        }
    }

    for (std::size_t index = 0; index < logical.referenced_public_entities.size(); ++index) {
        if (index != 0 && !byte_less(logical.referenced_public_entities[index - 1].value,
                                     logical.referenced_public_entities[index].value)) {
            fail_logical();
        }
    }

    std::vector<std::string> keys;
    keys.reserve(logical.candidate_routing.size());
    for (std::size_t index = 0; index < logical.candidate_features.size(); ++index) {
        const auto& candidate = logical.candidate_features[index];
        const auto& key_value = logical.candidate_routing[index].public_action_key;
        if (!ygo::environment::is_public_action_key(key_value) ||
            std::find(keys.begin(), keys.end(), key_value) != keys.end() ||
            !valid_action_kind(candidate.action_kind) ||
            (candidate.choice.has_value() && !valid_choice(*candidate.choice)) ||
            !valid_continuation(candidate.continuation_operation)) {
            fail_logical();
        }
        validate_logical_candidate_reference(candidate.source_reference,
                                             logical.public_locator_table, state.entities);
        validate_logical_candidate_reference(candidate.target_reference,
                                             logical.public_locator_table, state.entities);
        ygo::environment::PublicActionKeyInput key;
        key.action_kind = std::string(action_kind_token(candidate.action_kind));
        key.choice = candidate.choice;
        if (candidate.source_reference.has_value()) {
            key.source_reference = ygo::environment::PublicCardReference{
                candidate.source_reference->kind,
                candidate.source_reference->reference.locator.value};
        }
        if (candidate.target_reference.has_value()) {
            key.target_reference = ygo::environment::PublicCardReference{
                candidate.target_reference->kind,
                candidate.target_reference->reference.locator.value};
        }
        key.phase = candidate.phase;
        key.position = candidate.position;
        key.source_index = candidate.source_index;
        key.amount = candidate.amount;
        key.continuation_operation = candidate.continuation_operation;
        try {
            if (ygo::environment::public_action_key(key) != key_value) {
                fail_logical();
            }
        } catch (const EncodingFailure&) {
            throw;
        } catch (...) {
            fail_logical();
        }
        keys.push_back(key_value);
    }
    if (logical.public_observation_context_kind.has_value()) {
        try {
            if (!logical.public_candidate_domain_digest.has_value() ||
                *logical.public_candidate_domain_digest !=
                    ygo::environment::public_candidate_domain_digest(
                        *logical.public_observation_context_kind, keys)) {
                fail_logical();
            }
        } catch (const EncodingFailure&) {
            throw;
        } catch (...) {
            fail_logical();
        }
    } else if (logical.public_candidate_domain_digest.has_value()) {
        fail_logical();
    }
}

void write_properties(Writer& writer,
                      const std::optional<ygo::observation::CardProperties>& value) {
    writer.boolean(value.has_value());
    if (!value.has_value()) {
        return;
    }
    const auto& properties = *value;
    optional_u32(writer, properties.type);
    optional_u32(writer, properties.attribute);
    optional_u64(writer, properties.race);
    optional_i32(writer, properties.attack);
    optional_i32(writer, properties.defense);
    optional_i32(writer, properties.base_attack);
    optional_i32(writer, properties.base_defense);
    optional_u32(writer, properties.level);
    optional_u32(writer, properties.rank);
    optional_u32(writer, properties.link_rating);
    writer.u32be(static_cast<std::uint32_t>(properties.link_markers.size()));
    for (const auto marker : properties.link_markers) {
        writer.u8(link_marker_code(marker));
    }
    optional_u32(writer, properties.left_scale);
    optional_u32(writer, properties.right_scale);
    optional_u32(writer, properties.status_flags);
    writer.u32be(static_cast<std::uint32_t>(properties.counters.size()));
    for (const auto& counter : properties.counters) {
        writer.u32be(counter.type);
        writer.u32be(counter.count);
    }
}

void write_logical_globals(Writer& writer,
                           const ygo::observation::ObservedPlayerGlobals& globals) {
    writer.u64be(globals.duel_flags);
    writer.u32be(static_cast<std::uint32_t>(globals.life_points.size()));
    for (const auto value : globals.life_points) writer.u32be(value);
    optional_u8(writer, globals.player_to_act);
    optional_u8(writer, globals.turn_player);
    optional_u32(writer, globals.turn_count);
    optional_u32(writer, globals.phase);
    writer.u32be(globals.chain_length);
    optional_u8(writer, globals.winner);
    optional_u8(writer, globals.win_reason);
    writer.boolean(globals.terminal);
}

void write_logical_safe_state(Writer& writer, const LogicalPublicState& state) {
    writer.string(ygo::environment::kPublicSafeStateSchemaId);
    writer.string(ygo::environment::kPublicSafeStateSchemaId);
    write_logical_globals(writer, state.globals);

    auto zones = state.zones;
    std::sort(zones.begin(), zones.end(), zone_less);
    writer.u32be(static_cast<std::uint32_t>(zones.size()));
    for (const auto& zone : zones) {
        writer.u8(zone.player);
        writer.u8(semantic_zone_code(zone.kind));
        writer.u32be(zone.total_count);
        writer.u32be(zone.public_identity_count);
        writer.u32be(zone.hidden_count);
        writer.boolean(zone.player_observable_order);
    }

    auto entities = state.entities;
    std::sort(entities.begin(), entities.end(), [](const LogicalEntity& left,
                                                   const LogicalEntity& right) {
        return left.card.locator.value < right.card.locator.value;
    });
    writer.u32be(static_cast<std::uint32_t>(entities.size()));
    for (const auto& entity : entities) {
        const auto& card = entity.card;
        writer.string(card.locator.value);
        writer.boolean(card.identity_known);
        optional_u32(writer, card.passcode);
        optional_u8(writer, card.owner);
        optional_u8(writer, card.controller);
        writer.u8(semantic_zone_code(card.zone));
        optional_u32(writer, card.sequence);
        optional_u32(writer, card.overlay_sequence);
        writer.u8(position_code(card.position));
        writer.boolean(card.face_up);
        writer.boolean(card.face_down);
        write_properties(writer, card.printed);
        write_properties(writer, card.current);
    }

    auto relationships = state.relationships;
    std::sort(relationships.begin(), relationships.end(), relationship_less);
    writer.u32be(static_cast<std::uint32_t>(relationships.size()));
    for (const auto& relationship : relationships) {
        writer.u8(relationship_kind_code(relationship.kind));
        writer.string(relationship.source.locator.value);
        writer.string(relationship.target.locator.value);
    }

    writer.u32be(state.chain.length);
    writer.u32be(static_cast<std::uint32_t>(state.chain.links.size()));
    for (const auto& link : state.chain.links) {
        writer.u32be(link.index);
        optional_u8(writer, link.activating_player);
        writer.boolean(link.source.has_value());
        if (link.source.has_value()) writer.string(link.source->locator.value);
        writer.boolean(link.activation_zone.has_value());
        if (link.activation_zone.has_value()) writer.u8(semantic_zone_code(*link.activation_zone));
        optional_u64(writer, link.effect_description);
        auto targets = link.targets;
        std::sort(targets.begin(), targets.end(), [](const LogicalCurrentReference& left,
                                                    const LogicalCurrentReference& right) {
            return left.locator.value < right.locator.value;
        });
        writer.u32be(static_cast<std::uint32_t>(targets.size()));
        for (const auto& target : targets) writer.string(target.locator.value);
    }

    auto events = state.visible_events;
    std::sort(events.begin(), events.end(), [](const LogicalVisibleEvent& left,
                                               const LogicalVisibleEvent& right) {
        return left.event_index < right.event_index;
    });
    writer.u32be(static_cast<std::uint32_t>(events.size()));
    for (const auto& event : events) {
        writer.u64be(event.event_index);
        writer.u8(visible_event_kind_code(event.kind));
        optional_u8(writer, event.player);
        writer.boolean(event.entity.has_value());
        if (event.entity.has_value()) writer.string(event.entity->locator.value);
        optional_u32(writer, event.public_passcode);
        writer.boolean(event.from_zone.has_value());
        if (event.from_zone.has_value()) writer.u8(semantic_zone_code(*event.from_zone));
        writer.boolean(event.to_zone.has_value());
        if (event.to_zone.has_value()) writer.u8(semantic_zone_code(*event.to_zone));
        optional_u32(writer, event.count);
        optional_i32(writer, event.amount);
        optional_u32(writer, event.counter_type);
        optional_u32(writer, event.phase);
        optional_u8(writer, event.winner);
        optional_u8(writer, event.win_reason);
        optional_u64(writer, event.effect_description);
        auto targets = event.targets;
        std::sort(targets.begin(), targets.end(), [](const LogicalHistoricalReference& left,
                                                    const LogicalHistoricalReference& right) {
            return left.locator.value < right.locator.value;
        });
        writer.u32be(static_cast<std::uint32_t>(targets.size()));
        for (const auto& target : targets) writer.string(target.locator.value);
    }

    const auto& context = state.match_context;
    writer.u8(context.perspective_player);
    writer.u64be(context.duel_flags);
    writer.boolean(context.knowledge.own_decklist_known);
    writer.boolean(context.knowledge.opponent_decklist_known);
    const auto write_deck = [&writer](const ygo::observation::StaticDeckContext& deck) {
        writer.boolean(deck.known);
        auto main_deck = deck.main_deck;
        auto extra_deck = deck.extra_deck;
        std::sort(main_deck.begin(), main_deck.end());
        std::sort(extra_deck.begin(), extra_deck.end());
        writer.u32be(static_cast<std::uint32_t>(main_deck.size()));
        for (const auto code : main_deck) writer.u32be(code);
        writer.u32be(static_cast<std::uint32_t>(extra_deck.size()));
        for (const auto code : extra_deck) writer.u32be(code);
    };
    write_deck(context.own_deck);
    write_deck(context.opponent_deck);
}

void write_logical_candidate_exact(Writer& writer, const LogicalCandidate& candidate,
                                   const std::string& public_action_key) {
    writer.string(action_kind_token(candidate.action_kind));
    writer.string(public_action_key);
    writer.boolean(candidate.choice.has_value());
    if (candidate.choice.has_value()) {
        writer.u8(public_choice_code(candidate.choice->kind));
        writer.u64be(candidate.choice->value);
        optional_u32(writer, candidate.choice->response_index);
    }
    const auto write_reference = [&writer](
                                     const std::optional<LogicalPublicCardReference>& reference) {
        writer.boolean(reference.has_value());
        if (reference.has_value()) {
            writer.u8(public_reference_code(reference->kind));
            writer.string(reference->reference.locator.value);
        }
    };
    write_reference(candidate.source_reference);
    write_reference(candidate.target_reference);
    optional_u32(writer, candidate.phase);
    optional_u8(writer, candidate.position);
    optional_u32(writer, candidate.source_index);
    optional_i32(writer, candidate.amount);
    writer.string(candidate.continuation_operation);
    writer.boolean(candidate.submits_engine_response);
}

std::vector<std::uint8_t> canonical_logical_bytes_unchecked(
    const LogicalModelInputV1& logical) {
    Writer writer;
    writer.string(kLogicalModelInputSchemaId);
    writer.string(kLogicalModelInputSchemaId);
    writer.string(logical.public_observation_digest);
    writer.u8(logical.perspective_player);
    writer.u64be(logical.decision_index);
    optional_string(writer, logical.public_observation_context_kind);
    optional_u8(writer, logical.public_observation_context_player);
    writer.u32be(static_cast<std::uint32_t>(logical.referenced_public_entities.size()));
    for (const auto& reference : logical.referenced_public_entities) writer.string(reference.value);
    writer.u32be(static_cast<std::uint32_t>(logical.public_locator_table.size()));
    for (const auto& locator : logical.public_locator_table) writer.string(locator.value);

    Writer safe_writer;
    write_logical_safe_state(safe_writer, logical.public_safe_state);
    const auto safe_state = std::move(safe_writer).take();
    writer.bytes(safe_state);

    writer.u32be(static_cast<std::uint32_t>(logical.candidate_features.size()));
    for (std::size_t index = 0; index < logical.candidate_features.size(); ++index) {
        write_logical_candidate_exact(writer, logical.candidate_features[index],
                                      logical.candidate_routing[index].public_action_key);
    }
    optional_string(writer, logical.public_candidate_domain_digest);
    return std::move(writer).take();
}

std::optional<EncodedCardProperties> encode_properties(
    const std::optional<ygo::observation::CardProperties>& value) {
    if (!value.has_value()) return std::nullopt;
    EncodedCardProperties result;
    result.type = value->type;
    result.attribute = value->attribute;
    result.race = value->race;
    result.attack = value->attack;
    result.defense = value->defense;
    result.base_attack = value->base_attack;
    result.base_defense = value->base_defense;
    result.level = value->level;
    result.rank = value->rank;
    result.link_rating = value->link_rating;
    for (const auto marker : value->link_markers) result.link_marker_codes.push_back(link_marker_code(marker));
    result.left_scale = value->left_scale;
    result.right_scale = value->right_scale;
    result.status_flags = value->status_flags;
    for (const auto& counter : value->counters) {
        result.counters.push_back({counter.type, counter.count});
    }
    return std::optional<EncodedCardProperties>(std::move(result));
}

std::uint32_t vocabulary_id(const CardVocabularyV1& vocabulary,
                            const std::uint32_t passcode) {
    const auto id = vocabulary.id_for_public_passcode(passcode);
    if (!id.has_value() || *id < 2) fail_unknown_passcode();
    return *id;
}

EncodedCurrentReference encode_current_reference(const LogicalCurrentReference& reference) {
    return {reference.locator.public_locator_ordinal, reference.current_entity_ordinal};
}

EncodedCardReference encode_card_reference(const LogicalPublicCardReference& reference) {
    return {public_reference_code(reference.kind), encode_current_reference(reference.reference)};
}

EncodedModelInputV1 encode_unchecked(const LogicalModelInputV1& logical,
                                     const CardVocabularyV1& vocabulary) {
    EncodedModelInputV1 output;
    output.schema_id = std::string(kEncodedModelInputSchemaId);
    output.card_vocabulary_identity = vocabulary.identity();
    output.public_observation_digest = logical.public_observation_digest;
    output.perspective_player = logical.perspective_player;
    output.decision_index = logical.decision_index;
    for (const auto& locator : logical.public_locator_table) output.public_locator_table.push_back(locator.value);
    if (logical.public_observation_context_kind.has_value()) {
        output.public_observation_context_kind_code =
            request_kind_code(*logical.public_observation_context_kind);
    }
    output.public_observation_context_player = logical.public_observation_context_player;
    for (const auto& reference : logical.referenced_public_entities) {
        output.observation_context_reference_ordinals.push_back(reference.public_locator_ordinal);
    }

    output.globals.duel_flags = logical.public_safe_state.globals.duel_flags;
    output.globals.life_points = logical.public_safe_state.globals.life_points;
    output.globals.player_to_act = logical.public_safe_state.globals.player_to_act;
    output.globals.turn_player = logical.public_safe_state.globals.turn_player;
    output.globals.turn_count = logical.public_safe_state.globals.turn_count;
    output.globals.phase = logical.public_safe_state.globals.phase;
    output.globals.chain_length = logical.public_safe_state.globals.chain_length;
    output.globals.winner = logical.public_safe_state.globals.winner;
    output.globals.win_reason = logical.public_safe_state.globals.win_reason;
    output.globals.terminal = logical.public_safe_state.globals.terminal;

    for (const auto& source : logical.public_safe_state.zones) {
        output.zones.push_back({source.player, semantic_zone_code(source.kind), source.total_count,
                                source.public_identity_count, source.hidden_count,
                                source.player_observable_order});
    }
    for (const auto& source : logical.public_safe_state.entities) {
        EncodedEntity entity;
        entity.public_locator_ordinal = source.public_locator_ordinal;
        entity.identity_known = source.card.identity_known;
        if (source.card.identity_known) {
            if (!source.card.passcode.has_value()) {
                fail_logical();
            }
            entity.card_vocabulary_id = vocabulary_id(vocabulary, *source.card.passcode);
        } else {
            entity.card_vocabulary_id = vocabulary.unknown_or_redacted_id();
        }
        entity.owner = source.card.owner;
        entity.controller = source.card.controller;
        entity.zone_code = semantic_zone_code(source.card.zone);
        entity.sequence = source.card.sequence;
        entity.overlay_sequence = source.card.overlay_sequence;
        entity.position_code = position_code(source.card.position);
        entity.face_up = source.card.face_up;
        entity.face_down = source.card.face_down;
        if (source.card.identity_known) {
            entity.printed = encode_properties(source.card.printed);
            entity.current = encode_properties(source.card.current);
        }
        output.entities.push_back(std::move(entity));
    }
    for (const auto& source : logical.public_safe_state.relationships) {
        output.relationships.push_back({relationship_kind_code(source.kind),
                                        encode_current_reference(source.source),
                                        encode_current_reference(source.target)});
    }
    output.chain.length = logical.public_safe_state.chain.length;
    for (const auto& source : logical.public_safe_state.chain.links) {
        EncodedChainLink link;
        link.index = source.index;
        link.activating_player = source.activating_player;
        if (source.source.has_value()) link.source = encode_current_reference(*source.source);
        if (source.activation_zone.has_value()) link.activation_zone_code = semantic_zone_code(*source.activation_zone);
        link.effect_description = source.effect_description;
        for (const auto& target : source.targets) link.targets.push_back(encode_current_reference(target));
        output.chain.links.push_back(std::move(link));
    }
    for (const auto& source : logical.public_safe_state.visible_events) {
        EncodedVisibleEvent event;
        event.event_index = source.event_index;
        event.kind_code = visible_event_kind_code(source.kind);
        event.player = source.player;
        if (source.entity.has_value()) event.public_locator_ordinal = source.entity->locator.public_locator_ordinal;
        if (source.public_passcode.has_value()) {
            event.public_card_vocabulary_id = vocabulary_id(vocabulary, *source.public_passcode);
        }
        if (source.from_zone.has_value()) event.from_zone_code = semantic_zone_code(*source.from_zone);
        if (source.to_zone.has_value()) event.to_zone_code = semantic_zone_code(*source.to_zone);
        event.count = source.count;
        event.amount = source.amount;
        event.counter_type = source.counter_type;
        event.phase = source.phase;
        event.winner = source.winner;
        event.win_reason = source.win_reason;
        event.effect_description = source.effect_description;
        for (const auto& target : source.targets) event.target_public_locator_ordinals.push_back(target.locator.public_locator_ordinal);
        output.visible_events.push_back(std::move(event));
    }

    output.match_context.perspective_player = logical.public_safe_state.match_context.perspective_player;
    output.match_context.duel_flags = logical.public_safe_state.match_context.duel_flags;
    output.match_context.own_decklist_known = logical.public_safe_state.match_context.knowledge.own_decklist_known;
    output.match_context.opponent_decklist_known = logical.public_safe_state.match_context.knowledge.opponent_decklist_known;
    const auto encode_deck = [&vocabulary](const ygo::observation::StaticDeckContext& source) {
        EncodedDeck result;
        result.known = source.known;
        for (const auto passcode : source.main_deck) result.main_deck.push_back(vocabulary_id(vocabulary, passcode));
        for (const auto passcode : source.extra_deck) result.extra_deck.push_back(vocabulary_id(vocabulary, passcode));
        return result;
    };
    output.match_context.own_deck = encode_deck(logical.public_safe_state.match_context.own_deck);
    output.match_context.opponent_deck = encode_deck(logical.public_safe_state.match_context.opponent_deck);
    output.public_candidate_domain_digest = logical.public_candidate_domain_digest;

    for (const auto& source : logical.candidate_features) {
        EncodedCandidate candidate;
        candidate.action_kind_code = action_kind_code(source.action_kind);
        if (source.choice.has_value()) {
            candidate.choice = EncodedChoice{public_choice_code(source.choice->kind),
                                             source.choice->value, source.choice->response_index};
        }
        if (source.source_reference.has_value()) candidate.source_reference = encode_card_reference(*source.source_reference);
        if (source.target_reference.has_value()) candidate.target_reference = encode_card_reference(*source.target_reference);
        candidate.phase = source.phase;
        candidate.position = source.position;
        candidate.source_index = source.source_index;
        candidate.amount = source.amount;
        candidate.continuation_operation_code = continuation_code(source.continuation_operation);
        candidate.submits_engine_response = source.submits_engine_response;
        output.candidate_features.push_back(std::move(candidate));
    }
    for (const auto& routing : logical.candidate_routing) output.routing_keys.push_back(routing.public_action_key);
    return output;
}

void validate_encoded_properties(const std::optional<EncodedCardProperties>& value) {
    if (!value.has_value()) return;
    if (!fits_u32_count(value->link_marker_codes.size()) ||
        !fits_u32_count(value->counters.size())) {
        fail_encoded();
    }
    std::uint8_t previous_marker = 0;
    bool have_marker = false;
    for (const auto marker : value->link_marker_codes) {
        if (marker > 7 || (have_marker && marker < previous_marker)) fail_encoded();
        previous_marker = marker;
        have_marker = true;
    }
    std::pair<std::uint32_t, std::uint32_t> previous_counter{};
    bool have_counter = false;
    for (const auto& counter : value->counters) {
        const auto current = std::make_pair(counter.type, counter.count);
        if (have_counter && current < previous_counter) fail_encoded();
        previous_counter = current;
        have_counter = true;
    }
}

void validate_encoded_reference(const EncodedCurrentReference& reference,
                                const std::vector<std::string>& table,
                                const std::vector<EncodedEntity>& entities) {
    if (reference.public_locator_ordinal >= table.size()) fail_encoded();
    if (reference.current_entity_ordinal.has_value() &&
        (*reference.current_entity_ordinal >= entities.size() ||
         entities[*reference.current_entity_ordinal].public_locator_ordinal !=
             reference.public_locator_ordinal)) {
        fail_encoded();
    }
}

void validate_encoded_candidate_routing(
    const EncodedCandidate& candidate,
    const std::vector<std::string>& locator_table,
    const std::string& routing_key) {
    ygo::environment::PublicActionKeyInput key;
    key.action_kind = std::string(action_kind_token_from_code(candidate.action_kind_code));
    if (candidate.choice.has_value()) {
        key.choice = ygo::environment::PublicChoice{
            public_choice_kind_from_code(candidate.choice->kind_code),
            candidate.choice->value,
            candidate.choice->response_index};
    }
    const auto make_public_reference = [&locator_table](
                                           const EncodedCardReference& reference) {
        return ygo::environment::PublicCardReference{
            public_reference_kind_from_code(reference.kind_code),
            locator_table[reference.reference.public_locator_ordinal]};
    };
    if (candidate.source_reference.has_value()) {
        key.source_reference = make_public_reference(*candidate.source_reference);
    }
    if (candidate.target_reference.has_value()) {
        key.target_reference = make_public_reference(*candidate.target_reference);
    }
    key.phase = candidate.phase;
    key.position = candidate.position;
    key.source_index = candidate.source_index;
    key.amount = candidate.amount;
    key.continuation_operation = std::string(
        continuation_token(candidate.continuation_operation_code));
    try {
        if (ygo::environment::public_action_key(key) != routing_key) {
            fail_encoded();
        }
    } catch (const EncodingFailure&) {
        throw;
    } catch (...) {
        fail_encoded();
    }
}

void validate_encoded(const EncodedModelInputV1& encoded) {
    if (encoded.schema_id != kEncodedModelInputSchemaId ||
        !valid_digest(encoded.public_observation_digest) || encoded.perspective_player > 1 ||
        encoded.candidate_features.empty() ||
        !fits_u32_count(encoded.public_locator_table.size()) ||
        !fits_u32_count(encoded.observation_context_reference_ordinals.size()) ||
        !fits_u32_count(encoded.globals.life_points.size()) ||
        !fits_u32_count(encoded.zones.size()) || !fits_u32_count(encoded.entities.size()) ||
        !fits_u32_count(encoded.relationships.size()) ||
        !fits_u32_count(encoded.chain.links.size()) ||
        !fits_u32_count(encoded.visible_events.size()) ||
        !fits_u32_count(encoded.candidate_features.size()) ||
        !fits_u32_count(encoded.routing_keys.size()) ||
        encoded.candidate_features.size() != encoded.routing_keys.size() ||
        encoded.card_vocabulary_identity.rfind(kCardVocabularyIdentityPrefix, 0) != 0 ||
        encoded.card_vocabulary_identity.size() != kCardVocabularyIdentityPrefix.size() + 64) {
        fail_encoded();
    }
    const auto vocabulary_digest = encoded.card_vocabulary_identity.substr(kCardVocabularyIdentityPrefix.size());
    if (!valid_digest(vocabulary_digest)) fail_encoded();
    for (std::size_t index = 0; index < encoded.public_locator_table.size(); ++index) {
        if (!valid_locator(encoded.public_locator_table[index]) ||
            (index != 0 && !byte_less(encoded.public_locator_table[index - 1],
                                      encoded.public_locator_table[index]))) {
            fail_encoded();
        }
    }
    if (encoded.public_observation_context_kind_code.has_value() &&
        (*encoded.public_observation_context_kind_code == 0 ||
         *encoded.public_observation_context_kind_code > 14)) {
        fail_encoded();
    }
    if (encoded.public_observation_context_player.has_value() &&
        *encoded.public_observation_context_player > 1) {
        fail_encoded();
    }
    for (const auto ordinal : encoded.observation_context_reference_ordinals) {
        if (ordinal >= encoded.public_locator_table.size()) fail_encoded();
    }
    if (encoded.match_context.perspective_player != encoded.perspective_player ||
        (!encoded.match_context.own_deck.known &&
         (!encoded.match_context.own_deck.main_deck.empty() ||
          !encoded.match_context.own_deck.extra_deck.empty())) ||
        (!encoded.match_context.opponent_deck.known &&
         (!encoded.match_context.opponent_deck.main_deck.empty() ||
          !encoded.match_context.opponent_deck.extra_deck.empty()))) {
        fail_encoded();
    }
    for (const auto& zone : encoded.zones) {
        if (zone.player > 1 || zone.kind_code > 10) fail_encoded();
    }
    for (std::size_t index = 0; index < encoded.entities.size(); ++index) {
        const auto& entity = encoded.entities[index];
        if (entity.public_locator_ordinal >= encoded.public_locator_table.size() ||
            (index != 0 &&
             !(encoded.public_locator_table[encoded.entities[index - 1].public_locator_ordinal] <
               encoded.public_locator_table[entity.public_locator_ordinal])) ||
            (entity.identity_known && entity.card_vocabulary_id < 2) ||
            (!entity.identity_known && entity.card_vocabulary_id != kCardVocabularyUnknownOrRedactedId) ||
            entity.zone_code > 10 ||
            (entity.position_code != 0 && entity.position_code != 1 &&
             entity.position_code != 2 && entity.position_code != 4 &&
             entity.position_code != 8) ||
            !valid_optional_player(entity.owner) || !valid_optional_player(entity.controller) ||
            entity.face_up && entity.face_down) {
            fail_encoded();
        }
        if (!entity.identity_known && (entity.printed.has_value() || entity.current.has_value())) {
            fail_encoded();
        }
        validate_encoded_properties(entity.printed);
        validate_encoded_properties(entity.current);
    }
    for (const auto& relationship : encoded.relationships) {
        if (relationship.kind_code > 2) fail_encoded();
        validate_encoded_reference(relationship.source, encoded.public_locator_table, encoded.entities);
        validate_encoded_reference(relationship.target, encoded.public_locator_table, encoded.entities);
    }
    for (const auto& link : encoded.chain.links) {
        if (!valid_optional_player(link.activating_player) ||
            (link.activation_zone_code.has_value() && *link.activation_zone_code > 10) ||
            !fits_u32_count(link.targets.size())) {
            fail_encoded();
        }
        if (link.source.has_value()) validate_encoded_reference(*link.source, encoded.public_locator_table, encoded.entities);
        for (const auto& target : link.targets) validate_encoded_reference(target, encoded.public_locator_table, encoded.entities);
    }
    for (std::size_t event_index = 0; event_index < encoded.visible_events.size(); ++event_index) {
        const auto& event = encoded.visible_events[event_index];
        if (event.kind_code > 22 || !valid_optional_player(event.player) ||
            (event.public_locator_ordinal.has_value() &&
             *event.public_locator_ordinal >= encoded.public_locator_table.size()) ||
            (event.from_zone_code.has_value() && *event.from_zone_code > 10) ||
            (event.to_zone_code.has_value() && *event.to_zone_code > 10) ||
            (event.public_card_vocabulary_id.has_value() &&
             *event.public_card_vocabulary_id < 2) ||
            !fits_u32_count(event.target_public_locator_ordinals.size()) ||
            (event_index != 0 &&
             event.event_index <= encoded.visible_events[event_index - 1].event_index)) {
            fail_encoded();
        }
        for (const auto ordinal : event.target_public_locator_ordinals) {
            if (ordinal >= encoded.public_locator_table.size()) fail_encoded();
        }
    }
    const auto validate_deck = [](const EncodedDeck& deck) {
        if (!fits_u32_count(deck.main_deck.size()) ||
            !fits_u32_count(deck.extra_deck.size()) ||
            (!deck.known && (!deck.main_deck.empty() || !deck.extra_deck.empty())) ||
            !std::is_sorted(deck.main_deck.begin(), deck.main_deck.end()) ||
            !std::is_sorted(deck.extra_deck.begin(), deck.extra_deck.end())) {
            fail_encoded();
        }
        for (const auto id : deck.main_deck) if (id < 2) fail_encoded();
        for (const auto id : deck.extra_deck) if (id < 2) fail_encoded();
    };
    validate_deck(encoded.match_context.own_deck);
    validate_deck(encoded.match_context.opponent_deck);

    std::vector<std::string> keys;
    keys.reserve(encoded.routing_keys.size());
    for (const auto& key : encoded.routing_keys) {
        if (!ygo::environment::is_public_action_key(key) ||
            std::find(keys.begin(), keys.end(), key) != keys.end()) {
            fail_encoded();
        }
        keys.push_back(key);
    }

    for (std::size_t index = 0; index < encoded.candidate_features.size(); ++index) {
        const auto& candidate = encoded.candidate_features[index];
        if (candidate.action_kind_code == 0 || candidate.action_kind_code > 13 ||
            candidate.choice.has_value() && candidate.choice->kind_code == 0 ||
            candidate.choice.has_value() && candidate.choice->kind_code > 5 ||
            candidate.source_reference.has_value() && candidate.source_reference->kind_code > 1 ||
            candidate.target_reference.has_value() && candidate.target_reference->kind_code > 1 ||
            candidate.continuation_operation_code > 5) {
            fail_encoded();
        }
        if (candidate.choice.has_value()) {
            if ((candidate.choice->kind_code == 1 || candidate.choice->kind_code == 2) &&
                (candidate.choice->value > 1 || candidate.choice->response_index.has_value())) {
                fail_encoded();
            }
            if ((candidate.choice->kind_code == 3 && candidate.choice->response_index.has_value()) ||
                (candidate.choice->kind_code == 4 || candidate.choice->kind_code == 5) &&
                    !candidate.choice->response_index.has_value()) {
                fail_encoded();
            }
        }
        if (candidate.source_reference.has_value()) {
            validate_encoded_reference(candidate.source_reference->reference,
                                       encoded.public_locator_table, encoded.entities);
        }
        if (candidate.target_reference.has_value()) {
            validate_encoded_reference(candidate.target_reference->reference,
                                       encoded.public_locator_table, encoded.entities);
        }
        validate_encoded_candidate_routing(candidate, encoded.public_locator_table,
                                           encoded.routing_keys[index]);
    }
    if (encoded.public_observation_context_kind_code.has_value()) {
        if (!encoded.public_candidate_domain_digest.has_value() ||
            !valid_digest(*encoded.public_candidate_domain_digest) ||
            *encoded.public_candidate_domain_digest !=
                ygo::environment::public_candidate_domain_digest(
                    request_kind_token(*encoded.public_observation_context_kind_code), keys)) {
            fail_encoded();
        }
    } else if (encoded.public_candidate_domain_digest.has_value()) {
        fail_encoded();
    }
}

void write_encoded_properties(Writer& writer,
                              const std::optional<EncodedCardProperties>& value) {
    writer.boolean(value.has_value());
    if (!value.has_value()) return;
    optional_u32(writer, value->type);
    optional_u32(writer, value->attribute);
    optional_u64(writer, value->race);
    optional_i32(writer, value->attack);
    optional_i32(writer, value->defense);
    optional_i32(writer, value->base_attack);
    optional_i32(writer, value->base_defense);
    optional_u32(writer, value->level);
    optional_u32(writer, value->rank);
    optional_u32(writer, value->link_rating);
    writer.u32be(static_cast<std::uint32_t>(value->link_marker_codes.size()));
    for (const auto code : value->link_marker_codes) writer.u8(code);
    optional_u32(writer, value->left_scale);
    optional_u32(writer, value->right_scale);
    optional_u32(writer, value->status_flags);
    writer.u32be(static_cast<std::uint32_t>(value->counters.size()));
    for (const auto& counter : value->counters) {
        writer.u32be(counter.type);
        writer.u32be(counter.count);
    }
}

void write_encoded_reference_payload(Writer& writer,
                                     const EncodedCurrentReference& reference) {
    writer.u32be(reference.public_locator_ordinal);
    optional_u32(writer, reference.current_entity_ordinal);
}

void write_encoded_reference(Writer& writer, const EncodedCurrentReference& reference) {
    writer.boolean(true);
    write_encoded_reference_payload(writer, reference);
}

void write_optional_encoded_reference(Writer& writer,
                                     const std::optional<EncodedCurrentReference>& reference) {
    writer.boolean(reference.has_value());
    if (reference.has_value()) write_encoded_reference_payload(writer, *reference);
}

void write_encoded_globals(Writer& writer, const EncodedGlobals& globals) {
    writer.u64be(globals.duel_flags);
    writer.u32be(static_cast<std::uint32_t>(globals.life_points.size()));
    for (const auto value : globals.life_points) writer.u32be(value);
    optional_u8(writer, globals.player_to_act);
    optional_u8(writer, globals.turn_player);
    optional_u32(writer, globals.turn_count);
    optional_u32(writer, globals.phase);
    writer.u32be(globals.chain_length);
    optional_u8(writer, globals.winner);
    optional_u8(writer, globals.win_reason);
    writer.boolean(globals.terminal);
}

void write_encoded(Writer& writer, const EncodedModelInputV1& encoded) {
    writer.string(kEncodedModelInputSchemaId);
    writer.string(kEncodedModelInputSchemaId);
    writer.string(kLogicalModelInputSchemaId);
    writer.string(encoded.card_vocabulary_identity);
    writer.string(encoded.public_observation_digest);
    writer.u8(encoded.perspective_player);
    writer.u64be(encoded.decision_index);
    writer.u32be(static_cast<std::uint32_t>(encoded.public_locator_table.size()));
    for (const auto& locator : encoded.public_locator_table) writer.string(locator);
    optional_u16(writer, encoded.public_observation_context_kind_code);
    optional_u8(writer, encoded.public_observation_context_player);
    writer.u32be(static_cast<std::uint32_t>(encoded.observation_context_reference_ordinals.size()));
    for (const auto ordinal : encoded.observation_context_reference_ordinals) writer.u32be(ordinal);
    write_encoded_globals(writer, encoded.globals);
    writer.u32be(static_cast<std::uint32_t>(encoded.zones.size()));
    for (const auto& zone : encoded.zones) {
        writer.u8(zone.player);
        writer.u8(zone.kind_code);
        writer.u32be(zone.total_count);
        writer.u32be(zone.public_identity_count);
        writer.u32be(zone.hidden_count);
        writer.boolean(zone.player_observable_order);
    }
    writer.u32be(static_cast<std::uint32_t>(encoded.entities.size()));
    for (const auto& entity : encoded.entities) {
        writer.u32be(entity.public_locator_ordinal);
        writer.boolean(entity.identity_known);
        writer.u32be(entity.card_vocabulary_id);
        optional_u8(writer, entity.owner);
        optional_u8(writer, entity.controller);
        writer.u8(entity.zone_code);
        optional_u32(writer, entity.sequence);
        optional_u32(writer, entity.overlay_sequence);
        writer.u8(entity.position_code);
        writer.boolean(entity.face_up);
        writer.boolean(entity.face_down);
        write_encoded_properties(writer, entity.printed);
        write_encoded_properties(writer, entity.current);
    }
    writer.u32be(static_cast<std::uint32_t>(encoded.relationships.size()));
    for (const auto& relationship : encoded.relationships) {
        writer.u8(relationship.kind_code);
        write_encoded_reference(writer, relationship.source);
        write_encoded_reference(writer, relationship.target);
    }
    writer.u32be(encoded.chain.length);
    writer.u32be(static_cast<std::uint32_t>(encoded.chain.links.size()));
    for (const auto& link : encoded.chain.links) {
        writer.u32be(link.index);
        optional_u8(writer, link.activating_player);
        write_optional_encoded_reference(writer, link.source);
        optional_u8(writer, link.activation_zone_code);
        optional_u64(writer, link.effect_description);
        writer.u32be(static_cast<std::uint32_t>(link.targets.size()));
        for (const auto& target : link.targets) write_encoded_reference(writer, target);
    }
    writer.u32be(static_cast<std::uint32_t>(encoded.visible_events.size()));
    for (const auto& event : encoded.visible_events) {
        writer.u64be(event.event_index);
        writer.u8(event.kind_code);
        optional_u8(writer, event.player);
        writer.boolean(event.public_locator_ordinal.has_value());
        if (event.public_locator_ordinal.has_value()) writer.u32be(*event.public_locator_ordinal);
        optional_u32(writer, event.public_card_vocabulary_id);
        optional_u8(writer, event.from_zone_code);
        optional_u8(writer, event.to_zone_code);
        optional_u32(writer, event.count);
        optional_i32(writer, event.amount);
        optional_u32(writer, event.counter_type);
        optional_u32(writer, event.phase);
        optional_u8(writer, event.winner);
        optional_u8(writer, event.win_reason);
        optional_u64(writer, event.effect_description);
        writer.u32be(static_cast<std::uint32_t>(event.target_public_locator_ordinals.size()));
        for (const auto ordinal : event.target_public_locator_ordinals) writer.u32be(ordinal);
    }
    const auto& context = encoded.match_context;
    writer.u8(context.perspective_player);
    writer.u64be(context.duel_flags);
    writer.boolean(context.own_decklist_known);
    writer.boolean(context.opponent_decklist_known);
    const auto write_deck = [&writer](const EncodedDeck& deck) {
        writer.boolean(deck.known);
        writer.u32be(static_cast<std::uint32_t>(deck.main_deck.size()));
        for (const auto id : deck.main_deck) writer.u32be(id);
        writer.u32be(static_cast<std::uint32_t>(deck.extra_deck.size()));
        for (const auto id : deck.extra_deck) writer.u32be(id);
    };
    write_deck(context.own_deck);
    write_deck(context.opponent_deck);
    optional_string(writer, encoded.public_candidate_domain_digest);
    writer.u32be(static_cast<std::uint32_t>(encoded.candidate_features.size()));
    for (const auto& candidate : encoded.candidate_features) {
        writer.u16be(candidate.action_kind_code);
        writer.boolean(candidate.choice.has_value());
        if (candidate.choice.has_value()) {
            writer.u8(candidate.choice->kind_code);
            writer.u64be(candidate.choice->value);
            optional_u32(writer, candidate.choice->response_index);
        }
        const auto write_candidate_reference = [&writer](
                                                   const std::optional<EncodedCardReference>& reference) {
            writer.boolean(reference.has_value());
            if (reference.has_value()) {
                writer.u8(reference->kind_code);
                write_encoded_reference_payload(writer, reference->reference);
            }
        };
        write_candidate_reference(candidate.source_reference);
        write_candidate_reference(candidate.target_reference);
        optional_u32(writer, candidate.phase);
        optional_u8(writer, candidate.position);
        optional_u32(writer, candidate.source_index);
        optional_i32(writer, candidate.amount);
        writer.u8(candidate.continuation_operation_code);
        writer.boolean(candidate.submits_engine_response);
    }
    writer.u32be(static_cast<std::uint32_t>(encoded.routing_keys.size()));
    for (const auto& key : encoded.routing_keys) writer.string(key);
}

}  // namespace

std::string_view encoded_model_input_error_code_name(
    const EncodedModelInputErrorCode code) noexcept {
    switch (code) {
    case EncodedModelInputErrorCode::InvalidLogicalModelInput:
        return "invalid_logical_model_input";
    case EncodedModelInputErrorCode::UnknownPublicPasscode:
        return "unknown_public_passcode";
    case EncodedModelInputErrorCode::InvalidEncodedModelInput:
        return "invalid_encoded_model_input";
    case EncodedModelInputErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

EncodedModelInputResult encode_model_input_v1(
    const LogicalModelInputV1& logical,
    const CardVocabularyV1& vocabulary) noexcept {
    try {
        validate_logical(logical);
        (void)canonical_card_vocabulary_bytes(vocabulary);
        auto encoded = encode_unchecked(logical, vocabulary);
        validate_encoded(encoded);
        return {std::optional<EncodedModelInputV1>(std::move(encoded)), std::nullopt};
    } catch (const EncodingFailure& failure) {
        EncodedModelInputErrorCode code = EncodedModelInputErrorCode::InternalFailure;
        if (failure.reason() == FailureReason::InvalidLogical) {
            code = EncodedModelInputErrorCode::InvalidLogicalModelInput;
        } else if (failure.reason() == FailureReason::UnknownPublicPasscode) {
            code = EncodedModelInputErrorCode::UnknownPublicPasscode;
        } else if (failure.reason() == FailureReason::InvalidEncoded) {
            code = EncodedModelInputErrorCode::InvalidEncodedModelInput;
        }
        const char* diagnostic = "encoded model input failed";
        switch (code) {
        case EncodedModelInputErrorCode::InvalidLogicalModelInput:
            diagnostic = "logical model input is invalid";
            break;
        case EncodedModelInputErrorCode::UnknownPublicPasscode:
            diagnostic = "known public passcode is absent from the immutable vocabulary";
            break;
        case EncodedModelInputErrorCode::InvalidEncodedModelInput:
            diagnostic = "encoded model input is invalid";
            break;
        case EncodedModelInputErrorCode::InternalFailure:
            break;
        }
        return {std::nullopt, EncodedModelInputError{code, diagnostic}};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, EncodedModelInputError{
                                   EncodedModelInputErrorCode::InternalFailure,
                                   "encoded model input failed"}};
    } catch (...) {
        return {std::nullopt, EncodedModelInputError{
                                   EncodedModelInputErrorCode::InternalFailure,
                                   "encoded model input failed"}};
    }
}

std::vector<std::uint8_t> canonical_logical_model_input_bytes(
    const LogicalModelInputV1& logical) {
    try {
        validate_logical(logical);
        return canonical_logical_bytes_unchecked(logical);
    } catch (const EncodingFailure&) {
        throw std::invalid_argument("logical model input is invalid");
    }
}

std::vector<std::uint8_t> canonical_encoded_model_input_bytes(
    const EncodedModelInputV1& encoded) {
    try {
        validate_encoded(encoded);
        Writer writer;
        write_encoded(writer, encoded);
        return std::move(writer).take();
    } catch (const EncodingFailure&) {
        throw std::invalid_argument("encoded model input is invalid");
    }
}

std::vector<std::uint8_t> canonical_model_input_identity_bytes(
    const LogicalModelInputV1& logical,
    const EncodedModelInputV1& encoded) {
    const auto logical_bytes = canonical_logical_model_input_bytes(logical);
    const auto encoded_bytes = canonical_encoded_model_input_bytes(encoded);
    Writer writer;
    writer.string(kModelInputIdentitySchemaId);
    writer.string(kModelInputIdentitySchemaId);
    writer.string(kLogicalModelInputSchemaId);
    writer.string(kEncodedModelInputSchemaId);
    writer.string(encoded.card_vocabulary_identity);
    writer.bytes(logical_bytes);
    writer.bytes(encoded_bytes);
    return std::move(writer).take();
}

std::string model_input_identity(const LogicalModelInputV1& logical,
                                 const EncodedModelInputV1& encoded) {
    return std::string(kModelInputIdentityPrefix) +
           ygo::trace::sha256_bytes(canonical_model_input_identity_bytes(logical, encoded));
}

}  // namespace ygo::model
