#include "ygo/phase6/task7_dataset_authority_provisioning_v2.hpp"

#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v2.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ygo::phase6;

constexpr std::string_view kBaseCommit =
    "eaaa851f990584b14aa009a7a9f4796a85bfb10a";
constexpr std::string_view kSwordsoulBindingV2 =
    "ocgforge.teacher_policy_binding.v1.4da70292d08b5608552d9f9246050c2ea5b9c3b52c962ea26a8f9816c5447a5f";
constexpr std::string_view kSalamangreatBindingV2 =
    "ocgforge.teacher_policy_binding.v1.0ec1d4ce29956c72e7e8537d24ba04834dbed4f820ff222d0209f9d7880bc7c0";
constexpr std::string_view kSwordsoulArtifactV2 =
    "policy_artifact.v1.efbd7962734c993d9374acc4c527f722a2413e7279b851d10340a83defccfc01";
constexpr std::string_view kSalamangreatArtifactV2 =
    "policy_artifact.v1.17b2395a97e820c645f59037e7203f17bab808f90c1b70936c1000591efdb40e";

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_exact_schedule_and_identity() {
    const auto schedule = make_task7_collection_schedule_v2(std::string(kBaseCommit));
    require(schedule.jobs.size() == 16, "Task7 V2 schedule did not contain 16 jobs");
    require(schedule.environment_contract_id ==
                "ocgforge.episodic_environment.v3",
            "Task7 V2 schedule was not bound to EpisodicEnvironment V3");

    std::size_t index = 0;
    for (const auto seed : std::array<std::uint64_t, 4>{4, 6, 8, 9}) {
        for (const auto placement :
             std::array<ygo::environment::SeatAssignment, 2>{
                 ygo::environment::SeatAssignment::Normal,
                 ygo::environment::SeatAssignment::Mirror}) {
            for (const auto starting_player : std::array<std::uint8_t, 2>{0, 1}) {
                const auto& job = schedule.jobs[index++];
                require(job.root_seed == seed && job.seat_assignment == placement &&
                            job.starting_player == starting_player,
                        "Task7 V2 schedule order changed");
                require(job.environment_contract_id ==
                            "ocgforge.episodic_environment.v3" &&
                            job.teacher_producer_identity ==
                                "ocgforge.policy.teacher_core.v2" &&
                            job.teacher_action_adapter_identity ==
                                "ocgforge.policy.public_action_key.v2" &&
                            job.policy_rng_identity == "ocgforge.no_policy_rng.v1",
                        "Task7 V2 job version binding is incomplete");
                const bool mirror = placement == ygo::environment::SeatAssignment::Mirror;
                const auto& expected_seat0_binding = mirror ? kSalamangreatBindingV2
                                                             : kSwordsoulBindingV2;
                const auto& expected_seat1_binding = mirror ? kSwordsoulBindingV2
                                                             : kSalamangreatBindingV2;
                const auto& expected_seat0_artifact = mirror ? kSalamangreatArtifactV2
                                                              : kSwordsoulArtifactV2;
                const auto& expected_seat1_artifact = mirror ? kSwordsoulArtifactV2
                                                              : kSalamangreatArtifactV2;
                require(job.teacher_policy_binding_ids[0] == expected_seat0_binding &&
                            job.teacher_policy_binding_ids[1] == expected_seat1_binding &&
                            job.teacher_policy_artifact_ids[0] == expected_seat0_artifact &&
                            job.teacher_policy_artifact_ids[1] == expected_seat1_artifact,
                        "Task7 V2 job Teacher identities changed");
            }
        }
    }
    require(index == 16, "Task7 V2 schedule enumeration was incomplete");

    const auto bytes = canonical_task7_collection_schedule_bytes_v2(schedule);
    const auto decoded = decode_task7_collection_schedule_v2(bytes);
    require(static_cast<bool>(decoded), "Task7 V2 schedule did not decode");
    require(canonical_task7_collection_schedule_bytes_v2(*decoded.value) == bytes,
            "Task7 V2 schedule was not canonically stable");
    require(task7_collection_schedule_identity_v2(schedule) ==
                task7_collection_schedule_identity_v2(*decoded.value),
            "Task7 V2 schedule identity was not deterministic");
    std::cout << "TASK7_V2_SCHEDULE_ID="
              << task7_collection_schedule_identity_v2(schedule) << '\n';
}

