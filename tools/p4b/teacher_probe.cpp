#include "episodic_environment_test_access.hpp"

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/observed_card.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner.hpp"
#include "ygo/protocol/continuation.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/teacher_explanation_codec.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::protocol;
using namespace ygo::teacher;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string hex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto value : bytes) {
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

std::string optional_u64(const std::optional<std::uint64_t>& value) {
    return value.has_value() ? std::to_string(*value) : "none";
}

std::string optional_string(const std::optional<std::string>& value) {
    return value.has_value() ? *value : "none";
}

std::string ids(const std::vector<std::string>& values) {
    std::ostringstream output;
    output << values.size() << ':';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << values[index];
    }
    return output.str();
}

std::string facts(const std::vector<PublicFactValue>& values) {
    std::ostringstream output;
    output << values.size() << ':';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << hex(canonical_public_fact_value_bytes(values[index]));
    }
    return output.str();
}

std::string score(const std::optional<ScoreVector>& value) {
    if (!value.has_value()) {
        return "none";
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < value->values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << value->values[index];
    }
    return output.str();
}

std::string serialize_state(const EpisodeLocalStrategyStateV1& value) {
    std::ostringstream output;
    output << "strategy_profile_id=" << value.strategy_profile_id << '\n';
    output << "active_goal_id=" << optional_string(value.active_goal_id) << '\n';
    output << "active_line_id=" << optional_string(value.active_line_id) << '\n';
    output << "completed_line_node_ids=" << ids(value.completed_line_node_ids) << '\n';
    output << "achieved_goal_ids=" << ids(value.achieved_goal_ids) << '\n';
    output << "public_resource_facts=" << facts(value.public_resource_facts) << '\n';
    output << "public_restriction_facts=" << facts(value.public_restriction_facts) << '\n';
    output << "public_threat_facts=" << facts(value.public_threat_facts) << '\n';
    output << "last_accepted_decision_index="
           << optional_u64(value.last_accepted_decision_index) << '\n';
    output << "last_accepted_public_action_key="
           << optional_string(value.last_accepted_public_action_key) << '\n';
    return output.str();
}

std::string serialize_delta(const TeacherStateDeltaV1& value) {
    std::ostringstream output;
    output << "strategy_profile_id=" << value.strategy_profile_id << '\n';
    output << "base_last_accepted_decision_index="
           << optional_u64(value.base_last_accepted_decision_index) << '\n';
    output << "base_last_accepted_public_action_key="
           << optional_string(value.base_last_accepted_public_action_key) << '\n';
    output << "proposed_for_public_action_key=" << value.proposed_for_public_action_key << '\n';
    output << "active_goal_id=" << optional_string(value.active_goal_id) << '\n';
    output << "active_line_id=" << optional_string(value.active_line_id) << '\n';
    output << "completed_line_node_ids=" << ids(value.completed_line_node_ids) << '\n';
    output << "achieved_goal_ids=" << ids(value.achieved_goal_ids) << '\n';
    output << "public_resource_facts=" << facts(value.public_resource_facts) << '\n';
    output << "public_restriction_facts=" << facts(value.public_restriction_facts) << '\n';
    output << "public_threat_facts=" << facts(value.public_threat_facts) << '\n';
    output << "invalidation_reason_ids=" << ids(value.invalidation_reason_ids) << '\n';
    return output.str();
}

std::string serialize_evaluation(const CandidateEvaluation& value) {
    std::ostringstream output;
    output << "key=" << value.public_action_key << '\n';
    output << "status=" << static_cast<unsigned int>(value.status) << '\n';
    output << "score=" << score(value.score) << '\n';
    output << "matched_intent_ids=" << ids(value.matched_intent_ids) << '\n';
    output << "matched_goal_ids=" << ids(value.matched_goal_ids) << '\n';
    output << "matched_line_ids=" << ids(value.matched_line_ids) << '\n';
    output << "reason_ids=" << ids(value.reason_ids) << '\n';
    return output.str();
}

