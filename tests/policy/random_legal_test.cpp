#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/production.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/random_legal.hpp"
#include "ygo/policy/rng.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::policy::PolicyRngInitializationInput rng_input() {
    ygo::policy::PolicyRngInitializationInput input;
    input.policy_rng_root_seed = 0x0123456789abcdefULL;
    input.participant_policy_assignment_id =
        "participant_policy_assignment.v1." + std::string(64, 'b');
    input.policy_rng_stream_id = "player0";
    return input;
}

ygo::environment::PublicEnvironmentObservation public_observation() {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.match_context.perspective_player = 0;
    return ygo::environment::project_public_observation(source);
}

std::string public_key(const std::string& action_kind,
                      const ygo::environment::PublicChoice choice) {
    ygo::environment::PublicActionKeyInput input;
    input.action_kind = action_kind;
    input.choice = choice;
    return ygo::environment::public_action_key(input);
}

std::vector<ygo::environment::EnvironmentActionCandidate> candidates() {
    using ygo::environment::EnvironmentActionCandidate;
    using ygo::environment::EnvironmentActionKind;
    using ygo::environment::PublicChoice;
    using ygo::environment::PublicChoiceKind;

    const auto first = public_key("yes_no", PublicChoice{PublicChoiceKind::YesNo, 0, std::nullopt});
    const auto second = public_key("yes_no", PublicChoice{PublicChoiceKind::YesNo, 1, std::nullopt});
    const auto third = public_key("chain", PublicChoice{PublicChoiceKind::EffectChoice, 7, std::nullopt});

    EnvironmentActionCandidate first_candidate;
    first_candidate.action_kind = EnvironmentActionKind::YesNo;
    first_candidate.public_action_key = first;
    first_candidate.choice = PublicChoice{PublicChoiceKind::YesNo, 0, std::nullopt};

    EnvironmentActionCandidate second_candidate;
    second_candidate.action_kind = EnvironmentActionKind::YesNo;
    second_candidate.public_action_key = second;
    second_candidate.choice = PublicChoice{PublicChoiceKind::YesNo, 1, std::nullopt};

    EnvironmentActionCandidate third_candidate;
    third_candidate.action_kind = EnvironmentActionKind::Chain;
    third_candidate.public_action_key = third;
    third_candidate.choice = PublicChoice{PublicChoiceKind::EffectChoice, 7, std::nullopt};

    // Deliberately not lexical order; the policy must use this exact vector order.
    return {third_candidate, first_candidate, second_candidate};
}

ygo::trajectory::PolicyProvenanceEnvelope provenance_for(
    const ygo::trajectory::PolicyArtifact& artifact,
    const ygo::environment::CertifiedEnvironmentConfig& config,
    const ygo::environment::SeatAssignment seat_assignment,
    const std::uint8_t starting_player,
    const std::array<ygo::trajectory::PolicyRole, 2>& roles) {
    ygo::trajectory::PolicyProvenanceEnvelope result;
    result.policy_artifacts = {artifact};
    result.participant_assignments = ygo::policy::make_random_legal_participant_assignments(
        artifact, config, seat_assignment, starting_player, roles);
    return result;
}

