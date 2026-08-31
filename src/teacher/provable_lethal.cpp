#include "ygo/teacher/provable_lethal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trajectory/codec.hpp"
#include "teacher_validation.hpp"

namespace ygo::teacher {
namespace {

constexpr std::string_view kSnapshotCandidateInvalidReason =
    "lethal.snapshot_candidate_invalid";
constexpr std::string_view kBattleCommandUnclassifiedReason =
    "lethal.battle_command_unclassified";
constexpr std::string_view kCurrentActionProofUnavailableReason =
    "lethal.current_action_proof_unavailable";

bool valid_status(const ProvableLethalStatus value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(ProvableLethalStatus::Invalid);
}

bool valid_reason_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!detail::canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_candidate(const ProvableLethalCandidateV1& value) noexcept {
    if (value.schema_id != kProvableLethalSchemaId ||
        !environment::is_public_action_key(value.public_action_key) ||
        !valid_status(value.status) ||
        value.proof_reason_ids.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        !valid_reason_vector(value.proof_reason_ids)) {
        return false;
    }

    if (value.status == ProvableLethalStatus::NotApplicable) {
        return !value.guaranteed_opponent_lp_loss_lower_bound.has_value() &&
               value.proof_reason_ids.empty();
    }
    if (value.status == ProvableLethalStatus::ProvenLethal) {
        return value.guaranteed_opponent_lp_loss_lower_bound.has_value();
    }
    return !value.guaranteed_opponent_lp_loss_lower_bound.has_value();
}

void append_reason_vector(trajectory::ByteWriter& writer,
                          const std::vector<std::string>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value);
    }
}

}  // namespace

ProvableLethalEvaluationResult evaluate_provable_lethal(
    const PublicBattleSnapshotV1& snapshot) noexcept {
    ProvableLethalEvaluationResult result;
    try {
        if (!validate_public_battle_snapshot(snapshot)) {
            return result;
        }

        result.candidates.reserve(snapshot.candidate_facts.size());
        for (const auto& facts : snapshot.candidate_facts) {
            ProvableLethalCandidateV1 candidate;
            candidate.public_action_key = facts.public_action_key;

            if (facts.battle_candidate_class ==
                    PublicBattleCandidateClass::NonBattleCandidate &&
                facts.status == PublicBattleCandidateStatus::NotApplicable) {
                candidate.status = ProvableLethalStatus::NotApplicable;
            } else if (facts.status ==
                       PublicBattleCandidateStatus::Invalid) {
                candidate.status = ProvableLethalStatus::Invalid;
                candidate.proof_reason_ids = {
                    std::string(kSnapshotCandidateInvalidReason)};
            } else if (facts.battle_candidate_class ==
                           PublicBattleCandidateClass::
                               BattleCommandUnclassified &&
                       facts.status ==
                           PublicBattleCandidateStatus::Unsupported) {
                candidate.status = ProvableLethalStatus::Unsupported;
                candidate.proof_reason_ids = {
                    std::string(kBattleCommandUnclassifiedReason),
                    std::string(kCurrentActionProofUnavailableReason)};
            } else {
                return ProvableLethalEvaluationResult{};
            }

            if (!valid_candidate(candidate)) {
                return ProvableLethalEvaluationResult{};
            }
            result.candidates.push_back(std::move(candidate));
        }
        result.valid = true;
        return result;
    } catch (...) {
        return ProvableLethalEvaluationResult{};
    }
}

std::vector<std::uint8_t> canonical_provable_lethal_candidate_bytes(
    const ProvableLethalCandidateV1& value) {
    if (!valid_candidate(value)) {
        throw std::invalid_argument(
            "provable lethal candidate is not canonical");
    }

    trajectory::ByteWriter writer;
    writer.string(kProvableLethalSchemaId);
    writer.string(kProvableLethalSchemaId);
    writer.string(value.public_action_key);
    writer.u8(static_cast<std::uint8_t>(value.status));
    writer.boolean(
        value.guaranteed_opponent_lp_loss_lower_bound.has_value());
    if (value.guaranteed_opponent_lp_loss_lower_bound.has_value()) {
        writer.u64be(*value.guaranteed_opponent_lp_loss_lower_bound);
    }
    append_reason_vector(writer, value.proof_reason_ids);
    return std::move(writer).take();
}

}  // namespace ygo::teacher