std::string serialize_ranking(const TeacherRankingResult& value) {
    std::ostringstream output;
    output << "status=" << static_cast<unsigned int>(value.status) << '\n';
    output << "selected_public_action_key="
           << optional_string(value.selected_public_action_key) << '\n';
    output << "selected_score_vector=" << score(value.selected_score_vector) << '\n';
    output << "fallback_level=";
    if (value.fallback_level.has_value()) {
        output << static_cast<unsigned int>(*value.fallback_level);
    } else {
        output << "none";
    }
    output << '\n';
    output << "evaluations=" << value.evaluations.size() << '\n';
    for (std::size_t index = 0; index < value.evaluations.size(); ++index) {
        output << "evaluation[" << index << "]\n" << serialize_evaluation(value.evaluations[index]);
    }
    if (value.explanation.has_value()) {
        output << "explanation=" << hex(canonical_teacher_decision_explanation_bytes(
                                           *value.explanation))
               << '\n';
    } else {
        output << "explanation=absent\n";
    }
    if (value.proposed_state_delta.has_value()) {
        output << "delta\n" << serialize_delta(*value.proposed_state_delta);
    } else {
        output << "delta=absent\n";
    }
    return output.str();
}

PublicFactValue u64_fact(const std::string& fact_id, const std::uint64_t value) {
    PublicFactValue fact;
    fact.fact_id = fact_id;
    fact.value_kind = PublicFactValueKind::U64;
    fact.u64_value = value;
    return fact;
}

EpisodeLocalStrategyStateV1 reset_state(const StrategyProfileV1& profile) {
    const auto result = reset_strategy_state(profile);
    require(result.has_value(), "probe profile reset failed");
    return *result;
}

PublicEnvironmentObservation public_observation(
    const std::uint8_t participant,
    const std::uint64_t decision_index,
    const std::uint32_t phase = 0x04,
    const std::string& decision_kind = "idle_command",
    const std::uint32_t chain_length = 0,
    const std::optional<std::uint32_t>& visible_passcode = std::nullopt,
    const std::string& visible_locator = "",
    const ygo::observation::SemanticZone visible_zone =
        ygo::observation::SemanticZone::MonsterZone) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = participant;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = participant;
    source.globals.turn_player = participant;
    source.globals.turn_count = 1;
    source.globals.phase = phase;
    source.globals.chain_length = chain_length;
    source.globals.terminal = false;
    source.match_context.perspective_player = participant;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = decision_kind;
    source.decision_context.player = participant;
    if (visible_passcode.has_value()) {
        ygo::observation::ObservedCard entity;
        entity.locator = {visible_locator.empty()
                              ? "p" + std::to_string(participant) + ":MONSTER_ZONE:0"
                              : visible_locator};
        entity.identity_known = true;
        entity.passcode = *visible_passcode;
        entity.owner = participant;
        entity.controller = participant;
        entity.zone = visible_zone;
        entity.sequence = 0;
        entity.face_up = true;
        entity.face_down = false;
        source.entities.push_back(std::move(entity));
    }
    return project_public_observation(source);
}

EnvironmentActionCandidate yes_no_candidate() {
    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::YesNo;
    candidate.choice = PublicChoice{PublicChoiceKind::YesNo, 0, std::nullopt};
    PublicActionKeyInput key;
    key.action_kind = "yes_no";
    key.choice = candidate.choice;
    candidate.public_action_key = public_action_key(key);
    return candidate;
}

EnvironmentActionCandidate idle_card_candidate(const std::string& locator,
                                                const std::uint32_t command) {
    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::IdleCommand;
    candidate.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, locator};
    candidate.phase = command;
    candidate.source_index = 0;
    PublicActionKeyInput key;
    key.action_kind = "idle_command";
    key.source_reference = candidate.source_reference;
    key.phase = candidate.phase;
    key.source_index = candidate.source_index;
    candidate.public_action_key = public_action_key(key);
    return candidate;
}

EnvironmentActionCandidate chain_candidate(const std::string& locator) {
    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::Chain;
    candidate.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, locator};
    PublicActionKeyInput key;
    key.action_kind = "chain";
    key.source_reference = candidate.source_reference;
    candidate.public_action_key = public_action_key(key);
    return candidate;
}

TeacherRankingResult run_core(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& state,
    const PublicEnvironmentObservation& observation,
    const std::vector<EnvironmentActionCandidate>& candidates) {
    TeacherCore core;
    return core.propose(PolicyInput{observation, candidates}, profile, state);
}

