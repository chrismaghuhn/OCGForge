#include "ygo/environment/public_safe_state.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::phase6;
using namespace ygo::trajectory;

constexpr std::string_view kSourceCommit =
    "e5717ad9c3a29cf386b9cfe8951f6307cd731748";
constexpr std::uint64_t kDecisionLimit = 236;
constexpr std::string_view kHiitaLocator =
    "p1:EXTRA_DECK:public:48815792:0";

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool exposes_hiita(const DecisionRecordV3& record) {
    const auto safe = decode_canonical_public_safe_state(
        record.frame.public_observation.canonical_safe_state_bytes());
    if (!safe) return false;
    const auto entity = std::find_if(
        safe.value->entities().begin(), safe.value->entities().end(),
        [](const auto& value) {
            return value.locator.value == kHiitaLocator && value.identity_known &&
                   value.passcode.has_value() && *value.passcode == 48815792;
        });
    return entity != safe.value->entities().end();
}

void test_bounded_job0_diagnostic_entrypoint() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kSourceCommit));
    require(schedule.jobs.size() == 16, "Task7 V3 canonical schedule is not 16 jobs");
    require(schedule.jobs.front().root_seed == 4 &&
                schedule.jobs.front().seat_assignment == SeatAssignment::Normal &&
                schedule.jobs.front().starting_player == 0,
            "current Task7 V3 Job0 does not match the canonical target");

    const auto job_bytes_before = canonical_task7_collection_job_bytes_v3(
        schedule.jobs.front());
    const auto schedule_bytes_before = canonical_task7_collection_schedule_bytes_v3(schedule);
    const auto job_id_before = task7_collection_job_identity_v3(schedule.jobs.front());
    const auto schedule_id_before = task7_collection_schedule_identity_v3(schedule);

    const auto result = run_task7_collection_job_v3_bounded_for_diagnostics(
        schedule.jobs.front(), kDecisionLimit);
    require(!result.error.has_value() && result.envelope.has_value() && !result.quarantined,
            "bounded Task7 V3 Job0 execution failed: " + result.diagnostic);
    const auto* interrupted = std::get_if<InterruptedClosureV3>(
        &result.envelope->closure);
    require(interrupted != nullptr && interrupted->record_count == kDecisionLimit &&
                interrupted->pending_unacted_frame.has_value() &&
                interrupted->pending_unacted_frame->decision_index == kDecisionLimit,
            "bounded Job0 did not close at the non-selected decision-limit frame");
    require(!std::holds_alternative<TerminalClosureV3>(result.envelope->closure),
            "bounded Job0 diagnostic unexpectedly created a terminal closure");

    require(result.envelope->records.size() == kDecisionLimit &&
                result.envelope->records.back().frame.decision_index == 235,
            "bounded Job0 did not execute exactly decisions 0 through 235");
    const auto hiita_it = std::find_if(
        result.envelope->records.begin(), result.envelope->records.end(),
        [](const auto& record) {
            if (record.frame.decision_index != 234 ||
                record.frame.request.kind != EnvironmentDecisionKind::IdleCommand ||
                !exposes_hiita(record)) {
                return false;
            }
            return std::any_of(
                record.frame.request.candidates.begin(),
                record.frame.request.candidates.end(),
                [&record](const auto& candidate) {
                    return candidate.action_kind == EnvironmentActionKind::IdleCommand &&
                           candidate.source_reference.has_value() &&
                           candidate.source_reference->kind ==
                               PublicCardReferenceKind::VisibleCard &&
                           candidate.source_reference->observation_locator == kHiitaLocator &&
                           candidate.public_action_key == record.selected_public_action_key;
                });
        });
    require(hiita_it != result.envelope->records.end(),
            "bounded real Task7 Job0 did not reach/select public Hiita");
    const auto material_it = std::next(hiita_it);
    require(material_it != result.envelope->records.end() &&
                material_it->frame.decision_index == 235 &&
                material_it->frame.request.kind == EnvironmentDecisionKind::UnselectCard,
            "bounded real Job0 did not reach the immediate UnselectCard boundary");

    std::size_t select_count = 0;
    std::size_t cancel_count = 0;
    std::size_t unselect_count = 0;
    std::vector<std::string> keys;
    for (const auto& candidate : material_it->frame.request.candidates) {
        require(is_public_action_key_v3(candidate.public_action_key),
                "bounded Job0 material domain contains a non-V3 action key");
        keys.push_back(candidate.public_action_key);
        if (candidate.action_kind == EnvironmentActionKind::CardSelection) {
            require(candidate.card_selection_operation ==
                        PublicCardSelectionOperation::Select,
                    "bounded Job0 material candidate is not Select");
            ++select_count;
        } else if (candidate.action_kind == EnvironmentActionKind::Cancel) {
            require(candidate.card_selection_operation ==
                        PublicCardSelectionOperation::None,
                    "bounded Job0 Cancel is not None");
            ++cancel_count;
        } else {
            throw std::runtime_error("bounded Job0 material domain has an unexpected action");
        }
        if (candidate.card_selection_operation ==
            PublicCardSelectionOperation::Unselect) {
            ++unselect_count;
        }
    }
    require(select_count == 2 && cancel_count == 1 && unselect_count == 0 &&
                material_it->frame.request.candidates.size() == 3,
            "bounded Job0 material domain is not exactly 2 Select + Cancel");
    require(material_it->frame.public_candidate_domain_digest ==
                public_candidate_domain_digest_v3("unselect_card", keys),
            "bounded Job0 material domain digest does not recompute");
    PublicSemanticDecisionIdentityInputV3 decision;
    decision.episode_semantic_id = material_it->frame.episode_semantic_id;
    decision.decision_index = material_it->frame.decision_index;
    decision.acting_player = material_it->frame.acting_player;
    decision.request_kind = "unselect_card";
    decision.public_observation_digest = material_it->frame.public_observation_digest;
    decision.public_candidate_domain_digest = material_it->frame.public_candidate_domain_digest;
    decision.public_action_keys = keys;
    require(material_it->frame.public_semantic_decision_id ==
                public_semantic_decision_id_v3(decision),
            "bounded Job0 material decision identity does not recompute");

    const auto selected = std::find_if(
        material_it->frame.request.candidates.begin(),
        material_it->frame.request.candidates.end(),
        [&](const auto& candidate) {
            return candidate.public_action_key == material_it->selected_public_action_key;
        });
    require(selected != material_it->frame.request.candidates.end() &&
                selected->action_kind == EnvironmentActionKind::CardSelection &&
                selected->card_selection_operation == PublicCardSelectionOperation::Select,
            "bounded Job0 did not select a material Select action");
    require(material_it->successor.kind == SuccessorKind::NextFrame &&
                material_it->successor.next_frame.has_value(),
            "bounded Job0 material transition lacks a forward successor");
    require(material_it->successor.next_frame->next_decision_index == kDecisionLimit,
            "bounded Job0 successor does not point to the non-selected limit frame");

    require(canonical_task7_collection_job_bytes_v3(schedule.jobs.front()) == job_bytes_before &&
                task7_collection_job_identity_v3(schedule.jobs.front()) == job_id_before &&
                canonical_task7_collection_schedule_bytes_v3(schedule) == schedule_bytes_before &&
                task7_collection_schedule_identity_v3(schedule) == schedule_id_before,
            "diagnostic bound changed canonical Task7 job or schedule identity");

    const auto envelope_bytes = canonical_episode_envelope_bytes_v3(*result.envelope);
    std::cout << "BOUNDED_JOB0_ENTRYPOINT=run_task7_collection_job_v3_bounded_for_diagnostics\n"
              << "JOB0_ID=" << job_id_before << '\n'
              << "SCHEDULE_ID=" << schedule_id_before << '\n'
              << "HIITA_DECISION_INDEX=234\n"
              << "FIRST_MATERIAL_DECISION_INDEX=235\n"
              << "FIRST_MATERIAL_DOMAIN_DIGEST="
              << material_it->frame.public_candidate_domain_digest << '\n'
              << "SELECTED_MATERIAL_PUBLIC_ACTION_KEY="
              << material_it->selected_public_action_key << '\n'
              << "BOUNDED_ENVELOPE_SHA256="
              << ygo::trace::sha256_bytes(envelope_bytes) << '\n'
              << "BOUNDED_CLOSURE=InterruptedClosure\n"
              << "BOUNDED_OUTPUT_ADMISSION_ELIGIBLE=NO\n";
}

}  // namespace

int main() {
    try {
        test_bounded_job0_diagnostic_entrypoint();
        std::cout << "phase6_task9a_bounded_job0_diagnostic_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task9a_bounded_job0_diagnostic_test: "
                  << error.what() << '\n';
        return 1;
    }
}
