#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/recorder_v3.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PolicyProvenanceEnvelope production_v3_provenance() {
    const auto swordsoul = make_teacher_policy_artifact_v3(
        ygo::teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat = make_teacher_policy_artifact_v3(
        ygo::teacher::make_salamangreat_profile());
    PolicyProvenanceEnvelope result;
    result.policy_artifacts = {swordsoul, salamangreat};
    std::sort(result.policy_artifacts.begin(), result.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    const auto config = CertifiedEnvironmentConfig::canonical_v4();
    result.participant_assignments = make_teacher_participant_assignments(
        swordsoul, salamangreat, config, SeatAssignment::Normal, 0,
        {PolicyRole::Behavior, PolicyRole::Opponent});
    return result;
}

void test_v3_recorder_seals_v4_interruption() {
    const auto config = CertifiedEnvironmentConfig::canonical_v4();
    EpisodeSpec spec;
    spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    spec.root_seed = 2;
    RunControl control;
    control.engine_process_budget = 64;
    control.semantic_action_budget = 64;
    control.cancellation.source = "v3-recorder-test";

    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "V4 environment factory rejected recorder fixture");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(reset), "V4 reset was rejected");
    const auto& next = std::get<ResetAccepted>(reset).next;
    const auto* frame = std::get_if<DecisionFrame>(&next);
    require(frame != nullptr && frame->contract_id == kEpisodicEnvironmentV4ContractId,
            "V4 reset did not publish the expected frame");

    const auto provenance = production_v3_provenance();
    TrajectoryRecorderV3 recorder(config, spec, provenance,
                                  make_production_policy_provenance_resolver());
    require(recorder.on_reset_accepted(std::get<ResetAccepted>(reset)),
            "V3 recorder rejected the V4 reset");
    const auto interruption = environment->interrupt(
        InterruptRequest{std::string(kEpisodicEnvironmentV4ContractId),
                         InterruptionReason::AdministrativeCancel});
    const auto* accepted = std::get_if<InterruptAccepted>(&interruption);
    require(accepted != nullptr, "V4 environment interruption was rejected");
    require(recorder.on_interrupt_accepted(
                std::optional<DecisionFrame>{*frame}, *accepted),
            "V3 recorder rejected the pending V4 interruption");
    const auto envelope = recorder.seal();
    require(envelope.has_value() &&
                std::holds_alternative<InterruptedClosureV3>(envelope->closure),
            "V3 recorder did not seal an InterruptedClosureV3");
    const auto resealed = recorder.seal();
    require(resealed.has_value() &&
                canonical_episode_envelope_bytes_v3(*envelope) ==
                    canonical_episode_envelope_bytes_v3(*resealed),
            "V3 recorder produced unstable envelope bytes");
    const auto* interrupted = std::get_if<InterruptedClosureV3>(&envelope->closure);
    require(interrupted != nullptr && interrupted->pending_unacted_frame.has_value() &&
                !interrupted->pending_unacted_frame->request.candidates.empty() &&
                interrupted->pending_unacted_frame->request.candidates.front().public_action_key
                    .rfind("public_action.v3.", 0) == 0,
            "V3 recorder did not preserve V3 public keys");
}

}  // namespace

int main() {
    try {
        test_v3_recorder_seals_v4_interruption();
        std::cout << "trusted trajectory V3 recorder tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
