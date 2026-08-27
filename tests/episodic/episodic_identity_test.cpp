#include "ygo/environment/episodic_environment.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> from_hex(const std::string& value) {
    require(value.size() % 2 == 0, "golden vector has odd hex length");
    std::vector<std::uint8_t> result;
    result.reserve(value.size() / 2);
    const auto digit = [](const char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        throw std::runtime_error("golden vector contains a non-lowercase hex digit");
    };
    for (std::size_t index = 0; index < value.size(); index += 2) {
        result.push_back(static_cast<std::uint8_t>((digit(value[index]) << 4) | digit(value[index + 1])));
    }
    return result;
}

void test_environment_vector() {
    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical();
    const auto bytes = ygo::environment::canonical_environment_identity_bytes(config);
    require(bytes.size() == 1453, "environment identity byte length changed");
    require(ygo::environment::environment_semantic_id(config) ==
                "9048c70765192ad2fd06ba7d0960809c4891688b15395379e2c266a457dc94c5",
            "environment identity golden digest failed");
    require(config.environment_semantic_id == ygo::environment::environment_semantic_id(config),
            "canonical environment config did not retain its computed identity");
}

void test_episode_vector() {
    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical();
    ygo::environment::EpisodeSpec spec;
    spec.root_seed = 1;
    spec.seat_assignment = ygo::environment::SeatAssignment::Normal;
    spec.starting_player = 0;
    const auto bytes = ygo::environment::canonical_episode_identity_bytes(config, spec);
    require(bytes.size() == 383, "episode identity byte length changed");
    require(ygo::environment::episode_semantic_id(config, spec) ==
                "5b4c04d53af2e049744eced3cff763429d21dd1e4762d334df510c7a5cfd6700",
            "episode identity golden digest failed");
    auto changed = spec;
    changed.starting_player = 1;
    require(ygo::environment::episode_semantic_id(config, changed) !=
                ygo::environment::episode_semantic_id(config, spec),
            "starting-player mutation did not change episode identity");
}

void test_decision_vector() {
    const auto bytes = ygo::environment::canonical_semantic_decision_identity_bytes(
        "episode-fixed", 0, "protocol-fixed", 0, 6, "obs-fixed", "digest-fixed");
    require(bytes == from_hex(
                         "000000266f6367666f7267652e73656d616e7469635f6465636973696f6e5f6964656e746974792e7631000000266f6367666f7267652e73656d616e7469635f6465636973696f6e5f6964656e746974792e76310000000d657069736f64652d666978656400000000000000000000000e70726f746f636f6c2d6669786564000000000000000006000000096f62732d66697865640000000c6469676573742d6669786564"),
            "semantic decision identity bytes golden vector failed");
    require(ygo::environment::semantic_decision_id("episode-fixed", 0, "protocol-fixed", 0, 6,
                                                    "obs-fixed", "digest-fixed") ==
                "33c88c4e7971dfd495c735f0402e993556312cf6828eff99524d8a6ad71ab71d",
            "semantic decision identity golden digest failed");
}

}  // namespace

int main() {
    try {
        test_environment_vector();
        test_episode_vector();
        test_decision_vector();
        std::cout << "episodic_identity_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
