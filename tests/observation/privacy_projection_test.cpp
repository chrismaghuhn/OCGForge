#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

#include "card_projection.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::observation::detail::CardProjectionInput make_card(bool face_up) {
    ygo::observation::detail::CardProjectionInput input;
    input.query.code = 146746;
    input.query.position = face_up ? 0x1U : 0x8U;
    input.query.type = 0x04000001U;
    input.query.attack = 1500;
    input.query.defense = 0;
    input.query.link_rating = 2;
    input.query.link_markers = 34;
    input.query.is_public = face_up ? 1 : 0;
    input.owner = 1;
    input.controller = 1;
    input.zone = ygo::observation::SemanticZone::MonsterZone;
    input.sequence = 3;
    input.locator = {"p1:MONSTER_ZONE:3"};
    input.identity_visible = face_up;
    input.current_features_visible = face_up;
    input.sequence_visible = true;
    return input;
}

int run() {
    const auto hidden = ygo::observation::detail::project_card(make_card(false));
    require(!hidden.identity_known && !hidden.passcode.has_value(), "hidden card identity was not redacted");
    require(!hidden.current.has_value() && !hidden.printed.has_value(),
            "hidden card identity-derived features were not redacted");
    require(hidden.controller.value() == 1 && hidden.sequence.value() == 3,
            "safe hidden card coordinates were removed");

    const auto visible = ygo::observation::detail::project_card(make_card(true));
    require(visible.identity_known && visible.passcode.value() == 146746,
            "visible card identity was over-redacted");
    require(visible.current.has_value() && visible.current->attack.value() == 1500,
            "visible current card state was not projected");
    std::cout << "privacy_projection=ok\n";
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