StrategyProfileV1 recovery_probe_profile(const bool recovery_matches_mo_ye) {
    auto profile = make_swordsoul_tenyi_profile();
    const auto edge = std::find_if(
        profile.recovery_edges.begin(), profile.recovery_edges.end(), [](const auto& value) {
            return value.recovery_edge_id == "recovery.interaction.main1";
        });
    require(edge != profile.recovery_edges.end(), "probe recovery edge is missing");
    edge->candidate_intent_ids = {recovery_matches_mo_ye ? "intent.mo_ye.starter"
                                                         : "intent.interaction.chain"};
    profile.profile_id = strategy_profile_id(profile);
    require(validate_strategy_profile(profile), "probe recovery profile is invalid");
    return profile;
}

struct PairedPrivateWorld final {
    DecisionRequest request;
    ygo::observation::PlayerObservation observation;
};

ygo::observation::PlayerObservation hidden_observation(
    const std::uint8_t perspective,
    const std::uint64_t engine_step_index) {
    ygo::observation::PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = perspective;
    observation.engine_step_index = engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.globals.terminal = false;
    observation.match_context.perspective_player = perspective;
    const auto hidden_controller = static_cast<std::uint8_t>(1 - perspective);
    const auto locator =
        std::string("p") + std::to_string(hidden_controller) + ":SPELL_TRAP_ZONE:0";
    observation.zones.push_back(
        {hidden_controller, ygo::observation::SemanticZone::SpellTrapZone, 1, 0, 1, false});
    ygo::observation::ObservedCard hidden;
    hidden.locator = {locator};
    hidden.identity_known = false;
    hidden.controller = hidden_controller;
    hidden.zone = ygo::observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    observation.entities.push_back(hidden);
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

DecisionRequest hidden_request(const std::uint32_t hidden_code) {
    DecisionRequest request;
    request.kind = ygo::protocol::DecisionRequestKind::CardSelection;
    request.decision_id = "private-decision.card." + std::to_string(hidden_code);
    request.engine_step_index = 91;
    request.player = 1;
    request.engine_message_type = 15;
    request.engine_message_name = "MSG_SELECT_CARD";
    request.raw_message_hash = "private-raw." + std::to_string(hidden_code);
    ygo::protocol::ActionCandidate candidate;
    candidate.action_kind = ygo::protocol::ActionKind::CardSelection;
    candidate.semantic_key = "card.0.3." + std::to_string(hidden_code) + ".0.8.0";
    candidate.source_card = hidden_code;
    candidate.source_controller = 0;
    candidate.source_location = 8;
    candidate.source_sequence = 0;
    candidate.source_index = 3;
    candidate.exact_response_bytes = {3, 0, 0, 0};
    request.candidates.push_back(std::move(candidate));
    return request;
}

PairedPrivateWorld private_world(const std::uint32_t hidden_code) {
    auto request = hidden_request(hidden_code);
    auto observation = hidden_observation(request.player, request.engine_step_index);
    ygo::observation::attach_decision_context(observation, request);
    return {std::move(request), std::move(observation)};
}

std::unique_ptr<EpisodicEnvironment> make_test_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "probe could not create canonical environment");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

std::string candidate_text(const EnvironmentActionCandidate& candidate) {
    std::ostringstream output;
    output << static_cast<unsigned int>(candidate.action_kind) << '|'
           << candidate.public_action_key << '|';
    if (candidate.choice.has_value()) {
        output << static_cast<unsigned int>(candidate.choice->kind) << ','
               << candidate.choice->value << ','
               << (candidate.choice->response_index.has_value()
                       ? std::to_string(*candidate.choice->response_index)
                       : "none");
    } else {
        output << "none";
    }
    output << '|';
    const auto reference_text = [&output](const auto& reference) {
        if (!reference.has_value()) {
            output << "none";
        } else {
            output << static_cast<unsigned int>(reference->kind) << ','
                   << reference->observation_locator;
        }
    };
    reference_text(candidate.source_reference);
    output << '|';
    reference_text(candidate.target_reference);
    output << '|' << (candidate.phase.has_value() ? std::to_string(*candidate.phase) : "none")
           << '|' << (candidate.position.has_value() ? std::to_string(*candidate.position)
                                                       : "none")
           << '|'
           << (candidate.source_index.has_value() ? std::to_string(*candidate.source_index)
                                                  : "none")
           << '|' << (candidate.amount.has_value() ? std::to_string(*candidate.amount) : "none")
           << '|' << candidate.continuation_operation << '|'
           << (candidate.submits_engine_response ? "1" : "0");
    return output.str();
}

