#include "public_core_harness.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>

#include "card_data_reader.hpp"
#include "ocgapi_constants.h"
#include "script_reader.hpp"
#include "ygo/m3/canonical_rules.hpp"

namespace m35::core_test {

struct PublicCoreDuel::State {
    ygo::core::detail::CardDataStore card_data;
    std::string callback_error;
    std::string log;
    ygo::core::detail::CardDataCallbackContext card_payload{&card_data, &callback_error};
    ygo::core::detail::ScriptStore scripts{YGO_M35_CARDSCRIPTS, {}};
    std::pair<ygo::core::detail::ScriptStore*, std::string*> script_payload{&scripts, &callback_error};
    OCG_Duel duel = nullptr;
    std::vector<std::uint8_t> last_message;

    State() {
        card_data.load(YGO_M0_CARD_DATA_TSV);
        OCG_DuelOptions options{};
        options.seed[0] = 0x0123456789abcdefULL;
        options.seed[1] = 0xfedcba9876543210ULL;
        options.seed[2] = 0x13579bdf2468ace0ULL;
        options.seed[3] = 0x0eca8642fdb97531ULL;
        options.flags = ygo::m3::canonical_rules().duel_flags;
        options.team1 = {8000, 5, 1};
        options.team2 = options.team1;
        options.cardReader = ygo::core::detail::card_data_callback;
        options.payload1 = &card_payload;
        options.scriptReader = ygo::core::detail::script_reader_callback;
        options.payload2 = &script_payload;
        options.logHandler = ygo::core::detail::log_callback;
        options.payload3 = &log;
        options.cardReaderDone = ygo::core::detail::card_data_done_callback;
        options.payload4 = &card_data;
        options.enableUnsafeLibraries = 0;

        if (OCG_CreateDuel(&duel, &options) != OCG_DUEL_CREATION_SUCCESS || duel == nullptr) {
            throw std::runtime_error("OCG_CreateDuel failed in M3.5 public API harness");
        }
        for (const auto* script : {"constant.lua", "utility.lua", "proc_normal.lua"}) {
            if (scripts.load(duel, script, &callback_error) == 0) {
                throw std::runtime_error("global script failed in M3.5 public API harness: " +
                                         std::string(script));
            }
        }
    }

    ~State() {
        if (duel != nullptr) {
            OCG_DestroyDuel(duel);
        }
    }
};

namespace {

void require_callback_ok(const PublicCoreDuel::State& state, const char* operation) {
    if (!state.callback_error.empty()) {
        throw std::runtime_error(std::string(operation) + ": " + state.callback_error);
    }
}

void add_card(PublicCoreDuel::State& state, std::uint8_t team, std::uint32_t code,
              std::uint32_t location, std::uint32_t sequence) {
    OCG_NewCardInfo info{};
    info.team = team;
    info.duelist = 0;
    info.code = code;
    info.con = team;
    info.loc = location;
    info.seq = sequence;
    info.pos = POS_FACEDOWN_DEFENSE;
    OCG_DuelNewCard(state.duel, &info);
    require_callback_ok(state, "OCG_DuelNewCard");
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("truncated M3.5 public API message");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

}  // namespace

PublicCoreDuel::PublicCoreDuel() : state_(new State()) {}

PublicCoreDuel::~PublicCoreDuel() {
    delete state_;
    state_ = nullptr;
}

void PublicCoreDuel::load_deck(std::uint8_t team, const ygo::core::FixtureDeck& deck) {
    if (team > 1 || deck.main_deck.size() < 40) {
        throw std::runtime_error("M3.5 public API harness received an invalid deck");
    }
    for (std::size_t index = 0; index < deck.main_deck.size(); ++index) {
        add_card(*state_, team, deck.main_deck[index], LOCATION_DECK, static_cast<std::uint32_t>(index));
    }
    for (std::size_t index = 0; index < deck.extra_deck.size(); ++index) {
        add_card(*state_, team, deck.extra_deck[index], LOCATION_EXTRA, static_cast<std::uint32_t>(index));
    }
}

void PublicCoreDuel::load_script(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open M3.5 fixture script: " + path.string());
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto name = path.filename().string();
    if (OCG_LoadScript(state_->duel, bytes.data(), static_cast<std::uint32_t>(bytes.size()), name.c_str()) == 0) {
        throw std::runtime_error("OCG_LoadScript rejected M3.5 fixture script");
    }
    require_callback_ok(*state_, "OCG_LoadScript");
}

OCG_Duel PublicCoreDuel::handle() const {
    return state_->duel;
}

void PublicCoreDuel::start() {
    OCG_StartDuel(state_->duel);
    require_callback_ok(*state_, "OCG_StartDuel");
}

void PublicCoreDuel::process_once() {
    (void)OCG_DuelProcess(state_->duel);
    require_callback_ok(*state_, "OCG_DuelProcess");
    std::uint32_t length = 0;
    const auto* raw = static_cast<const std::uint8_t*>(OCG_DuelGetMessage(state_->duel, &length));
    state_->last_message.assign(raw == nullptr ? nullptr : raw, raw == nullptr ? nullptr : raw + length);
}

std::optional<std::uint8_t> PublicCoreDuel::first_new_turn_player() const {
    std::size_t offset = 0;
    while (offset < state_->last_message.size()) {
        if (offset + 4 > state_->last_message.size()) {
            throw std::runtime_error("truncated M3.5 message frame");
        }
        const auto size = read_u32(state_->last_message, offset);
        offset += 4;
        if (size == 0 || offset + size > state_->last_message.size()) {
            throw std::runtime_error("invalid M3.5 message frame");
        }
        if (state_->last_message[offset] == MSG_NEW_TURN) {
            if (size != 2) {
                throw std::runtime_error("invalid MSG_NEW_TURN in M3.5 test");
            }
            return state_->last_message[offset + 1];
        }
        offset += size;
    }
    return std::nullopt;
}

const std::vector<std::uint8_t>& PublicCoreDuel::last_message() const noexcept {
    return state_->last_message;
}

std::uint32_t PublicCoreDuel::query_count(std::uint8_t team, std::uint32_t location) const {
    return OCG_DuelQueryCount(state_->duel, team, location);
}

std::vector<std::uint8_t> PublicCoreDuel::query(const OCG_QueryInfo& info) const {
    std::uint32_t length = 0;
    const auto* raw = static_cast<const std::uint8_t*>(OCG_DuelQuery(state_->duel, &length, &info));
    if (raw == nullptr) {
        return {};
    }
    return {raw, raw + length};
}

}  // namespace m35::core_test
