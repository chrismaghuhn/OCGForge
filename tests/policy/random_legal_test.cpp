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

int run() {
    test_production_registrations_and_composition();
    test_strict_rng_callbacks_and_incompatible_provenance();
    test_random_legal_selects_only_from_the_supplied_domain();
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
