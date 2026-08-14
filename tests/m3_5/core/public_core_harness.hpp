#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ocgapi.h"
#include "ocgapi_types.h"
#include "ygo/core/core_host.hpp"

namespace m35::core_test {

class PublicCoreDuel final {
public:
    struct State;

    PublicCoreDuel();
    ~PublicCoreDuel();

    PublicCoreDuel(const PublicCoreDuel&) = delete;
    PublicCoreDuel& operator=(const PublicCoreDuel&) = delete;

    void load_deck(std::uint8_t team, const ygo::core::FixtureDeck& deck);
    void load_script(const std::filesystem::path& path);
    int set_starting_player(std::uint8_t player);
    OCG_Duel handle() const;
    void start();
    void process_once();

    std::optional<std::uint8_t> first_new_turn_player() const;
    const std::vector<std::uint8_t>& last_message() const noexcept;
    std::uint32_t query_count(std::uint8_t team, std::uint32_t location) const;
    std::vector<std::uint8_t> query(const OCG_QueryInfo& info) const;

private:
    State* state_ = nullptr;
};

}  // namespace m35::core_test