std::string public_frame_text(const DecisionFrame& frame) {
    std::ostringstream output;
    output << hex(canonical_public_environment_observation_bytes(frame.public_observation))
           << '|'
           << static_cast<unsigned int>(frame.request.kind) << '|'
           << static_cast<unsigned int>(frame.request.player) << '|'
           << frame.request.candidates.size() << '\n';
    for (const auto& candidate : frame.request.candidates) {
        output << candidate_text(candidate) << '\n';
    }
    return output.str();
}

void require_equal_public_frame(const DecisionFrame& left, const DecisionFrame& right) {
    require(public_frame_text(left) == public_frame_text(right),
            "probe paired public frames differ");
    require(left.request.candidates.size() == right.request.candidates.size(),
            "probe paired candidate counts differ");
    for (std::size_t index = 0; index < left.request.candidates.size(); ++index) {
        require(left.request.candidates[index].source_reference.has_value() &&
                    right.request.candidates[index].source_reference.has_value() &&
                    left.request.candidates[index].source_reference->kind ==
                        PublicCardReferenceKind::RedactedSlot &&
                    right.request.candidates[index].source_reference->kind ==
                        PublicCardReferenceKind::RedactedSlot,
                "probe hidden candidate was not redacted");
    }
}

void test_paired_worlds() {
    auto environment = make_test_environment();
    const auto private_a = private_world(14821890);
    const auto private_b = private_world(7654321);
    require(private_a.request.candidates.front().semantic_key !=
                private_b.request.candidates.front().semantic_key,
            "probe private worlds did not differ");
    require(ygo::observation::canonical_serialize(private_a.observation) !=
                ygo::observation::canonical_serialize(private_b.observation),
            "probe private observations did not differ");
    const auto frame_a = ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, private_a.request, private_a.observation, std::string(64, 'a'), 7);
    const auto frame_b = ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, private_b.request, private_b.observation, std::string(64, 'a'), 7);
    require_equal_public_frame(frame_a, frame_b);
    const auto public_text = public_frame_text(frame_a);
    require(public_text.find("14821890") == std::string::npos &&
                public_text.find("7654321") == std::string::npos,
            "probe public frame contains a hidden passcode");

    for (const auto& profile : {make_swordsoul_tenyi_profile(),
                                make_salamangreat_profile()}) {
        const auto state = reset_state(profile);
        const auto left = run_core(profile, state, frame_a.public_observation,
                                   frame_a.request.candidates);
        const auto right = run_core(profile, state, frame_b.public_observation,
                                    frame_b.request.candidates);
        require(serialize_ranking(left) == serialize_ranking(right),
                "paired public worlds changed Teacher output");
        require(serialize_ranking(left).find("14821890") == std::string::npos &&
                    serialize_ranking(left).find("7654321") == std::string::npos,
                "Teacher output contains a hidden passcode");
    }
    std::cout << "paired_world=PASS\n";
    std::cout << "paired_teacher_result_sha256="
              << ygo::trace::sha256_string(serialize_ranking(
                     run_core(make_salamangreat_profile(), reset_state(
                                                           make_salamangreat_profile()),
                              frame_a.public_observation, frame_a.request.candidates)))
              << '\n';
}

struct CorpusCase final {
    std::string name;
    StrategyProfileV1 profile;
    EpisodeLocalStrategyStateV1 state;
    PublicEnvironmentObservation observation;
    std::vector<EnvironmentActionCandidate> candidates;
    TeacherFallbackLevel expected_level = TeacherFallbackLevel::F4;
};

