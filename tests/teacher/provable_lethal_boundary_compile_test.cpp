#include "ygo/teacher/provable_lethal.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

using ExpectedEvaluationSignature =
    ygo::teacher::ProvableLethalEvaluationResult (*)(
        const ygo::teacher::PublicBattleSnapshotV1&) noexcept;
using ExpectedBytesSignature =
    std::vector<std::uint8_t> (*)(
        const ygo::teacher::ProvableLethalCandidateV1&);

static_assert(std::is_same_v<decltype(&ygo::teacher::evaluate_provable_lethal),
                             ExpectedEvaluationSignature>);
static_assert(
    std::is_same_v<
        decltype(&ygo::teacher::canonical_provable_lethal_candidate_bytes),
        ExpectedBytesSignature>);
static_assert(
    std::is_default_constructible_v<ygo::teacher::ProvableLethalCandidateV1>);
static_assert(std::is_default_constructible_v<
              ygo::teacher::ProvableLethalEvaluationResult>);

int main() {
    return 0;
}