ygo::trajectory::PolicyProvenanceEnvelope rebound_provenance_for(
    const ygo::trajectory::PolicyArtifact& artifact,
    const std::vector<ygo::trajectory::ParticipantPolicyAssignment>& template_assignments) {
    ygo::trajectory::PolicyProvenanceEnvelope result;
    result.policy_artifacts = {artifact};
    result.participant_assignments = template_assignments;
    for (auto& assignment : result.participant_assignments) {
        assignment.policy_artifact_id = artifact.policy_artifact_id;
        assignment.participant_policy_assignment_id =
            ygo::trajectory::compute_participant_policy_assignment_id(assignment);
    }
    std::sort(result.participant_assignments.begin(), result.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    return result;
}

void test_random_legal_execution_binding_composition() {
    const auto artifact = ygo::policy::make_random_legal_policy_artifact();
    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical();
    const std::array<ygo::trajectory::PolicyRole, 2> roles = {
        ygo::trajectory::PolicyRole::Behavior, ygo::trajectory::PolicyRole::Opponent};
    const auto assignments = ygo::policy::make_random_legal_participant_assignments(
        artifact, config, ygo::environment::SeatAssignment::Normal, 0, roles);
    constexpr std::uint64_t root = 0x0123456789abcdefULL;
    std::vector<std::string> stream_identities;

    for (const auto& assignment : assignments) {
        const auto stream_id = assignment.player == 0 ? std::string("player0")
                                                      : std::string("player1");
        const auto binding = ygo::policy::make_random_legal_execution_binding(
            artifact, assignment, root, stream_id);

        require(binding.initialization.policy_rng_contract_identity ==
                    artifact.policy_rng_contract_identity,
                "execution binding initialization used the wrong RNG contract");
        require(binding.initialization.policy_rng_stream_id == stream_id,
                "execution binding initialization used the wrong stream");
        require(!binding.initialization.initialization_material.empty(),
                "execution binding discarded raw initialization material");

        const auto decoded_material =
            ygo::policy::decode_canonical_policy_rng_initialization_material(
                binding.initialization.initialization_material);
        require(static_cast<bool>(decoded_material),
                "execution binding raw initialization material did not decode");
        require(decoded_material.value->policy_rng_root_seed.value() == root &&
                    decoded_material.value->participant_policy_assignment_id ==
                        assignment.participant_policy_assignment_id &&
                    decoded_material.value->policy_rng_stream_id == stream_id,
                "execution binding raw initialization material changed its inputs");

        const auto initialization_bytes =
            ygo::trajectory::canonical_policy_rng_initialization_identity_bytes(
                binding.initialization);
        const auto decoded_initialization =
            ygo::trajectory::decode_policy_rng_initialization_identity(initialization_bytes);
        require(static_cast<bool>(decoded_initialization),
                "execution binding initialization identity codec rejected the value");
        require(decoded_initialization.value->policy_rng_initialization_identity ==
                    binding.initialization.policy_rng_initialization_identity,
                "execution binding initialization identity did not recompute");
        require(ygo::trajectory::compute_policy_rng_initialization_id(binding.initialization) ==
                    binding.initialization.policy_rng_initialization_identity,
                "execution binding initialization authority disagreed with the identity");

        const auto stream_bytes =
            ygo::trajectory::canonical_policy_rng_stream_identity_bytes(binding.stream);
        const auto decoded_stream =
            ygo::trajectory::decode_policy_rng_stream_identity(stream_bytes);
        require(static_cast<bool>(decoded_stream),
                "execution binding stream identity codec rejected the value");
        require(ygo::trajectory::compute_policy_rng_stream_id(binding.stream) ==
                    binding.stream.policy_rng_identity,
                "execution binding stream authority disagreed with the identity");
        require(binding.stream.policy_artifact_id == artifact.policy_artifact_id &&
                    binding.stream.participant_policy_assignment_id ==
                        assignment.participant_policy_assignment_id &&
                    binding.stream.policy_rng_contract_identity ==
                        artifact.policy_rng_contract_identity &&
                    binding.stream.policy_rng_stream_id == stream_id &&
                    binding.stream.policy_rng_initialization_identity ==
                        binding.initialization.policy_rng_initialization_identity,
                "execution binding stream fields were not copied from the inputs");

        require(binding.execution_binding.policy_artifact_id ==
                        binding.stream.policy_artifact_id &&
                    binding.execution_binding.participant_policy_assignment_id ==
                        binding.stream.participant_policy_assignment_id &&
                    binding.execution_binding.policy_rng_contract_identity ==
                        binding.stream.policy_rng_contract_identity &&
                    binding.execution_binding.policy_rng_stream_id ==
                        binding.stream.policy_rng_stream_id &&
                    binding.execution_binding.policy_rng_initialization_identity ==
                        binding.stream.policy_rng_initialization_identity &&
                    binding.execution_binding.policy_rng_identity ==
                        binding.stream.policy_rng_identity,
                "immutable execution binding diverged from stream identity");
        stream_identities.push_back(binding.stream.policy_rng_identity);
    }
    require(stream_identities.size() == 2 && stream_identities[0] != stream_identities[1],
            "different participants received the same policy RNG identity");
}

void test_production_registrations_and_composition() {
    using ygo::trajectory::ProvenanceKind;

    const auto resolver = ygo::policy::make_production_policy_provenance_resolver();
    const std::array<std::pair<ProvenanceKind, std::string>, 6> expected = {{
        {ProvenanceKind::ProducerImplementation, "ocgforge.policy.random_legal.v1"},
        {ProvenanceKind::InferenceAdapter, "ocgforge.policy.direct_execution.v1"},
        {ProvenanceKind::ObservationAdapter, "ocgforge.policy.public_observation.v1"},
        {ProvenanceKind::ActionAdapter, "ocgforge.policy.public_action_key.v1"},
        {ProvenanceKind::SamplingContract, "ocgforge.policy.uniform_below_u64.v1"},
        {ProvenanceKind::PolicyRngContract, "ocgforge.policy_rng.sha256_counter.v1"},
    }};
    for (const auto& entry : expected) {
        require(resolver.can_resolve(entry.first, entry.second),
                "production provenance registration is missing");
    }
    require(!resolver.can_resolve(ProvenanceKind::ActionAdapter,
                                  "ocgforge.policy.uniform_below_u64.v1"),
            "sampling registration crossed into action authority");
    require(resolver.can_resolve(ProvenanceKind::PolicyRngContract,
                                 ygo::trajectory::kNoPolicyRngContractId),
            "production resolver lost the typed NONE RNG registration");
    require(!ygo::trajectory::ProvenanceResolver{}.can_resolve(
                ProvenanceKind::ProducerImplementation,
                "ocgforge.policy.random_legal.v1"),
            "default trajectory resolver trusted a production producer identity");

    const auto* sampling = resolver.sampling_contract_capabilities(
        "ocgforge.policy.uniform_below_u64.v1");
    require(sampling != nullptr && sampling->complete && !sampling->deterministic,
            "production sampling capabilities are incorrect");
    const auto* rng = resolver.policy_rng_contract_descriptor(
        "ocgforge.policy_rng.sha256_counter.v1");
    require(rng != nullptr && rng->initialization_material_is_canonical &&
                rng->cursor_is_unique,
            "production RNG descriptor lacks typed callbacks");

    const auto artifact = ygo::policy::make_random_legal_policy_artifact();
    require(artifact.policy_kind == ygo::trajectory::PolicyKind::RandomLegal,
            "production artifact has the wrong policy kind");
    require(artifact.model_checkpoint_identity == std::nullopt &&
                artifact.search_contract_identity == std::nullopt &&
                artifact.demonstration_source_identity == std::nullopt,
            "RandomLegal artifact carries an unrelated artifact identity");
    require(artifact.policy_rng_contract_identity ==
                "ocgforge.policy_rng.sha256_counter.v1",
            "RandomLegal artifact has the wrong RNG contract");

    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical();
    const std::array<ygo::trajectory::PolicyRole, 2> roles = {
        ygo::trajectory::PolicyRole::Behavior, ygo::trajectory::PolicyRole::Opponent};
    for (const auto seat_assignment : {ygo::environment::SeatAssignment::Normal,
                                       ygo::environment::SeatAssignment::Mirror}) {
        for (const auto starting_player : {std::uint8_t{0}, std::uint8_t{1}}) {
            const auto assignments = ygo::policy::make_random_legal_participant_assignments(
                artifact, config, seat_assignment, starting_player, roles);
            require(assignments.size() == 2,
                    "production assignment setup did not create two players");
            require(std::is_sorted(assignments.begin(), assignments.end(),
                                   [](const auto& left, const auto& right) {
                                       return left.participant_policy_assignment_id <
                                              right.participant_policy_assignment_id;
                                   }),
                    "production assignments were not sorted by typed identity");
            for (const auto& assignment : assignments) {
                const auto expected_deck_index =
                    seat_assignment == ygo::environment::SeatAssignment::Mirror
                        ? 1u - static_cast<unsigned int>(assignment.player)
                        : static_cast<unsigned int>(assignment.player);
                require(assignment.assignment_epoch == 0 &&
                            assignment.effective_from_decision_index == 0,
                        "production assignment was not epoch zero");
                require(assignment.policy_role == roles[assignment.player],
                        "production assignment did not use the caller-provided role");
                require(assignment.resolved_locked_deck_id ==
                            config.locked_decks[expected_deck_index].id,
                        "production assignment used the wrong locked deck");
            }

            ygo::environment::EpisodeSpec spec;
            spec.seat_assignment = seat_assignment;
            spec.starting_player = starting_player;
            std::string error;
            require(resolver.validate(provenance_for(artifact, config, seat_assignment,
                                                     starting_player, roles),
                                      config, spec, &error),
                    "complete production RandomLegal provenance was rejected");
        }
    }
}

void test_strict_rng_callbacks_and_incompatible_provenance() {
    using ygo::trajectory::PolicyRngInitializationIdentity;
    using ygo::trajectory::ProvenanceKind;
    using ygo::trajectory::ProvenanceRegistration;

    const auto resolver = ygo::policy::make_production_policy_provenance_resolver();
    const auto* descriptor = resolver.policy_rng_contract_descriptor(
        "ocgforge.policy_rng.sha256_counter.v1");
    require(descriptor != nullptr, "production RNG descriptor is missing");
    const auto initialization = ygo::policy::make_policy_rng_initialization(rng_input());
    require(static_cast<bool>(initialization), "valid callback initialization was rejected");
    const auto& material = initialization.value->initialization_material;
    require(descriptor->initialization_material_is_canonical(material),
            "production callback rejected canonical RNG material");

    auto truncated = material;
    truncated.pop_back();
    require(!descriptor->initialization_material_is_canonical(truncated),
            "production callback accepted truncated RNG material");
    auto trailing = material;
    trailing.push_back(0);
    require(!descriptor->initialization_material_is_canonical(trailing),
            "production callback accepted trailing RNG material");
    auto wrong_domain = material;
    wrong_domain[4] ^= 1;
    require(!descriptor->initialization_material_is_canonical(wrong_domain),
            "production callback accepted a wrong initialization domain");
    auto invalid_stream = material;
    invalid_stream.back() = 'P';
    require(!descriptor->initialization_material_is_canonical(invalid_stream),
            "production callback accepted a noncanonical stream token");

    PolicyRngInitializationIdentity identity;
    identity.policy_rng_contract_identity =
        "ocgforge.policy_rng.sha256_counter.v1";
    identity.policy_rng_stream_id = "player0";
    identity.initialization_material = material;
    identity.policy_rng_initialization_identity =
        ygo::trajectory::compute_policy_rng_initialization_id(identity);
    require(descriptor->cursor_is_unique(identity),
            "production callback rejected a recomputable RNG identity");
    auto wrong_stream_identity = identity;
    wrong_stream_identity.policy_rng_stream_id = "player1";
    require(!descriptor->cursor_is_unique(wrong_stream_identity),
            "production callback accepted an identity with mismatched stream");
    auto wrong_material_identity = identity;
    wrong_material_identity.initialization_material = trailing;
    require(!descriptor->cursor_is_unique(wrong_material_identity),
            "production callback accepted noncanonical identity material");

    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical();
    const std::array<ygo::trajectory::PolicyRole, 2> roles = {
        ygo::trajectory::PolicyRole::Behavior, ygo::trajectory::PolicyRole::Opponent};
    const auto valid_artifact = ygo::policy::make_random_legal_policy_artifact();
    const auto valid_assignments = ygo::policy::make_random_legal_participant_assignments(
        valid_artifact, config, ygo::environment::SeatAssignment::Normal, 0, roles);
    ygo::environment::EpisodeSpec spec;
    spec.starting_player = 0;

    auto none_artifact = valid_artifact;
    none_artifact.policy_rng_contract_identity = ygo::trajectory::kNoPolicyRngContractId;
    none_artifact.policy_artifact_id =
        ygo::trajectory::compute_policy_artifact_id(none_artifact);
    auto none_error = std::string();
    require(!resolver.validate(rebound_provenance_for(none_artifact, valid_assignments),
                               config, spec, &none_error),
            "RandomLegal artifact paired with NONE RNG was admitted");

    std::vector<ProvenanceRegistration> deterministic_entries;
    deterministic_entries.push_back(
        {ProvenanceKind::PolicyRngContract, ygo::trajectory::kNoPolicyRngContractId});
    deterministic_entries.push_back(
        {ProvenanceKind::ProducerImplementation, "ocgforge.policy.random_legal.v1"});
    deterministic_entries.push_back(
        {ProvenanceKind::InferenceAdapter, "ocgforge.policy.direct_execution.v1"});
    deterministic_entries.push_back(
        {ProvenanceKind::ObservationAdapter, "ocgforge.policy.public_observation.v1"});
    deterministic_entries.push_back(
        {ProvenanceKind::ActionAdapter, "ocgforge.policy.public_action_key.v1"});
    deterministic_entries.push_back(
        {ProvenanceKind::SamplingContract, "ocgforge.policy.deterministic_sampling.v1",
         ygo::trajectory::SamplingContractCapabilities{true, true}, std::nullopt});
    ProvenanceRegistration production_rng;
    production_rng.kind = ProvenanceKind::PolicyRngContract;
    production_rng.identity = "ocgforge.policy_rng.sha256_counter.v1";
    production_rng.policy_rng_descriptor = *descriptor;
    deterministic_entries.push_back(std::move(production_rng));
    const ygo::trajectory::ProvenanceResolver deterministic_resolver(
        std::move(deterministic_entries));
    auto deterministic_artifact = valid_artifact;
    deterministic_artifact.sampling_contract_identity =
        "ocgforge.policy.deterministic_sampling.v1";
    deterministic_artifact.policy_artifact_id =
        ygo::trajectory::compute_policy_artifact_id(deterministic_artifact);
    require(!deterministic_resolver.validate(
                rebound_provenance_for(deterministic_artifact, valid_assignments),
                config, spec),
            "RandomLegal artifact paired with deterministic sampling was admitted");
}

void test_random_legal_selects_only_from_the_supplied_domain() {
    const auto created = ygo::policy::create_random_legal_policy(rng_input());
    require(static_cast<bool>(created), "valid RandomLegal policy was rejected");
    auto policy = std::move(*created.value);
    const auto observation = public_observation();
    auto domain = candidates();
    const auto before = domain;
    std::vector<std::string> before_keys;
    before_keys.reserve(before.size());
    for (const auto& candidate : before) {
        before_keys.push_back(candidate.public_action_key);
    }
    const auto before_digest = ygo::environment::public_candidate_domain_digest(
        "yes_no", before_keys);

    const ygo::policy::PolicyInput input{observation, domain};
    const auto selection = policy.select(input);
    require(static_cast<bool>(selection), "RandomLegal selection failed for valid domain");
    require(selection.value->public_action_key == domain[1].public_action_key,
            "RandomLegal did not select the deterministic golden domain position");
    require(selection.value->rng_cursor.has_value() &&
                selection.value->rng_cursor->pre_cursor == 0 &&
                selection.value->rng_cursor->post_cursor == 1,
            "RandomLegal did not return the exact cursor transition");
    require(domain.size() == before.size(), "RandomLegal changed candidate count");
    for (std::size_t index = 0; index < domain.size(); ++index) {
        require(domain[index].public_action_key == before[index].public_action_key,
                "RandomLegal changed candidate ordering or membership");
    }
    std::vector<std::string> after_keys;
    after_keys.reserve(domain.size());
    for (const auto& candidate : domain) {
        after_keys.push_back(candidate.public_action_key);
    }
    const auto after_digest = ygo::environment::public_candidate_domain_digest(
        "yes_no", after_keys);
    require(after_digest == before_digest, "candidate-domain digest changed at policy boundary");

    std::vector<ygo::environment::EnvironmentActionCandidate> empty;
    const ygo::policy::PolicyInput empty_input{observation, empty};
    const auto empty_selection = policy.select(empty_input);
    require(!empty_selection && empty_selection.error.has_value() &&
                empty_selection.error->code == ygo::policy::PolicyErrorCode::EmptyCandidateDomain,
            "RandomLegal did not fail closed on an empty domain");

    auto invalid_domain = domain;
    invalid_domain[1].public_action_key = "not-a-public-action-key";
    const ygo::policy::PolicyInput invalid_input{observation, invalid_domain};
    const auto invalid_selection = policy.select(invalid_input);
    require(!invalid_selection && invalid_selection.error.has_value() &&
                invalid_selection.error->code == ygo::policy::PolicyErrorCode::InvalidCandidateDomain,
            "RandomLegal accepted an invalid public action key");
}

ygo::observation::PlayerObservation paired_private_observation(const std::string& marker) {
    ygo::observation::PlayerObservation observation;
    observation.perspective_player = 0;
    observation.decision_index = 7;
    observation.engine_step_index = marker == "private-a" ? 11 : 991;
    observation.globals.life_points = {8000, 8000};
    observation.match_context.perspective_player = 0;
    observation.match_context.opponent_deck.known = false;
    observation.decision_context.kind = "yes_no";
    observation.decision_context.player = 0;
    observation.decision_context.decision_id = marker;
    observation.decision_context.engine_step_index = observation.engine_step_index;
    observation.decision_context.engine_message_name = "private-message-" + marker;
    observation.observation_hash = "private-observation-hash-" + marker;
    return observation;
}

void test_paired_hidden_worlds_have_identical_random_legal_output() {
    const auto private_a = paired_private_observation("private-a");
    const auto private_b = paired_private_observation("private-b");
    require(ygo::observation::canonical_serialize(private_a) !=
                ygo::observation::canonical_serialize(private_b),
            "paired privacy worlds did not differ in hidden/private state");

    const auto public_a = ygo::environment::project_public_observation(private_a);
    const auto public_b = ygo::environment::project_public_observation(private_b);
    require(public_a.canonical_safe_state_bytes() == public_b.canonical_safe_state_bytes(),
            "paired hidden worlds produced different public-safe state bytes");
    require(ygo::environment::canonical_public_environment_observation_bytes(public_a) ==
                ygo::environment::canonical_public_environment_observation_bytes(public_b),
            "paired hidden worlds produced different public observations");
    require(ygo::environment::public_observation_digest(public_a) ==
                ygo::environment::public_observation_digest(public_b),
            "paired hidden worlds produced different public observation digests");

    auto policy_a_result = ygo::policy::create_random_legal_policy(rng_input());
    auto policy_b_result = ygo::policy::create_random_legal_policy(rng_input());
    require(static_cast<bool>(policy_a_result) && static_cast<bool>(policy_b_result),
            "paired privacy policies could not be constructed");
    auto policy_a = std::move(*policy_a_result.value);
    auto policy_b = std::move(*policy_b_result.value);
    auto candidates_a = candidates();
    auto candidates_b = candidates();
    const ygo::policy::PolicyInput input_a{public_a, candidates_a};
    const ygo::policy::PolicyInput input_b{public_b, candidates_b};
    const auto selection_a = policy_a.select(input_a);
    const auto selection_b = policy_b.select(input_b);
    require(static_cast<bool>(selection_a) && static_cast<bool>(selection_b),
            "paired privacy RandomLegal selection failed");
    require(selection_a.value->public_action_key == selection_b.value->public_action_key,
            "paired hidden worlds changed the selected public action key");
    require(selection_a.value->rng_cursor.has_value() && selection_b.value->rng_cursor.has_value() &&
                selection_a.value->rng_cursor->pre_cursor ==
                    selection_b.value->rng_cursor->pre_cursor &&
                selection_a.value->rng_cursor->post_cursor ==
                    selection_b.value->rng_cursor->post_cursor,
            "paired hidden worlds changed the policy RNG cursor trace");
    require(candidates_a.size() == candidates_b.size(),
            "paired hidden worlds changed the supplied candidate count");
    for (std::size_t index = 0; index < candidates_a.size(); ++index) {
        require(candidates_a[index].public_action_key == candidates_b[index].public_action_key,
                "paired hidden worlds changed candidate ordering or membership");
    }
    std::vector<std::string> keys_a;
    std::vector<std::string> keys_b;
    for (const auto& candidate : candidates_a) {
        keys_a.push_back(candidate.public_action_key);
    }
    for (const auto& candidate : candidates_b) {
        keys_b.push_back(candidate.public_action_key);
    }
    require(ygo::environment::public_candidate_domain_digest("yes_no", keys_a) ==
                ygo::environment::public_candidate_domain_digest("yes_no", keys_b),
            "paired hidden worlds changed the public candidate-domain digest");
}

int run() {
    test_random_legal_execution_binding_composition();
    test_production_registrations_and_composition();
    test_strict_rng_callbacks_and_incompatible_provenance();
    test_random_legal_selects_only_from_the_supplied_domain();
    test_paired_hidden_worlds_have_identical_random_legal_output();
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