std::vector<CorpusCase> make_corpus() {
    std::vector<CorpusCase> result;
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto mo_ye_observation = public_observation(
        0, 0, 0x04, "idle_command", 0, std::optional<std::uint32_t>{20001443},
        "p0:HAND:0", ygo::observation::SemanticZone::Hand);
    const auto mo_ye = idle_card_candidate("p0:HAND:0", 0);

    auto retained = reset_state(swordsoul);
    retained.active_goal_id = "goal.main1.swordsoul";
    retained.active_line_id = "line.main1.swordsoul";
    result.push_back({"swordsoul-retained-f0", swordsoul, retained, mo_ye_observation, {mo_ye},
                      TeacherFallbackLevel::F0});
    result.push_back({"swordsoul-public-replan-f1", swordsoul, reset_state(swordsoul),
                      mo_ye_observation, {mo_ye}, TeacherFallbackLevel::F1});
    result.push_back({"swordsoul-f4", swordsoul, reset_state(swordsoul),
                      public_observation(0, 0), {yes_no_candidate()},
                      TeacherFallbackLevel::F4});

    auto recovery_profile = recovery_probe_profile(false);
    auto recovery_state = reset_state(recovery_profile);
    recovery_state.active_goal_id = "goal.interaction.preservation";
    recovery_state.active_line_id = "line.interaction.preserve";
    recovery_state.public_resource_facts = {u64_fact("public.chain.length", 1)};
    const auto chain_observation = public_observation(
        0, 0, 0x04, "idle_command", 0, std::optional<std::uint32_t>{10045474});
    result.push_back({"swordsoul-recovery-f1", recovery_profile, recovery_state,
                      chain_observation, {chain_candidate("p0:MONSTER_ZONE:0")},
                      TeacherFallbackLevel::F1});

    const auto salamangreat = make_salamangreat_profile();
    result.push_back({"salamangreat-f4", salamangreat, reset_state(salamangreat),
                      public_observation(1, 0), {yes_no_candidate()},
                      TeacherFallbackLevel::F4});
    return result;
}

void test_determinism_corpus() {
    const auto corpus = make_corpus();
    std::cout << "determinism_corpus=PASS\n";
    std::cout << "case_count=" << corpus.size() << '\n';
    for (std::size_t index = 0; index < corpus.size(); ++index) {
        const auto& item = corpus[index];
        const auto ranking =
            run_core(item.profile, item.state, item.observation, item.candidates);
        require(ranking.status == TeacherRankingStatus::Selected &&
                    ranking.fallback_level == std::optional<TeacherFallbackLevel>{
                        item.expected_level} &&
                    ranking.proposed_state_delta.has_value(),
                "determinism corpus case did not produce its expected Teacher result");
        std::cout << "case[" << index << "].name=" << item.name << '\n';
        std::cout << "case[" << index << "].profile_id=" << item.profile.profile_id << '\n';
        std::cout << "case[" << index << "].ranking\n" << serialize_ranking(ranking);
    }
}

void test_knowledge_boundary() {
    const auto known_observation = public_observation(
        0, 0, 0x04, "idle_command", 0, std::optional<std::uint32_t>{20001443},
        "p0:HAND:0", ygo::observation::SemanticZone::Hand);
    const auto known_profile = make_swordsoul_tenyi_profile();
    const auto known_state = reset_state(known_profile);
    const auto known_candidate = idle_card_candidate("p0:HAND:0", 0);
    const auto known_ranking =
        run_core(known_profile, known_state, known_observation, {known_candidate});
    require(known_ranking.status == TeacherRankingStatus::Selected,
            "known public card situation was not evaluable");
    const auto known_state_text = serialize_state(known_state);
    const auto known_result_text = serialize_ranking(known_ranking);
    require(known_state_text.find("20001443") == std::string::npos &&
                known_state_text.find("p0:HAND:0") == std::string::npos &&
                known_result_text.find("20001443") == std::string::npos &&
                known_result_text.find("p0:HAND:0") == std::string::npos,
            "Teacher state or evidence retained a card identity/locator");

    auto environment = make_test_environment();
    const auto private_a = private_world(14821890);
    const auto private_b = private_world(7654321);
    const auto frame_a = ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, private_a.request, private_a.observation, std::string(64, 'b'), 7);
    const auto frame_b = ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, private_b.request, private_b.observation, std::string(64, 'b'), 7);
    require_equal_public_frame(frame_a, frame_b);
    const auto profile = make_salamangreat_profile();
    const auto state = reset_state(profile);
    const auto result_a = run_core(profile, state, frame_a.public_observation,
                                   frame_a.request.candidates);
    const auto result_b = run_core(profile, state, frame_b.public_observation,
                                   frame_b.request.candidates);
    require(result_a.fallback_level == std::optional<TeacherFallbackLevel>{
                TeacherFallbackLevel::F4} &&
                serialize_ranking(result_a) == serialize_ranking(result_b),
            "post-knowledge-destruction Teacher outputs differ");
    const auto result_text = serialize_ranking(result_a);
    require(result_text.find("14821890") == std::string::npos &&
                result_text.find("7654321") == std::string::npos,
            "post-knowledge-destruction evidence contains hidden identity");
    std::cout << "knowledge_boundary=PASS\n";
    std::cout << "knowledge_boundary_sha256="
              << ygo::trace::sha256_string(result_text) << '\n';
}

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(assignments.begin(), assignments.end(),
                                 [player](const auto& assignment) {
                                     return assignment.player == player;
                                 });
    require(it != assignments.end(), "probe assignment is missing a player");
    return *it;
}

