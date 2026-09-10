#include "ygo/environment/episodic_environment.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

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

void test_v4_environment_identity() {
    static_assert(ygo::environment::kEpisodicEnvironmentV4ContractId ==
                  "ocgforge.episodic_environment.v4");
    static_assert(ygo::environment::kEnvironmentIdentityV4SchemaId ==
                  "ocgforge.environment_identity.v4");

    const auto v3 = ygo::environment::CertifiedEnvironmentConfig::canonical_v3();
    const auto v4 = ygo::environment::CertifiedEnvironmentConfig::canonical_v4();
    require(v4.contract_id == ygo::environment::kEpisodicEnvironmentV4ContractId &&
                v4.public_action_identity_schema_id ==
                    ygo::environment::kPublicActionIdentityV3SchemaId &&
                v4.public_candidate_digest_schema_id ==
                    ygo::environment::kPublicCandidateDomainV3SchemaId &&
                v4.public_decision_identity_schema_id ==
                    ygo::environment::kPublicSemanticDecisionIdentityV3SchemaId,
            "canonical V4 config did not bind the V3 public identity generation");
    require(v4.environment_semantic_id != v3.environment_semantic_id &&
                v4.environment_semantic_id ==
                    ygo::environment::environment_semantic_id(v4),
            "canonical V4 environment identity did not recompute exactly");
    const auto bytes = ygo::environment::canonical_environment_identity_bytes(v4);
    require(!bytes.empty(), "V4 environment identity bytes were empty");

    const auto decoded = ygo::trajectory::decode_environment_identity_input_v4(bytes);
    require(decoded && decoded.value.has_value() &&
                decoded.value->contract_id == v4.contract_id &&
                decoded.value->environment_semantic_id == v4.environment_semantic_id,
            "V4 environment identity did not decode through the V4 resolver");
    require(!ygo::trajectory::decode_environment_identity_input_v3(bytes).value.has_value() &&
                !ygo::trajectory::decode_environment_identity_input(bytes).value.has_value(),
            "historical environment resolvers accepted V4 identity bytes");
    require(ygo::trajectory::is_current_certified_environment_v4(v4) &&
                !ygo::trajectory::is_current_certified_environment_v3(v4) &&
                !ygo::trajectory::is_current_certified_environment(v4),
            "V4 environment resolver generation boundary was not strict");
}

void test_v4_episode_identity() {
    const auto v3 = ygo::environment::CertifiedEnvironmentConfig::canonical_v3();
    const auto v4 = ygo::environment::CertifiedEnvironmentConfig::canonical_v4();
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = ygo::environment::kEpisodicEnvironmentV4ContractId;
    spec.root_seed = 2;
    spec.seat_assignment = ygo::environment::SeatAssignment::Mirror;
    spec.starting_player = 1;
    const auto bytes = ygo::environment::canonical_episode_identity_bytes(v4, spec);
    const auto decoded = ygo::trajectory::decode_episode_identity_input_v4(bytes, v4);
    require(decoded && decoded.value.has_value() &&
                decoded.value->contract_id == ygo::environment::kEpisodicEnvironmentV4ContractId &&
                decoded.value->root_seed == spec.root_seed &&
                decoded.value->seat_assignment == spec.seat_assignment &&
                decoded.value->starting_player == spec.starting_player,
            "V4 episode identity did not decode exactly");
    require(!ygo::trajectory::decode_episode_identity_input_v3(bytes, v4).value.has_value() &&
                !ygo::trajectory::decode_episode_identity_input(bytes, v4).value.has_value(),
            "historical episode resolvers accepted V4 episode identity bytes");

    const auto v3_bytes = ygo::environment::canonical_episode_identity_bytes(v3, spec);
    require(!ygo::trajectory::decode_episode_identity_input_v4(v3_bytes, v4).value.has_value(),
            "V4 episode resolver accepted V3 parent identity bytes");
}

}  // namespace

int main() {
    try {
        test_v4_environment_identity();
        test_v4_episode_identity();
        std::cout << "episodic environment V4 identity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
