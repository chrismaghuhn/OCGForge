#pragma once

#include <filesystem>
#include <string>

#include "ocgapi_types.h"

namespace ygo::core::detail {

class ScriptStore final {
public:
    explicit ScriptStore(std::filesystem::path root) : root_(std::move(root)) {}

    int load(OCG_Duel duel, const char* name, std::string* error);

private:
    std::filesystem::path root_;
};

int script_reader_callback(void* payload, OCG_Duel duel, const char* name);
void log_callback(void* payload, const char* message, int type);

}  // namespace ygo::core::detail
