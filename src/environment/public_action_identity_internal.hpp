#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ygo::environment::detail {

struct PublicActionBinding final {
    std::string public_action_key;
    std::string internal_semantic_key;
};

std::optional<std::string> resolve_public_action_key(
    const std::vector<PublicActionBinding>& bindings, std::string_view public_key);

}  // namespace ygo::environment::detail