void test_fixed_schedule_fails_closed_without_complete_jobs() {
    const auto schedule = make_task7_collection_schedule_v2(std::string(kBaseCommit));
    std::size_t calls = 0;
    const auto result = provision_task7_dataset_authority_v2(
        schedule, [&calls](const Task7CollectionJobV2&) {
            ++calls;
            return ygo::policy::TeacherRunnerV3TrajectoryRunResult{};
        });
    require(calls == 16, "Task7 V2 provisioner did not evaluate every fixed job");
    require(!result && result.outcomes.size() == 16,
            "Task7 V2 provisioner issued authority for missing job artifacts");

    auto v1_environment = schedule;
    v1_environment.jobs.front().environment_contract_id =
        "ocgforge.episodic_environment.v2";
    bool rejected = false;
    try {
        (void)canonical_task7_collection_schedule_bytes_v2(v1_environment);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Task7 V2 schedule accepted a V1 environment contract");
}

void test_job_episode_binding_is_exact() {
    const auto schedule = make_task7_collection_schedule_v2(std::string(kBaseCommit));
    const auto& job = schedule.jobs.front();
    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical_v3();
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = std::string(ygo::environment::kEpisodicEnvironmentV3ContractId);
    spec.root_seed = job.root_seed;
    spec.seat_assignment = job.seat_assignment;
    spec.starting_player = job.starting_player;
    ygo::trajectory::EpisodeEnvelopeV2 envelope;
    envelope.manifest.trusted_trajectory_contract_id =
        std::string(ygo::trajectory::kTrustedTrajectoryV2ContractId);
    envelope.manifest.episodic_environment_contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV3ContractId);
    envelope.manifest.environment_identity_input =
        ygo::environment::canonical_environment_identity_bytes(config);
    envelope.manifest.environment_semantic_id = ygo::environment::environment_semantic_id(config);
    envelope.manifest.episode_identity_input =
        ygo::environment::canonical_episode_identity_bytes(config, spec);
    envelope.manifest.episode_semantic_id = ygo::environment::episode_semantic_id(config, spec);
    envelope.manifest.policy_provenance = make_task7_v2_policy_provenance(job);
    std::string error;
    require(validate_task7_v2_job_episode_binding(job, envelope, &error),
            "Task7 V2 job/episode identity fixture did not validate: " + error);
    auto wrong_job = job;
    wrong_job.root_seed += 1;
    require(!validate_task7_v2_job_episode_binding(wrong_job, envelope, &error),
            "Task7 V2 accepted an episode under the wrong job seed");

    auto extra_artifact = envelope;
    extra_artifact.manifest.policy_provenance.policy_artifacts.push_back(
        ygo::policy::make_teacher_policy_artifact(
            ygo::teacher::make_swordsoul_tenyi_profile()));
    require(!validate_task7_v2_job_episode_binding(job, extra_artifact, &error),
            "Task7 V2 accepted extra Teacher artifact provenance");

    auto extra_assignment = envelope;
    auto assignment = extra_assignment.manifest.policy_provenance.participant_assignments.front();
    assignment.assignment_epoch = 1;
    assignment.effective_from_decision_index = 1;
    assignment.participant_policy_assignment_id =
        ygo::trajectory::compute_participant_policy_assignment_id(assignment);
    extra_assignment.manifest.policy_provenance.participant_assignments.push_back(assignment);
    require(!validate_task7_v2_job_episode_binding(job, extra_assignment, &error),
            "Task7 V2 accepted extra Teacher assignment provenance");
}