void test_profile_registry() {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_again = make_swordsoul_tenyi_profile();
    require(canonical_strategy_profile_content_bytes(swordsoul) ==
                canonical_strategy_profile_content_bytes(swordsoul_again) &&
                strategy_profile_id(swordsoul) == swordsoul_again.profile_id,
            "Swordsoul profile identity is not deterministic");
    const auto swordsoul_binding = make_teacher_policy_binding(swordsoul);
    const auto salamangreat_binding = make_teacher_policy_binding(salamangreat);
    require(swordsoul_binding.teacher_policy_binding_id ==
                make_teacher_policy_binding(swordsoul_again).teacher_policy_binding_id,
            "Swordsoul binding identity is not deterministic");
    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    require(swordsoul_artifact.policy_artifact_id ==
                make_teacher_policy_artifact(swordsoul_again).policy_artifact_id,
            "Swordsoul artifact identity is not deterministic");

    const auto config = CertifiedEnvironmentConfig::canonical();
    std::string diagnostic;
    require(validate_strategy_profile_binding(swordsoul, config, &diagnostic) &&
                validate_strategy_profile_binding(salamangreat, config, &diagnostic),
            "published profile binding validation failed: " + diagnostic);
    const auto resolver = make_production_policy_provenance_resolver();
    require(resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                 swordsoul_binding.teacher_policy_binding_id) &&
                resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                     salamangreat_binding.teacher_policy_binding_id),
            "published Teacher binding is not registered");

    auto changed = swordsoul;
    changed.preferences.back().value++;
    changed.profile_id = strategy_profile_id(changed);
    require(changed.profile_id != swordsoul.profile_id,
            "semantic profile change did not change profile identity");
    const auto changed_binding = make_teacher_policy_binding(changed);
    const auto changed_artifact = make_teacher_policy_artifact(changed);
    require(changed_binding.teacher_policy_binding_id !=
                swordsoul_binding.teacher_policy_binding_id &&
                changed_artifact.policy_artifact_id != swordsoul_artifact.policy_artifact_id,
            "semantic profile change did not change binding/artifact identity");

    auto forced_old_id = changed;
    forced_old_id.profile_id = swordsoul.profile_id;
    require(!validate_strategy_profile(forced_old_id),
            "changed profile forced under old ID passed validation");
    require(!resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                  changed_binding.teacher_policy_binding_id),
            "unpublished binding unexpectedly resolved");

    auto forged_core = swordsoul_binding;
    forged_core.teacher_core_artifact_identity = "ocgforge.policy.other_teacher.v1";
    forged_core.teacher_policy_binding_id = teacher_policy_binding_id(forged_core);
    require(validate_teacher_policy_binding(forged_core, swordsoul),
            "changed TeacherCore identity could not form an internal binding");
    require(!resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                  forged_core.teacher_policy_binding_id),
            "changed TeacherCore identity aliased a published binding");

    const std::array<PolicyRole, 2> roles = {PolicyRole::Behavior, PolicyRole::Opponent};
    const auto assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config, SeatAssignment::Normal, 0, roles);
    auto unpublished_assignment = assignment_for_player(assignments, 0);
    unpublished_assignment.policy_artifact_id = changed_artifact.policy_artifact_id;
    unpublished_assignment.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(unpublished_assignment);
    require(!create_teacher_policy_session(changed, changed_binding, changed_artifact,
                                           unpublished_assignment),
            "unpublished profile entered a production session");

    std::cout << "profile_registry=PASS\n";
    std::cout << "swordsoul_profile_id=" << swordsoul.profile_id << '\n';
    std::cout << "salamangreat_profile_id=" << salamangreat.profile_id << '\n';
    std::cout << "swordsoul_binding_id=" << swordsoul_binding.teacher_policy_binding_id << '\n';
    std::cout << "salamangreat_binding_id=" << salamangreat_binding.teacher_policy_binding_id
              << '\n';
    std::cout << "swordsoul_policy_artifact_id=" << swordsoul_artifact.policy_artifact_id << '\n';
    std::cout << "salamangreat_policy_artifact_id=" << salamangreat_artifact.policy_artifact_id
              << '\n';
}

