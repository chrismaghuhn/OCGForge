#include "ygo/teacher/public_battle_snapshot.hpp"

#include <type_traits>
#include <utility>

using ExpectedExtractionSignature =
    ygo::teacher::PublicBattleSnapshotExtractionResult (*)(
        const ygo::environment::PublicEnvironmentObservation&,
        const std::vector<ygo::environment::EnvironmentActionCandidate>&) noexcept;

static_assert(std::is_same_v<decltype(&ygo::teacher::extract_public_battle_snapshot),
                             ExpectedExtractionSignature>);
static_assert(std::is_default_constructible_v<ygo::teacher::PublicBattleSnapshotV1>);
static_assert(std::is_default_constructible_v<ygo::teacher::PublicBattleCandidateFactsV1>);
static_assert(std::is_same_v<decltype(ygo::teacher::PublicBattleSnapshotV1{}.candidate_facts),
                             std::vector<ygo::teacher::PublicBattleCandidateFactsV1>>);

int main() {
    return 0;
}
