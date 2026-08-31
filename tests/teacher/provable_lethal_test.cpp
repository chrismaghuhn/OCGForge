#include "ygo/teacher/provable_lethal.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"

namespace {

using ygo::environment::EnvironmentActionKind;
using ygo::environment::PublicActionKeyInput;
using ygo::teacher::ProvableLethalCandidateV1;
using ygo::teacher::ProvableLethalEvaluationResult;
using ygo::teacher::ProvableLethalStatus;
using ygo::teacher::PublicBattleCandidateClass;
using ygo::teacher::PublicBattleCandidateFactsV1;
using ygo::teacher::PublicBattleCandidateStatus;
using ygo::teacher::PublicBattleSnapshotV1;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string public_key(const EnvironmentActionKind action_kind,
                       const std::optional<std::uint32_t>& phase = std::nullopt,
                       const std::optional<std::uint32_t>& choice = std::nullopt) {
    PublicActionKeyInput input;
    input.action_kind =
        std::string(ygo::environment::environment_action_kind_name(action_kind));
    input.phase = phase;
    if (choice.has_value()) {
        input.choice = ygo::environment::PublicChoice{
            ygo::environment::PublicChoiceKind::EffectChoice, *choice,
            std::nullopt};
    }
    return ygo::environment::public_action_key(input);
}

PublicBattleCandidateFactsV1 non_battle_fact() {
    PublicBattleCandidateFactsV1 value;
    value.public_action_key = public_key(EnvironmentActionKind::IdleCommand);
    value.status = PublicBattleCandidateStatus::NotApplicable;
    value.battle_candidate_class =
        PublicBattleCandidateClass::NonBattleCandidate;
    return value;
}

PublicBattleCandidateFactsV1 battle_fact(
    const std::string& key, const std::vector<std::string>& reasons,
    const std::optional<std::int32_t>& attack = std::nullopt) {
    PublicBattleCandidateFactsV1 value;
    value.public_action_key = key;
    value.status = PublicBattleCandidateStatus::Unsupported;
    value.battle_candidate_class =
        PublicBattleCandidateClass::BattleCommandUnclassified;
    value.source_current_attack = attack;
    value.reason_ids = reasons;
    return value;
}

PublicBattleSnapshotV1 valid_snapshot() {
    PublicBattleSnapshotV1 snapshot;
    snapshot.perspective_player = 0;
    snapshot.decision_index = 23;
    snapshot.decision_context_kind = "battle_command";
    snapshot.turn_phase = 0x80;
    snapshot.self_life_points = 8000;
    snapshot.opponent_life_points = 1;
    snapshot.candidate_facts = {
        non_battle_fact(),
        battle_fact(
            public_key(EnvironmentActionKind::BattleCommand, 1, 0),
            {"battle.command_subtype_unproven"}, 999999),
        battle_fact(
            public_key(EnvironmentActionKind::BattleCommand, 1, 1),
            {"battle.command_subtype_unproven", "battle.source_redacted"}),
        battle_fact(
            public_key(EnvironmentActionKind::BattleCommand, 1, 2),
            {"battle.command_subtype_unproven",
             "battle.source_position_unknown"}),
    };
    return snapshot;
}

bool throws_invalid(const ProvableLethalCandidateV1& value) {
    try {
        (void)ygo::teacher::canonical_provable_lethal_candidate_bytes(value);
    } catch (...) {
        return true;
    }
    return false;
}

void test_status_mapping_and_complete_alignment() {
    const auto snapshot = valid_snapshot();
    const auto result = ygo::teacher::evaluate_provable_lethal(snapshot);
    require(result.valid, "valid snapshot was rejected by lethal evaluator");
    require(result.candidates.size() == snapshot.candidate_facts.size(),
            "lethal evaluator changed the complete candidate count");
    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        require(result.candidates[index].public_action_key ==
                    snapshot.candidate_facts[index].public_action_key,
                "lethal evaluator changed candidate order or public key");
        require(result.candidates[index].status !=
                    ProvableLethalStatus::ProvenLethal,
                "current corpus emitted PROVEN_LETHAL");
        require(!result.candidates[index]
                     .guaranteed_opponent_lp_loss_lower_bound.has_value(),
                "current fail-closed evaluator emitted a lower bound");
        if (snapshot.candidate_facts[index].battle_candidate_class ==
            PublicBattleCandidateClass::BattleCommandUnclassified) {
            require(result.candidates[index].status ==
                        ProvableLethalStatus::Unsupported,
                    "current BattleCommand was not UNSUPPORTED");
        }
    }

    require(result.candidates[0].status ==
                ProvableLethalStatus::NotApplicable &&
                result.candidates[0].proof_reason_ids.empty(),
            "non-battle candidate was not NOT_APPLICABLE");
    require(result.candidates[1].status == ProvableLethalStatus::Unsupported &&
                result.candidates[1].proof_reason_ids ==
                    std::vector<std::string>{
                        "lethal.battle_command_unclassified",
                        "lethal.current_action_proof_unavailable"},
            "current BattleCommand did not use the authoritative unsupported reasons");
    require(result.candidates[2].status == ProvableLethalStatus::Unsupported &&
                result.candidates[3].status == ProvableLethalStatus::Unsupported,
            "redacted/unknown-position BattleCommand was not unsupported");
    require(result.candidates[2].proof_reason_ids ==
                result.candidates[1].proof_reason_ids,
            "redacted current action received an optimistic lethal status");
}

