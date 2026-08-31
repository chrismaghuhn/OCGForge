#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/observation/chain_state.hpp"
#include "ygo/observation/match_context.hpp"
#include "ygo/observation/observed_card.hpp"
#include "ygo/observation/observed_player_globals.hpp"
#include "ygo/observation/observed_zone.hpp"
#include "ygo/observation/relationship.hpp"
#include "ygo/observation/visible_event.hpp"

namespace ygo::observation {
struct PlayerObservation;
}

namespace ygo::environment {

struct PublicSafeVisibleEvent final {
    std::uint64_t event_index = 0;
    ygo::observation::VisibleEventKind kind =
        ygo::observation::VisibleEventKind::Unknown;
    std::optional<std::uint8_t> player;
    std::optional<ygo::observation::ObservationLocator> entity;
    std::optional<std::uint32_t> public_passcode;
    std::optional<ygo::observation::SemanticZone> from_zone;
    std::optional<ygo::observation::SemanticZone> to_zone;
    std::optional<std::uint32_t> count;
    std::optional<std::int32_t> amount;
    std::optional<std::uint32_t> counter_type;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> winner;
    std::optional<std::uint8_t> win_reason;
    std::optional<std::uint64_t> effect_description;
    std::vector<ygo::observation::ObservationLocator> targets;
};

struct PublicSafeStateDecodeResult;

class PublicSafeStateView final {
public:
    const ygo::observation::ObservedPlayerGlobals& globals() const noexcept {
        return globals_;
    }

    const std::vector<ygo::observation::ObservedZone>& zones() const noexcept {
        return zones_;
    }

    const std::vector<ygo::observation::ObservedCard>& entities() const noexcept {
        return entities_;
    }

    const std::vector<ygo::observation::Relationship>& relationships() const noexcept {
        return relationships_;
    }

    const ygo::observation::ChainState& chain() const noexcept { return chain_; }

    const std::vector<PublicSafeVisibleEvent>& visible_events() const noexcept {
        return visible_events_;
    }

    const ygo::observation::MatchContext& match_context() const noexcept {
        return match_context_;
    }

private:
    PublicSafeStateView() = default;

    ygo::observation::ObservedPlayerGlobals globals_;
    std::vector<ygo::observation::ObservedZone> zones_;
    std::vector<ygo::observation::ObservedCard> entities_;
    std::vector<ygo::observation::Relationship> relationships_;
    ygo::observation::ChainState chain_;
    std::vector<PublicSafeVisibleEvent> visible_events_;
    ygo::observation::MatchContext match_context_;

    friend struct PublicSafeStateDecodeResult;
    friend PublicSafeStateDecodeResult decode_canonical_public_safe_state(
        const std::vector<std::uint8_t>& bytes) noexcept;
    friend std::vector<std::uint8_t> canonical_public_safe_state_bytes(
        const ygo::observation::PlayerObservation& observation);
};

struct PublicSafeStateDecodeResult final {
    std::optional<PublicSafeStateView> value;
    std::optional<std::string> diagnostic;

    explicit operator bool() const noexcept {
        return value.has_value() && !diagnostic.has_value();
    }
};

PublicSafeStateDecodeResult decode_canonical_public_safe_state(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_public_safe_state_bytes(
    const PublicSafeStateView& view);

}  // namespace ygo::environment
