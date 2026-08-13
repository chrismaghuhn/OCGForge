#pragma once

#include <array>
#include <cstdint>

namespace ygo::core {

struct SeedBundle {
    std::array<std::uint64_t, 4> words{};

    bool operator==(const SeedBundle& other) const noexcept { return words == other.words; }
    bool operator!=(const SeedBundle& other) const noexcept { return !(*this == other); }
};

}  // namespace ygo::core
