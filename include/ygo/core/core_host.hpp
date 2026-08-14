#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "ygo/core/card_data.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/core/seed_bundle.hpp"

struct OCG_QueryInfo;

namespace ygo::core {

struct CoreHostConfig {
    RulesBundlePaths rules;
    SeedBundle seed;
    std::uint64_t duel_flags = 0;
    std::uint32_t starting_lp = 8000;
    std::uint32_t starting_draw_count = 5;
    std::uint32_t draw_count_per_turn = 1;
    std::optional<std::uint8_t> starting_player;
    std::vector<std::uint32_t> required_script_codes;

    std::uint8_t effective_starting_player() const noexcept {
        return starting_player.value_or(0);
    }
};

struct ProcessResult {
    int status = 0;
    std::vector<std::uint8_t> message;
};

class CoreHost final {
public:
    explicit CoreHost(CoreHostConfig config);
    ~CoreHost();

    CoreHost(const CoreHost&) = delete;
    CoreHost& operator=(const CoreHost&) = delete;
    CoreHost(CoreHost&&) = delete;
    CoreHost& operator=(CoreHost&&) = delete;

    void load_deck(std::uint8_t team, const FixtureDeck& deck);
    void load_fixture_card(std::uint8_t team, std::uint32_t code, std::uint32_t location,
                           std::uint32_t sequence, std::uint32_t position);
    void load_fixture_script(const std::filesystem::path& path);
    void start_duel();
    ProcessResult process();
    void submit_response(const std::vector<std::uint8_t>& response);

    std::uint32_t query_count(std::uint8_t team, std::uint32_t location) const;
    std::vector<std::uint8_t> query(const OCG_QueryInfo& info) const;
    std::vector<std::uint8_t> query_location(const OCG_QueryInfo& info) const;
    std::vector<std::uint8_t> query_field() const;
    std::optional<StaticCardData> static_card_data(std::uint32_t code) const;

    const CoreHostConfig& config() const noexcept { return config_; }
    std::size_t response_submission_count() const noexcept { return response_submission_count_; }
    std::size_t process_call_count() const noexcept { return process_call_count_; }
    int api_major() const noexcept { return api_major_; }
    int api_minor() const noexcept { return api_minor_; }

private:
    struct Impl;
    void throw_if_callback_error(const char* operation) const;
    CoreHostConfig config_;
    Impl* impl_ = nullptr;
    std::size_t response_submission_count_ = 0;
    std::size_t process_call_count_ = 0;
    int api_major_ = 0;
    int api_minor_ = 0;
};

}  // namespace ygo::core