TeacherRunnerConfig make_runner_config(const SeatAssignment seat_assignment,
                                        const std::uint8_t starting_player) {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    TeacherRunnerConfig config;
    config.environment_config = CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = 2;
    config.episode_spec.seat_assignment = seat_assignment;
    config.episode_spec.starting_player = starting_player;
    config.run_control.engine_process_budget = 512;
    config.run_control.semantic_action_budget = 1;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "phase4b-acceptance";
    const std::array<PolicyRole, 2> roles = {PolicyRole::Behavior, PolicyRole::Opponent};
    const auto assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config.environment_config,
        seat_assignment, starting_player, roles);
    config.policy_provenance.policy_artifacts = {swordsoul_artifact, salamangreat_artifact};
    std::sort(config.policy_provenance.policy_artifacts.begin(),
              config.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    config.policy_provenance.participant_assignments = assignments;
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(assignments, player);
        const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        auto session = create_teacher_policy_session(
            profile, make_teacher_policy_binding(profile), artifact, assignment);
        require(static_cast<bool>(session), "probe could not create matrix Teacher session");
        config.sessions[player] = std::move(*session.value);
    }
    return config;
}

void test_matrix() {
    for (const auto seat_assignment : {SeatAssignment::Normal, SeatAssignment::Mirror}) {
        for (const auto starting_player : {std::uint8_t{0}, std::uint8_t{1}}) {
            auto created = TeacherRunner::create(
                make_runner_config(seat_assignment, starting_player));
            require(static_cast<bool>(created), "probe matrix runner construction failed");
            const auto result = created.value->run();
            require(result.disposition == PolicyRunnerDisposition::CleanAdmitted &&
                        result.envelope.has_value() && result.candidate_shard.has_value() &&
                        result.admission_verification.has_value() &&
                        result.admission_receipt.has_value() &&
                        result.dataset_manifest.has_value(),
                    "probe matrix row did not complete the trusted Teacher path");
            require(!result.envelope->records.empty(), "probe matrix row has no record");
            for (const auto& record : result.envelope->records) {
                require(record.policy_rng_decision_provenance.mode == PolicyRngMode::None &&
                            !record.policy_rng_decision_provenance.pre_cursor.has_value() &&
                            !record.policy_rng_decision_provenance.post_cursor.has_value() &&
                            !record.policy_rng_decision_provenance.pre_state.has_value() &&
                            !record.policy_rng_decision_provenance.post_state.has_value(),
                        "probe matrix Teacher record has policy RNG state");
            }
            const auto seat_name = seat_assignment == SeatAssignment::Normal ? "normal" : "mirror";
            std::cout << "matrix_row=" << seat_name << ':' << static_cast<unsigned int>(
                starting_player) << ":PASS\n";
        }
    }
    std::cout << "matrix=PASS\n";
}

int run_mode(const std::string& mode) {
    if (mode == "--paired-world") {
        test_paired_worlds();
        return 0;
    }
    if (mode == "--determinism-corpus") {
        test_determinism_corpus();
        return 0;
    }
    if (mode == "--knowledge-boundary") {
        test_knowledge_boundary();
        return 0;
    }
    if (mode == "--profile-registry") {
        test_profile_registry();
        return 0;
    }
    if (mode == "--matrix") {
        test_matrix();
        return 0;
    }
    throw std::invalid_argument("unknown Teacher probe mode");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Teacher probe requires exactly one mode");
        return run_mode(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "teacher_probe: FAIL: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "teacher_probe: FAIL: unknown error\n";
        return 1;
    }
}
