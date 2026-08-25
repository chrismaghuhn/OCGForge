#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "ygo/observation/player_observation.hpp"

#ifdef YGO_M4_PERFORMANCE_AUDIT
#include "ygo/observation/performance_audit.hpp"
#endif

namespace ygo::core {
class CoreHost;
}

namespace ygo::observation {

enum class ObservationFinalization {
    Immediate,
    Deferred,
};

struct ObservationBuildConfig {
    MatchKnowledgeConfig knowledge;
    StaticDeckContext own_deck;
    StaticDeckContext opponent_deck;
    std::uint64_t decision_index = 0;
    std::uint64_t engine_step_index = 0;
    std::vector<VisibleGameEvent> visible_events;
    std::optional<DecisionContext> decision_context;
    // Deferred is reserved for the canonical decision build -> attach path;
    // it returns with an empty observation_hash until attach_decision_context
    // performs the single final hash. Immediate preserves the public default.
    ObservationFinalization finalization = ObservationFinalization::Immediate;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    PerformanceAuditCollector* performance_audit = nullptr;
#endif
};

PlayerObservation build_player_observation(const ygo::core::CoreHost& host,
                                           std::uint8_t perspective_player,
                                           const ObservationBuildConfig& config = {});

}  // namespace ygo::observation
