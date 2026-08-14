#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "ocgapi_types.h"

namespace ygo::core::detail {

class ScriptStore final {
public:
    ScriptStore(std::filesystem::path root, const std::vector<std::uint32_t>& required_codes)
        : root_(std::move(root)), required_codes_(required_codes.begin(), required_codes.end()) {}

    int load(OCG_Duel duel, const char* name, std::string* error);

private:
    std::filesystem::path root_;
    std::unordered_set<std::uint32_t> required_codes_;
};

int script_reader_callback(void* payload, OCG_Duel duel, const char* name);
void log_callback(void* payload, const char* message, int type);

}  // namespace ygo::core::detail
