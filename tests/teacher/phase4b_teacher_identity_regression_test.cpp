#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/trajectory/policy_provenance.hpp"
#include "ygo/trajectory/types.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

constexpr std::string_view kRulesBundleId =
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
constexpr std::string_view kSwordsoulProfileId =
    "ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74";
constexpr std::string_view kSalamangreatProfileId =
    "ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55";
constexpr std::string_view kSwordsoulBindingId =
    "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c";
constexpr std::string_view kSalamangreatBindingId =
    "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56";
constexpr std::string_view kSwordsoulArtifactId =
    "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d";
constexpr std::string_view kSalamangreatArtifactId =
    "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527";

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_profile(const StrategyProfileV1& profile,
                   const std::string_view expected_profile_id,
                   const std::uint8_t expected_own_role,
                   const std::string_view expected_own_deck_id,
                   const std::string_view expected_own_deck_sha256,
                   const std::uint8_t expected_opponent_role,
                   const std::string_view expected_opponent_deck_id,
                   const std::string_view expected_opponent_deck_sha256,
                   const std::string_view expected_binding_id,
                   const std::string_view expected_artifact_id) {
    require(profile.profile_id == expected_profile_id,
            "Teacher profile ID changed from the accepted Phase-4B value");
    require(profile.matchup_id == kCertifiedMatchupId &&
                profile.rules_bundle_id == kRulesBundleId &&
                profile.format_id == "TCG_ADVANCED_2026_05_18" &&
                profile.duel_mode == "DUEL_MODE_MR5" &&
                profile.duel_flags == 190464,
            "Teacher profile environment binding changed");
    require(profile.own_deck_role == expected_own_role &&
                profile.own_deck_id == expected_own_deck_id &&
                profile.own_deck_sha256 == expected_own_deck_sha256 &&
                profile.opponent_deck_role == expected_opponent_role &&
                profile.opponent_deck_id == expected_opponent_deck_id &&
                profile.opponent_deck_sha256 == expected_opponent_deck_sha256,
            "Teacher profile deck binding changed");

    const auto config = CertifiedEnvironmentConfig::canonical();
    std::string diagnostic;
    require(validate_strategy_profile_binding(profile, config, &diagnostic),
            "current Teacher profile failed certified binding validation: " +
                diagnostic);
    const auto binding = make_teacher_policy_binding(profile);
    require(validate_teacher_policy_binding(binding, profile, &diagnostic),
            "current Teacher binding failed validation: " + diagnostic);
    const auto artifact = make_teacher_policy_artifact(profile);
    require(binding.teacher_policy_binding_id == expected_binding_id &&
                artifact.policy_artifact_id == expected_artifact_id &&
                artifact.artifact_metadata_identity ==
                    std::optional<std::string>{binding.teacher_policy_binding_id} &&
                artifact.producer_implementation_identity ==
                    kTeacherProducerImplementationIdentity &&
                artifact.sampling_contract_identity ==
                    kDeterministicLexicographicArgmaxSamplingContractIdentity &&
                artifact.policy_rng_contract_identity == kNoPolicyRngContractId,
            "current Teacher PolicyArtifact identity changed");
}

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    for (const auto& assignment : assignments) {
        if (assignment.player == player) {
            return assignment;
        }
    }
    throw std::runtime_error("Teacher assignment missing a player");
}

void test_published_identities_and_assignments() {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    check_profile(
        swordsoul, kSwordsoulProfileId, 0,
        "ocgforge.swordsoul_tenyi.ml_v1",
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
        1, "ocgforge.salamangreat.ml_v1",
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
        kSwordsoulBindingId, kSwordsoulArtifactId);
    check_profile(
        salamangreat, kSalamangreatProfileId, 1,
        "ocgforge.salamangreat.ml_v1",
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
        0, "ocgforge.swordsoul_tenyi.ml_v1",
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
        kSalamangreatBindingId, kSalamangreatArtifactId);

    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    const auto resolver = make_production_policy_provenance_resolver();
    require(resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                 kTeacherProducerImplementationIdentity),
            "accepted Teacher producer registration is missing");
    require(!resolver.can_resolve(
                ProvenanceKind::ProducerImplementation,
                "ocgforge.policy.teacher_core.v2"),
            "unexpected Teacher v2 producer registration exists");
    const auto* sampling = resolver.sampling_contract_capabilities(
        kDeterministicLexicographicArgmaxSamplingContractIdentity);
    require(sampling != nullptr && sampling->complete && sampling->deterministic,
            "accepted deterministic sampling registration changed");
    require(resolver.can_resolve(
                ProvenanceKind::ArtifactMetadataArtifact,
                kSwordsoulBindingId) &&
                resolver.can_resolve(
                    ProvenanceKind::ArtifactMetadataArtifact,
                    kSalamangreatBindingId),
            "accepted Teacher binding registrations are missing");

    const std::array<PolicyRole, 2> roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    for (const auto seat_assignment : {SeatAssignment::Normal,
                                       SeatAssignment::Mirror}) {
        for (const auto starting_player : {std::uint8_t{0},
                                           std::uint8_t{1}}) {
            const auto assignments = make_teacher_participant_assignments(
                swordsoul_artifact, salamangreat_artifact,
                CertifiedEnvironmentConfig::canonical(), seat_assignment,
                starting_player, roles);
            require(assignments.size() == 2,
                    "Teacher assignment matrix did not contain two players");
            for (std::uint8_t player = 0; player < 2; ++player) {
                const auto& assignment = assignment_for_player(assignments, player);
                const auto expected_deck_role =
                    seat_assignment == SeatAssignment::Mirror
                        ? (player == 0 ? DeckRole::SecondLockedDeck
                                       : DeckRole::FirstLockedDeck)
                        : (player == 0 ? DeckRole::FirstLockedDeck
                                       : DeckRole::SecondLockedDeck);
                require(assignment.deck_role == expected_deck_role &&
                            assignment.seat_role ==
                                (player == starting_player
                                     ? SeatRole::StartingPlayer
                                     : SeatRole::NonStartingPlayer),
                        "Teacher seat/deck assignment is not exact");
            }
        }
    }
}

}  // namespace

int main() {
    try {
        test_published_identities_and_assignments();
        std::cout << "phase4b_teacher_identity_regression_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase4b_teacher_identity_regression_test: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