void test_valid_snapshot_preserves_candidate_level_invalid() {
    auto snapshot = valid_snapshot();
    snapshot.candidate_facts[1].status =
        PublicBattleCandidateStatus::Invalid;
    require(ygo::teacher::validate_public_battle_snapshot(snapshot),
            "candidate-level INVALID made the whole snapshot invalid");

    const auto result = ygo::teacher::evaluate_provable_lethal(snapshot);
    require(result.valid &&
                result.candidates.size() == snapshot.candidate_facts.size(),
            "candidate-level INVALID did not preserve a valid evaluation");
    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        require(result.candidates[index].public_action_key ==
                    snapshot.candidate_facts[index].public_action_key,
                "candidate-level INVALID changed N-domain order or key");
    }
    require(result.candidates[1].status == ProvableLethalStatus::Invalid &&
                !result.candidates[1]
                     .guaranteed_opponent_lp_loss_lower_bound.has_value() &&
                result.candidates[1].proof_reason_ids ==
                    std::vector<std::string>{
                        "lethal.snapshot_candidate_invalid"},
            "candidate-level INVALID did not map to the required lethal result");
}

void test_atk_lp_trap_and_missing_proof_remain_closed() {
    auto snapshot = valid_snapshot();
    snapshot.candidate_facts[1].source_current_attack = 999999;
    snapshot.opponent_life_points = 1;
    const auto result = ygo::teacher::evaluate_provable_lethal(snapshot);
    require(result.valid &&
                result.candidates[1].status !=
                    ProvableLethalStatus::ProvenLethal &&
                !result.candidates[1]
                     .guaranteed_opponent_lp_loss_lower_bound.has_value(),
            "ATK >= LP was treated as provable lethal");
    require(result.candidates[1].proof_reason_ids ==
                std::vector<std::string>{
                    "lethal.battle_command_unclassified",
                    "lethal.current_action_proof_unavailable"},
            "missing direct/target proof was not represented deterministically");
}

void test_invalid_snapshot_fails_without_partial_output() {
    auto wrong_schema = valid_snapshot();
    wrong_schema.schema_id = "ocgforge.other.v1";
    const auto invalid_schema =
        ygo::teacher::evaluate_provable_lethal(wrong_schema);
    require(!invalid_schema.valid && invalid_schema.candidates.empty(),
            "invalid snapshot schema produced partial lethal output");

    auto duplicate_keys = valid_snapshot();
    duplicate_keys.candidate_facts.push_back(
        duplicate_keys.candidate_facts.front());
    const auto duplicate =
        ygo::teacher::evaluate_provable_lethal(duplicate_keys);
    require(!duplicate.valid && duplicate.candidates.empty(),
            "duplicate snapshot key was repaired or evaluated");

    auto empty = valid_snapshot();
    empty.candidate_facts.clear();
    const auto empty_result = ygo::teacher::evaluate_provable_lethal(empty);
    require(!empty_result.valid && empty_result.candidates.empty(),
            "empty snapshot fabricated a lethal domain");
}

void test_canonical_candidate_bytes_are_strict_and_deterministic() {
    const auto result =
        ygo::teacher::evaluate_provable_lethal(valid_snapshot());
    require(result.valid, "baseline lethal result is invalid");
    for (const auto& candidate : result.candidates) {
        const auto first =
            ygo::teacher::canonical_provable_lethal_candidate_bytes(candidate);
        const auto second =
            ygo::teacher::canonical_provable_lethal_candidate_bytes(candidate);
        require(!first.empty() && first == second,
                "lethal candidate bytes were not deterministic");
    }

    auto unsupported = result.candidates[1];
    unsupported.schema_id = "ocgforge.other.v1";
    require(throws_invalid(unsupported),
            "wrong lethal schema was accepted");

    unsupported = result.candidates[1];
    unsupported.public_action_key = "not-a-public-action-key";
    require(throws_invalid(unsupported),
            "invalid lethal public key was accepted");

    unsupported = result.candidates[1];
    unsupported.proof_reason_ids = {"z", "a"};
    require(throws_invalid(unsupported),
            "unsorted lethal reasons were accepted");

    unsupported = result.candidates[1];
    unsupported.proof_reason_ids = {"a", "a"};
    require(throws_invalid(unsupported),
            "duplicate lethal reasons were accepted");

    unsupported = result.candidates[1];
    unsupported.guaranteed_opponent_lp_loss_lower_bound = 0;
    require(throws_invalid(unsupported),
            "unsupported result with a zero lower bound was accepted");

    auto malformed_status = result.candidates[1];
    malformed_status.status = static_cast<ProvableLethalStatus>(9);
    require(throws_invalid(malformed_status),
            "unknown lethal status code was accepted");

    auto not_proven = result.candidates[1];
    not_proven.status = ProvableLethalStatus::NotProven;
    not_proven.proof_reason_ids = {"lethal.proof_incomplete"};
    not_proven.guaranteed_opponent_lp_loss_lower_bound.reset();
    require(!throws_invalid(not_proven),
            "frozen NOT_PROVEN status could not be canonically represented");

    auto not_applicable = result.candidates[0];
    require(!throws_invalid(not_applicable),
            "frozen NOT_APPLICABLE status could not be canonically represented");
}

}  // namespace

int main() {
    try {
        test_status_mapping_and_complete_alignment();
        test_valid_snapshot_preserves_candidate_level_invalid();
        test_atk_lp_trap_and_missing_proof_remain_closed();
        test_invalid_snapshot_fails_without_partial_output();
        test_canonical_candidate_bytes_are_strict_and_deterministic();
        std::cout << "provable_lethal_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "provable_lethal_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
