#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "ygo/policy/runner.hpp"

namespace ygo::policy::detail {

enum class TestSelectorBehavior : std::uint8_t {
    InvalidPublicAction = 0,
    PolicyFailure = 1,
};

struct PolicyRunnerTestOverride final {
    std::uint8_t player = 0;
    TestSelectorBehavior behavior = TestSelectorBehavior::InvalidPublicAction;
    std::shared_ptr<std::size_t> selection_calls;
};

struct PolicyRunnerTestAccess final {
    static PolicyRunnerResult run_with_test_selector(
        PolicyRunner& runner, const std::uint8_t player, const TestSelectorBehavior behavior,
        std::shared_ptr<std::size_t> selection_calls = nullptr) {
        const PolicyRunnerTestOverride override{player, behavior, std::move(selection_calls)};
        return runner.run_impl(&override);
    }
};

inline PolicyRunnerResult run_with_test_selector(
    PolicyRunner& runner, const std::uint8_t player, const TestSelectorBehavior behavior,
    std::shared_ptr<std::size_t> selection_calls = nullptr) {
    return PolicyRunnerTestAccess::run_with_test_selector(
        runner, player, behavior, std::move(selection_calls));
}

}  // namespace ygo::policy::detail
