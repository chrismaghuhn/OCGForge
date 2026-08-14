#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ocgapi_constants.h"
#include "event_projection.hpp"
#include "ygo/observation/observation_session.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) { bytes.push_back(value); }
void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}
void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}
void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}
void append_location(std::vector<std::uint8_t>& bytes, std::uint8_t controller,
                     std::uint8_t location, std::uint32_t sequence, std::uint32_t position) {
    append_u8(bytes, controller);
    append_u8(bytes, location);
    append_u32(bytes, sequence);
    append_u32(bytes, position);
}
void append_frame(std::vector<std::uint8_t>& stream, const std::vector<std::uint8_t>& frame) {
    append_u32(stream, static_cast<std::uint32_t>(frame.size()));
    stream.insert(stream.end(), frame.begin(), frame.end());
}

int run() {
    std::vector<std::uint8_t> messages;
    append_frame(messages, {MSG_NEW_TURN, 1});
    std::vector<std::uint8_t> phase{MSG_NEW_PHASE};
    append_u16(phase, PHASE_MAIN1);
    append_frame(messages, phase);

    std::vector<std::uint8_t> hidden_move{MSG_MOVE};
    append_u32(hidden_move, 0xdeadbeef);
    append_location(hidden_move, 1, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE);
    append_location(hidden_move, 1, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    append_u32(hidden_move, 0);
    append_frame(messages, hidden_move);

    std::vector<std::uint8_t> visible_move{MSG_MOVE};
    append_u32(visible_move, 1234);
    append_location(visible_move, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    append_location(visible_move, 0, LOCATION_MZONE, 2, POS_FACEUP_ATTACK);
    append_u32(visible_move, 0);
    append_frame(messages, visible_move);

    append_frame(messages, {MSG_SHUFFLE_DECK, 1});

    std::vector<std::uint8_t> confirm{MSG_CONFIRM_CARDS, 0};
    append_u32(confirm, 1);
    append_u32(confirm, 555);
    append_location(confirm, 1, LOCATION_HAND, 2, POS_FACEDOWN_DEFENSE);
    append_frame(messages, confirm);

    std::vector<std::uint8_t> lp{MSG_LPUPDATE, 0};
    append_u32(lp, 7200);
    append_frame(messages, lp);

    std::vector<std::uint8_t> chain{MSG_CHAINING};
    append_u32(chain, 1234);
    append_location(chain, 0, LOCATION_MZONE, 2, POS_FACEUP_ATTACK);
    append_u8(chain, 0);
    append_u8(chain, LOCATION_MZONE);
    append_u32(chain, 2);
    append_u64(chain, 99);
    append_u32(chain, 1);
    append_frame(messages, chain);
    append_frame(messages, {MSG_CHAIN_SOLVED, 1});
    append_frame(messages, {MSG_CHAIN_END});

    std::vector<std::uint8_t> counter{MSG_ADD_COUNTER};
    append_u16(counter, 7);
    append_u8(counter, 0);
    append_u8(counter, LOCATION_MZONE);
    append_u8(counter, 2);
    append_u16(counter, 3);
    append_frame(messages, counter);

    std::vector<std::uint8_t> equip{MSG_EQUIP};
    append_location(equip, 0, LOCATION_SZONE, 0, POS_FACEUP_ATTACK);
    append_location(equip, 0, LOCATION_MZONE, 2, POS_FACEUP_ATTACK);
    append_frame(messages, equip);

    ygo::observation::ObservationSession session(0);
    session.ingest(messages, 42);
    const auto& events = session.visible_events();
    require(events.size() >= 10, "required visible event subset was not emitted");
    bool saw_boundary = false;
    bool saw_hidden_redaction = false;
    bool saw_visible_code = false;
    bool saw_reveal = false;
    for (const auto& event : events) {
        saw_boundary = saw_boundary || event.kind == ygo::observation::VisibleEventKind::RandomizationBoundary;
        saw_hidden_redaction = saw_hidden_redaction ||
                               (event.kind == ygo::observation::VisibleEventKind::CardMoved &&
                                !event.public_passcode.has_value());
        saw_visible_code = saw_visible_code || event.public_passcode.value_or(0) == 1234;
        saw_reveal = saw_reveal || event.public_passcode.value_or(0) == 555;
        require(event.engine_step_index == 42, "event engine step was not retained");
    }
    require(saw_boundary, "shuffle did not create a knowledge-destroying boundary");
    require(saw_hidden_redaction, "hidden move code leaked into visible event history");
    require(saw_visible_code && saw_reveal, "positive visible event payload was over-redacted");
    require(events.front().event_index == 0 && events.back().event_index + 1 == events.size(),
            "event indices were not session-canonical");

    std::vector<std::uint8_t> knowledge_boundary;
    std::vector<std::uint8_t> move_to_hidden_deck{MSG_MOVE};
    append_u32(move_to_hidden_deck, 777);
    append_location(move_to_hidden_deck, 1, LOCATION_MZONE, 3, POS_FACEUP_ATTACK);
    append_location(move_to_hidden_deck, 1, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE);
    append_u32(move_to_hidden_deck, REASON_RETURN);
    append_frame(knowledge_boundary, move_to_hidden_deck);
    append_frame(knowledge_boundary, {MSG_SHUFFLE_DECK, 1});
    const auto previous_event_count = events.size();
    session.ingest(knowledge_boundary, 43);
    const auto& after_boundary = session.visible_events();
    bool saw_boundary_after_hidden_move = false;
    bool hidden_destination_has_no_locator = true;
    for (std::size_t index = previous_event_count; index < after_boundary.size(); ++index) {
        const auto& event = after_boundary[index];
        saw_boundary_after_hidden_move = saw_boundary_after_hidden_move ||
                                         event.kind == ygo::observation::VisibleEventKind::RandomizationBoundary;
        if ((event.kind == ygo::observation::VisibleEventKind::CardMoved ||
             event.kind == ygo::observation::VisibleEventKind::CardReturned) &&
            event.to_zone == ygo::observation::SemanticZone::MainDeck) {
            hidden_destination_has_no_locator = hidden_destination_has_no_locator && !event.entity.has_value();
        }
    }
    require(saw_boundary_after_hidden_move, "knowledge-destroying shuffle boundary was not recorded");
    require(hidden_destination_has_no_locator, "hidden deck transition retained a trackable locator");
    std::cout << "event_projection=ok\n";
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