void test_authority_closure_rejects_detached_values() {
    const auto schedule = make_task7_collection_schedule_v2(std::string(kBaseCommit));
    const auto vocabulary = ygo::model::CardVocabularyV1::from_ascending_passcodes({100});
    require(static_cast<bool>(vocabulary), "Task7 V2 detached-authority vocabulary fixture failed");
    Task7V2DatasetAuthority detached{
        schedule, {}, ygo::trajectory::DatasetManifestV2{}, TrainingDatasetSplitV1{},
        *vocabulary.value};
    std::string error;
    require(!validate_task7_v2_authority(detached, &error),
            "Task7 V2 detached authority passed closure validation");
}

void test_split_v1_and_public_vocabulary_v1() {
    std::vector<std::string> episode_ids;
    bool has_train = false;
    bool has_validation = false;
    bool has_test = false;
    for (std::uint32_t value = 1; episode_ids.size() < 16 && value < 100000; ++value) {
        std::ostringstream stream;
        stream << std::hex << std::setw(64) << std::setfill('0') << value;
        const auto id = stream.str();
        const auto partition = phase6_partition_for_episode(id);
        if (!partition.has_value()) {
            continue;
        }
        if (*partition == Phase6DatasetPartition::Train) has_train = true;
        if (*partition == Phase6DatasetPartition::Validation) has_validation = true;
        if (*partition == Phase6DatasetPartition::Test) has_test = true;
        episode_ids.push_back(id);
    }
    require(has_train && has_validation && has_test && episode_ids.size() == 16,
            "Task7 V2 split fixture did not cover all partitions");

    const auto split = derive_training_dataset_split_v1_from_v2(
        std::string(64, 'a'), episode_ids);
    require(static_cast<bool>(split), "Task7 V2 split derivation failed");
    require(!split.value->train_episode_ids.empty() &&
                !split.value->validation_episode_ids.empty() &&
                !split.value->test_episode_ids.empty(),
            "Task7 V2 split did not fail/produce the required nonempty partitions");
    auto duplicate_episode_ids = episode_ids;
    duplicate_episode_ids[1] = duplicate_episode_ids[0];
    require(!derive_training_dataset_split_v1_from_v2(
                 std::string(64, 'a'), duplicate_episode_ids),
            "Task7 V2 split accepted duplicate episode IDs");

    ygo::observation::PlayerObservation source;
    source.perspective_player = 0;
    source.match_context.perspective_player = 0;
    source.match_context.own_deck.known = true;
    source.match_context.own_deck.main_deck = {200, 100};
    ygo::observation::VisibleGameEvent event;
    event.event_index = 0;
    event.public_passcode = 300;
    source.visible_events.push_back(event);
    const auto observation = ygo::environment::project_public_observation(source);
    const auto vocabulary = derive_card_vocabulary_v1_from_public_observations({observation});
    require(static_cast<bool>(vocabulary), "Task7 V2 public vocabulary derivation failed");
    require(vocabulary.value->ascending_passcodes() ==
                std::vector<std::uint32_t>{100, 200, 300},
            "Task7 V2 vocabulary was not public, unique, and ascending");
    std::cout << "TASK7_V2_SPLIT_ID=" << split.value->split_identity << '\n'
              << "TASK7_V1_VOCABULARY_ID=" << vocabulary.value->identity() << '\n';
}

}  // namespace

int main() {
    try {
        test_exact_schedule_and_identity();
        test_fixed_schedule_fails_closed_without_complete_jobs();
        test_job_episode_binding_is_exact();
        test_authority_closure_rejects_detached_values();
        test_split_v1_and_public_vocabulary_v1();
        std::cout << "phase6_task7_dataset_authority_provisioning_v2_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task7_dataset_authority_provisioning_v2_test: "
                  << error.what() << '\n';
        return 1;
    }
}
