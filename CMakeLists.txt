cmake_minimum_required(VERSION 3.23)

project(OCGForge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(cmake/RulesBundle.cmake)

set(M0_DERIVED_DIR "${CMAKE_SOURCE_DIR}/.cache/derived")
set(M0_CARD_DATA_TSV "${M0_DERIVED_DIR}/fixture_card_data.tsv")
add_custom_command(
    OUTPUT "${M0_CARD_DATA_TSV}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_card_data.py"
            --database "${M0_BABELCDB_DIR}/cards.cdb"
            --output "${M0_CARD_DATA_TSV}"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_a.deck"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_b.deck"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/m1_engine/cards.deck"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/m2_cards.deck"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
            --code 20001444
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/prepare_card_data.py"
        "${M0_BABELCDB_DIR}/cards.cdb"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_a.deck"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_b.deck"
        "${CMAKE_SOURCE_DIR}/fixtures/m1_engine/cards.deck"
        "${CMAKE_SOURCE_DIR}/fixtures/m2_cards.deck"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    VERBATIM
)
add_custom_target(m0_fixture_data DEPENDS "${M0_CARD_DATA_TSV}")

set(OCG_CORE_CPP
    "${M0_CORE_DIR}/card.cpp"
    "${M0_CORE_DIR}/duel.cpp"
    "${M0_CORE_DIR}/effect.cpp"
    "${M0_CORE_DIR}/field.cpp"
    "${M0_CORE_DIR}/interpreter.cpp"
    "${M0_CORE_DIR}/libcard.cpp"
    "${M0_CORE_DIR}/libdebug.cpp"
    "${M0_CORE_DIR}/libduel.cpp"
    "${M0_CORE_DIR}/libeffect.cpp"
    "${M0_CORE_DIR}/libgroup.cpp"
    "${M0_CORE_DIR}/ocgapi.cpp"
    "${M0_CORE_DIR}/operations.cpp"
    "${M0_CORE_DIR}/playerop.cpp"
    "${M0_CORE_DIR}/processor.cpp"
    "${M0_CORE_DIR}/processor_visit.cpp"
    "${M0_CORE_DIR}/scriptlib.cpp"
)
set(OCG_CORE_LUA_C
    "${M0_CORE_DIR}/lua/src/lapi.c"
    "${M0_CORE_DIR}/lua/src/lauxlib.c"
    "${M0_CORE_DIR}/lua/src/lbaselib.c"
    "${M0_CORE_DIR}/lua/src/lcode.c"
    "${M0_CORE_DIR}/lua/src/lctype.c"
    "${M0_CORE_DIR}/lua/src/ldebug.c"
    "${M0_CORE_DIR}/lua/src/ldo.c"
    "${M0_CORE_DIR}/lua/src/ldump.c"
    "${M0_CORE_DIR}/lua/src/lfunc.c"
    "${M0_CORE_DIR}/lua/src/lgc.c"
    "${M0_CORE_DIR}/lua/src/liolib.c"
    "${M0_CORE_DIR}/lua/src/llex.c"
    "${M0_CORE_DIR}/lua/src/lmathlib.c"
    "${M0_CORE_DIR}/lua/src/lmem.c"
    "${M0_CORE_DIR}/lua/src/lobject.c"
    "${M0_CORE_DIR}/lua/src/lopcodes.c"
    "${M0_CORE_DIR}/lua/src/lparser.c"
    "${M0_CORE_DIR}/lua/src/lstate.c"
    "${M0_CORE_DIR}/lua/src/lstring.c"
    "${M0_CORE_DIR}/lua/src/lstrlib.c"
    "${M0_CORE_DIR}/lua/src/ltable.c"
    "${M0_CORE_DIR}/lua/src/ltablib.c"
    "${M0_CORE_DIR}/lua/src/ltm.c"
    "${M0_CORE_DIR}/lua/src/lundump.c"
    "${M0_CORE_DIR}/lua/src/lvm.c"
    "${M0_CORE_DIR}/lua/src/lzio.c"
)
set_source_files_properties(${OCG_CORE_LUA_C} PROPERTIES LANGUAGE CXX)

add_library(ocgcore STATIC ${OCG_CORE_CPP} ${OCG_CORE_LUA_C})
target_include_directories(ocgcore
    PUBLIC
        "${M0_CORE_DIR}"
        "${M0_CORE_DIR}/lua"
        "${M0_CORE_DIR}/lua/src"
)
if(MSVC)
    target_compile_options(ocgcore PRIVATE "/FI${M0_CORE_DIR}/lua/luaconf-customize.h")
    target_compile_options(ocgcore PRIVATE /W3)
else()
    target_compile_options(ocgcore PRIVATE "-include${M0_CORE_DIR}/lua/luaconf-customize.h")
    target_compile_options(ocgcore PRIVATE -Wno-unused-parameter -pedantic)
endif()

add_library(ygo_m0 STATIC
    src/core/card_data_reader.cpp
    src/core/core_host.cpp
    src/core/rules_bundle.cpp
    src/core/script_reader.cpp
    src/m3/conformance_policy.cpp
    src/protocol/continuation.cpp
    src/protocol/message_decoder.cpp
    src/protocol/response_builder.cpp
    src/trace/engine_trace.cpp
    src/trace/sha256.cpp
    src/observation/query_decoder.cpp
    src/observation/direct_canonical_writer.cpp
    src/observation/serialization.cpp
    src/observation/card_projection.cpp
    src/observation/observation_builder.cpp
    src/observation/observation_session.cpp
    src/observation/event_projection.cpp
    src/observation/decision_integration.cpp
    src/observation/zone_projection.cpp
    src/view/player_view.cpp
)
add_dependencies(ygo_m0 m0_fixture_data)
target_include_directories(ygo_m0
    PUBLIC "${CMAKE_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/core"
)
target_link_libraries(ygo_m0 PUBLIC ocgcore)
target_compile_definitions(ygo_m0 PUBLIC
    YGO_M0_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YGO_M0_CARD_DATA_TSV="${M0_CARD_DATA_TSV}"
    YGO_M0_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    YGO_M0_BUILD_TYPE="$<CONFIG>"
    YGO_M3_FORMAT_ID="${M0_RULES_FORMAT_ID}"
    YGO_M3_DUEL_MODE_NAME="${M0_RULES_DUEL_MODE}"
    YGO_M3_DUEL_FLAGS_VALUE=${M0_RULES_DUEL_FLAGS}
    YGO_M3_RULES_BUNDLE_ID="${M0_RULES_BUNDLE_ID}"
    YGO_M3_CORE_PATCHSET_ID="${M0_RULES_CORE_PATCHSET_ID}"
    YGO_M3_CORE_PATCHSET_SHA256="${M0_RULES_CORE_PATCHSET_SHA256}"
    YGO_M0_CARDSCRIPTS_ROOT="${M0_CARDSCRIPTS_DIR}"
    YGO_M0_BABELCDB_ROOT="${M0_BABELCDB_DIR}"
    YGO_M0_CORE_API_VERSION="${M0_CORE_API_VERSION}"
    YGO_M0_CORE_COMMIT="${M0_CORE_COMMIT}"
    YGO_M0_CORE_RESOLVED_CHECKOUT_SHA256="${M0_CORE_RESOLVED_CHECKOUT_SHA256}"
    YGO_M0_CARDSCRIPTS_COMMIT="${M0_CARDSCRIPTS_COMMIT}"
    YGO_M0_CARDSCRIPTS_RESOLVED_CHECKOUT_SHA256="${M0_CARDSCRIPTS_RESOLVED_CHECKOUT_SHA256}"
    YGO_M0_DATABASE_COMMIT="${M0_DATABASE_COMMIT}"
    YGO_M0_DATABASE_RESOLVED_CHECKOUT_SHA256="${M0_DATABASE_RESOLVED_CHECKOUT_SHA256}"
    YGO_M0_DATABASE_ARTIFACT_SHA256="${M0_DATABASE_ARTIFACT_SHA256}"
)
option(YGO_M4_DIRECT_CANONICAL_WRITER
       "Use the direct canonical serializer writer"
       ON)
if(YGO_M4_DIRECT_CANONICAL_WRITER)
    target_compile_definitions(ygo_m0 PRIVATE YGO_M4_DIRECT_CANONICAL_WRITER)
endif()
if(MSVC)
    target_compile_options(ygo_m0 PRIVATE /W4 /permissive-)
else()
    target_compile_options(ygo_m0 PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(ygo_m4 STATIC
    src/environment/episode_driver.cpp
    src/environment/episodic_environment.cpp
    src/environment/identity_contract.cpp
    src/environment/candidate_domain_evidence.cpp
    src/environment/public_action_identity.cpp
    src/environment/public_safe_state.cpp
    src/environment/public_environment_observation.cpp
    src/environment/public_environment_observation_decoder.cpp
    src/simulation/canonical_simulation.cpp
)
target_include_directories(ygo_m4 PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(ygo_m4 PUBLIC ygo_m0)
if(MSVC)
    target_compile_options(ygo_m4 PRIVATE /W4 /permissive-)
else()
    target_compile_options(ygo_m4 PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(ygo_trajectory STATIC
    src/trajectory/codec.cpp
    src/trajectory/policy_provenance.cpp
    src/trajectory/identity_resolver.cpp
    src/trajectory/recorder.cpp
    src/trajectory/shard.cpp
    src/trajectory/restricted_evidence.cpp
    src/trajectory/storage.cpp
    src/trajectory/admission.cpp
    src/trajectory/receipt.cpp
    src/trajectory/dataset_manifest.cpp
)
target_include_directories(ygo_trajectory PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(ygo_trajectory PUBLIC ygo_m4)
if(MSVC)
    target_compile_options(ygo_trajectory PRIVATE /W4 /permissive-)
else()
    target_compile_options(ygo_trajectory PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(ygo_teacher STATIC
    src/teacher/candidate_evaluator.cpp
    src/teacher/candidate_features.cpp
    src/teacher/deterministic_resolver.cpp
    src/teacher/fallback_resolver.cpp
    src/teacher/interaction_evaluator.cpp
    src/teacher/material_evaluator.cpp
    src/teacher/goal_line_controller.cpp
    src/teacher/predicate_registry.cpp
    src/teacher/public_fact_registry.cpp
    src/teacher/recovery_controller.cpp
    src/teacher/salamangreat_profile.cpp
    src/teacher/strategy_profile.cpp
    src/teacher/strategy_profile_codec.cpp
    src/teacher/strategy_state.cpp
    src/teacher/strategy_state_reconciler.cpp
    src/teacher/target_evaluator.cpp
    src/teacher/tactical_evaluator.cpp
    src/teacher/swordsoul_tenyi_profile.cpp
    src/teacher/teacher_core.cpp
    src/teacher/teacher_explanation_codec.cpp
    src/teacher/teacher_policy_binding.cpp
    src/teacher/teacher_decision.cpp
)
target_include_directories(ygo_teacher PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(ygo_teacher PUBLIC ygo_trajectory)
if(MSVC)
    target_compile_options(ygo_teacher PRIVATE /W4 /permissive-)
else()
    target_compile_options(ygo_teacher PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(ygo_policy STATIC
    src/policy/rng.cpp
    src/policy/production_provenance.cpp
    src/policy/production.cpp
    src/policy/random_legal.cpp
    src/policy/runner.cpp
    src/policy/runner_shared.cpp
    src/policy/teacher.cpp
    src/policy/teacher_runner.cpp
)
target_include_directories(ygo_policy PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(ygo_policy PUBLIC ygo_teacher)
if(MSVC)
    target_compile_options(ygo_policy PRIVATE /W4 /permissive-)
else()
    target_compile_options(ygo_policy PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(trajectory_codec_test tests/trajectory/codec_test.cpp)
target_link_libraries(trajectory_codec_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_codec_test COMMAND trajectory_codec_test)

add_executable(trajectory_recorder_test tests/trajectory/recorder_test.cpp)
target_link_libraries(trajectory_recorder_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_recorder_test COMMAND trajectory_recorder_test)

add_executable(trajectory_shard_test tests/trajectory/shard_test.cpp)
target_link_libraries(trajectory_shard_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_shard_test COMMAND trajectory_shard_test)

add_executable(trajectory_restricted_evidence_test tests/trajectory/restricted_evidence_test.cpp)
target_link_libraries(trajectory_restricted_evidence_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_restricted_evidence_test COMMAND trajectory_restricted_evidence_test)

add_executable(trajectory_replay_admission_test tests/trajectory/replay_admission_test.cpp)
target_include_directories(trajectory_replay_admission_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/src/environment")
target_link_libraries(trajectory_replay_admission_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_replay_admission_test COMMAND trajectory_replay_admission_test)
set_tests_properties(trajectory_replay_admission_test PROPERTIES
    LABELS "P4A_HEAVY_REPLAY"
)

add_executable(trajectory_receipt_test tests/trajectory/receipt_test.cpp)
target_link_libraries(trajectory_receipt_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_receipt_test COMMAND trajectory_receipt_test)

add_executable(trajectory_dataset_manifest_test tests/trajectory/dataset_manifest_test.cpp)
target_link_libraries(trajectory_dataset_manifest_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_dataset_manifest_test COMMAND trajectory_dataset_manifest_test)

add_executable(trajectory_artifact_determinism_test
               tests/trajectory/artifact_determinism_test.cpp)
target_include_directories(trajectory_artifact_determinism_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/tests/trajectory")
target_link_libraries(trajectory_artifact_determinism_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_artifact_determinism_test COMMAND trajectory_artifact_determinism_test)

add_executable(trajectory_privacy_test tests/trajectory/privacy_test.cpp)
target_include_directories(trajectory_privacy_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/tests/trajectory"
                           "${CMAKE_SOURCE_DIR}/src/environment")
target_link_libraries(trajectory_privacy_test PRIVATE ygo_trajectory)
add_test(NAME trajectory_privacy_test COMMAND trajectory_privacy_test)

add_executable(strategy_profile_codec_test tests/teacher/strategy_profile_codec_test.cpp)
target_link_libraries(strategy_profile_codec_test PRIVATE ygo_teacher)
add_test(NAME strategy_profile_codec_test COMMAND strategy_profile_codec_test)

add_executable(strategy_profile_negative_test tests/teacher/strategy_profile_negative_test.cpp)
target_link_libraries(strategy_profile_negative_test PRIVATE ygo_teacher)
add_test(NAME strategy_profile_negative_test COMMAND strategy_profile_negative_test)

add_executable(teacher_policy_boundary_compile_test
               tests/teacher/teacher_policy_boundary_compile_test.cpp)
target_include_directories(teacher_policy_boundary_compile_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/include")
add_test(NAME teacher_policy_boundary_compile_test
         COMMAND teacher_policy_boundary_compile_test)

add_executable(teacher_decision_dto_test tests/teacher/teacher_decision_dto_test.cpp)
target_link_libraries(teacher_decision_dto_test PRIVATE ygo_teacher)
add_test(NAME teacher_decision_dto_test COMMAND teacher_decision_dto_test)

add_executable(teacher_domain_preservation_test
               tests/teacher/teacher_domain_preservation_test.cpp)
target_link_libraries(teacher_domain_preservation_test PRIVATE ygo_teacher)
add_test(NAME teacher_domain_preservation_test COMMAND teacher_domain_preservation_test)

add_executable(teacher_ranking_test tests/teacher/teacher_ranking_test.cpp)
target_link_libraries(teacher_ranking_test PRIVATE ygo_teacher)
add_test(NAME teacher_ranking_test COMMAND teacher_ranking_test)

add_executable(teacher_evaluator_test tests/teacher/teacher_evaluator_test.cpp)
target_link_libraries(teacher_evaluator_test PRIVATE ygo_teacher)
add_test(NAME teacher_evaluator_test COMMAND teacher_evaluator_test)

add_executable(teacher_strategy_state_test tests/teacher/teacher_strategy_state_test.cpp)
target_link_libraries(teacher_strategy_state_test PRIVATE ygo_teacher)
add_test(NAME teacher_strategy_state_test COMMAND teacher_strategy_state_test)

add_executable(teacher_rejected_transition_test
               tests/teacher/teacher_rejected_transition_test.cpp)
target_link_libraries(teacher_rejected_transition_test PRIVATE ygo_teacher)
add_test(NAME teacher_rejected_transition_test
         COMMAND teacher_rejected_transition_test)

add_executable(teacher_goal_line_test tests/teacher/teacher_goal_line_test.cpp)
target_link_libraries(teacher_goal_line_test PRIVATE ygo_teacher)
add_test(NAME teacher_goal_line_test COMMAND teacher_goal_line_test)

add_executable(teacher_recovery_test tests/teacher/teacher_recovery_test.cpp)
target_link_libraries(teacher_recovery_test PRIVATE ygo_teacher)
add_test(NAME teacher_recovery_test COMMAND teacher_recovery_test)

add_executable(teacher_fallback_test tests/teacher/teacher_fallback_test.cpp)
target_link_libraries(teacher_fallback_test PRIVATE ygo_teacher)
add_test(NAME teacher_fallback_test COMMAND teacher_fallback_test)

add_executable(teacher_explanation_test tests/teacher/teacher_explanation_test.cpp)
target_link_libraries(teacher_explanation_test PRIVATE ygo_teacher)
add_test(NAME teacher_explanation_test COMMAND teacher_explanation_test)

add_executable(swordsoul_profile_test tests/teacher/swordsoul_profile_test.cpp)
target_link_libraries(swordsoul_profile_test PRIVATE ygo_teacher)
add_test(NAME swordsoul_profile_test COMMAND swordsoul_profile_test)

add_executable(swordsoul_teacher_scenarios_test
               tests/teacher/swordsoul_teacher_scenarios_test.cpp)
target_link_libraries(swordsoul_teacher_scenarios_test PRIVATE ygo_teacher)
add_test(NAME swordsoul_teacher_scenarios_test
         COMMAND swordsoul_teacher_scenarios_test)

add_executable(salamangreat_profile_test tests/teacher/salamangreat_profile_test.cpp)
target_link_libraries(salamangreat_profile_test PRIVATE ygo_teacher)
target_compile_definitions(salamangreat_profile_test PRIVATE
                           YGO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME salamangreat_profile_test COMMAND salamangreat_profile_test)

add_executable(salamangreat_teacher_scenarios_test
               tests/teacher/salamangreat_teacher_scenarios_test.cpp)
target_link_libraries(salamangreat_teacher_scenarios_test PRIVATE ygo_teacher)
add_test(NAME salamangreat_teacher_scenarios_test
         COMMAND salamangreat_teacher_scenarios_test)

add_executable(teacher_provenance_test tests/teacher/teacher_provenance_test.cpp)
target_link_libraries(teacher_provenance_test PRIVATE ygo_policy)
add_test(NAME teacher_provenance_test COMMAND teacher_provenance_test)

add_executable(teacher_runner_trajectory_test
               tests/teacher/teacher_runner_trajectory_test.cpp)
target_link_libraries(teacher_runner_trajectory_test PRIVATE ygo_policy)
add_test(NAME teacher_runner_trajectory_test COMMAND teacher_runner_trajectory_test)

add_executable(teacher_probe tools/p4b/teacher_probe.cpp)
target_include_directories(teacher_probe PRIVATE
                           "${CMAKE_SOURCE_DIR}/src/environment")
target_link_libraries(teacher_probe PRIVATE ygo_policy)
add_test(NAME teacher_knowledge_boundary_test
         COMMAND teacher_probe --knowledge-boundary)
add_test(NAME teacher_profile_registry_test
         COMMAND teacher_probe --profile-registry)

add_executable(ygo_core_probe tools/ygo_core_probe/main.cpp)
target_link_libraries(ygo_core_probe PRIVATE ygo_m4)
target_compile_definitions(ygo_core_probe PRIVATE
    YGO_M0_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M0_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M0_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)

add_executable(ygo_episodic_probe tools/ygo_episodic_probe/main.cpp)
target_link_libraries(ygo_episodic_probe PRIVATE ygo_m4)
add_test(NAME ygo_episodic_probe_smoke
         COMMAND ygo_episodic_probe --seed 2 --max-actions 2)

add_executable(ygo_episodic_reset_probe tools/ygo_episodic_reset_probe/main.cpp)
target_link_libraries(ygo_episodic_reset_probe PRIVATE ygo_m4)
target_include_directories(ygo_episodic_reset_probe PRIVATE
                           "${CMAKE_SOURCE_DIR}/src/environment")

add_executable(ygo_episodic_internal_witness tools/ygo_episodic_internal_witness/main.cpp)
target_link_libraries(ygo_episodic_internal_witness PRIVATE ygo_m4)

add_executable(ygo_m4_worker
    tools/ygo_m4_worker/json_protocol.cpp
    tools/ygo_m4_worker/main.cpp
)
target_link_libraries(ygo_m4_worker PRIVATE ygo_m4)
target_compile_definitions(ygo_m4_worker PRIVATE
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
if(MSVC)
    target_compile_options(ygo_m4_worker PRIVATE /W4 /permissive-)
else()
    target_compile_options(ygo_m4_worker PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ygo_observation_probe tools/ygo_observation_probe/main.cpp)
target_link_libraries(ygo_observation_probe PRIVATE ygo_m0)
target_compile_definitions(ygo_observation_probe PRIVATE
    YGO_M2_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M2_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M2_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M2_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m2_setup.lua"
)

add_executable(sha256_benchmark tools/ygo_sha256_benchmark/main.cpp)
target_link_libraries(sha256_benchmark PRIVATE ygo_m0)
if(MSVC)
    target_compile_options(sha256_benchmark PRIVATE /W4 /permissive-)
else()
    target_compile_options(sha256_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(m4_simulation_contract_test tests/m4/simulation_contract_test.cpp)
target_include_directories(m4_simulation_contract_test PRIVATE "${CMAKE_SOURCE_DIR}/include")
add_test(NAME m4_simulation_contract_test COMMAND m4_simulation_contract_test)

add_executable(episode_driver_seam_test tests/episodic/episode_driver_seam_test.cpp)
target_link_libraries(episode_driver_seam_test PRIVATE ygo_m4)
add_test(NAME episode_driver_seam_test COMMAND episode_driver_seam_test)

add_test(NAME episode_driver_ownership_guard
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/episodic/episode_driver_ownership_guard.py")

add_executable(episode_driver_stale_key_test tests/episodic/episode_driver_stale_key_test.cpp)
target_link_libraries(episode_driver_stale_key_test PRIVATE ygo_m4)
target_compile_definitions(episode_driver_stale_key_test PRIVATE
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME episode_driver_stale_key_test COMMAND episode_driver_stale_key_test)

add_executable(episode_driver_tribute_integration_test
    tests/episodic/episode_driver_tribute_integration_test.cpp
)
target_link_libraries(episode_driver_tribute_integration_test PRIVATE ygo_m4)
target_compile_definitions(episode_driver_tribute_integration_test PRIVATE
    YGO_M1_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_a.deck"
    YGO_M1_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_b.deck"
    YGO_M1_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME episode_driver_tribute_integration_test COMMAND episode_driver_tribute_integration_test)

add_executable(normative_prerequisites_test tests/episodic/normative_prerequisites_test.cpp)
target_link_libraries(normative_prerequisites_test PRIVATE ygo_m4)
add_test(NAME normative_prerequisites_test COMMAND normative_prerequisites_test)

add_executable(episodic_identity_test tests/episodic/episodic_identity_test.cpp)
target_link_libraries(episodic_identity_test PRIVATE ygo_m4)
add_test(NAME episodic_identity_test COMMAND episodic_identity_test)

add_executable(episodic_environment_test tests/episodic/episodic_environment_test.cpp)
target_link_libraries(episodic_environment_test PRIVATE ygo_m4)
add_test(NAME episodic_environment_test COMMAND episodic_environment_test)

add_executable(episodic_environment_privacy_fail_closed_test
               tests/episodic/episodic_environment_privacy_fail_closed_test.cpp)
target_link_libraries(episodic_environment_privacy_fail_closed_test PRIVATE ygo_m4)
add_test(NAME episodic_environment_privacy_fail_closed_test
         COMMAND episodic_environment_privacy_fail_closed_test)

add_executable(episodic_environment_v2_public_projection_test
               tests/episodic/episodic_environment_v2_public_projection_test.cpp)
target_link_libraries(episodic_environment_v2_public_projection_test PRIVATE ygo_m4)
add_test(NAME episodic_environment_v2_public_projection_test
         COMMAND episodic_environment_v2_public_projection_test)

add_executable(episodic_paired_world_test tests/episodic/episodic_paired_world_test.cpp)
target_link_libraries(episodic_paired_world_test PRIVATE ygo_m4)
target_include_directories(episodic_paired_world_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/src/environment")
add_test(NAME episodic_paired_world_test COMMAND episodic_paired_world_test)

add_executable(episodic_lifecycle_test tests/episodic/episodic_lifecycle_test.cpp)
target_link_libraries(episodic_lifecycle_test PRIVATE ygo_m4)
add_test(NAME episodic_lifecycle_test COMMAND episodic_lifecycle_test)

add_executable(episodic_rejection_test tests/episodic/episodic_rejection_test.cpp)
target_link_libraries(episodic_rejection_test PRIVATE ygo_m4)
add_test(NAME episodic_rejection_test COMMAND episodic_rejection_test)

add_executable(episodic_interrupt_test tests/episodic/episodic_interrupt_test.cpp)
target_link_libraries(episodic_interrupt_test PRIVATE ygo_m4)
add_test(NAME episodic_interrupt_test COMMAND episodic_interrupt_test)

add_executable(episodic_budget_test tests/episodic/episodic_budget_test.cpp)
target_link_libraries(episodic_budget_test PRIVATE ygo_m4)
add_test(NAME episodic_budget_test COMMAND episodic_budget_test)

add_executable(episodic_terminal_privacy_test tests/episodic/episodic_terminal_privacy_test.cpp)
target_link_libraries(episodic_terminal_privacy_test PRIVATE ygo_m4)
add_test(NAME episodic_terminal_privacy_test COMMAND episodic_terminal_privacy_test)

add_executable(episodic_replay_test tests/episodic/episodic_replay_test.cpp)
target_link_libraries(episodic_replay_test PRIVATE ygo_m4)
add_test(NAME episodic_replay_test COMMAND episodic_replay_test)

add_executable(episodic_fault_injection_test tests/episodic/episodic_fault_injection_test.cpp)
target_link_libraries(episodic_fault_injection_test PRIVATE ygo_m4)
target_compile_definitions(episodic_fault_injection_test PRIVATE
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME episodic_fault_injection_test COMMAND episodic_fault_injection_test)

add_executable(episodic_reset_after_failure_test
               tests/episodic/episodic_reset_after_failure_test.cpp)
target_link_libraries(episodic_reset_after_failure_test PRIVATE ygo_m4)
target_include_directories(episodic_reset_after_failure_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/src/environment")
add_test(NAME episodic_reset_after_failure_test COMMAND episodic_reset_after_failure_test)

add_executable(public_action_identity_test tests/episodic/public_action_identity_test.cpp)
target_include_directories(public_action_identity_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/src/environment")
target_link_libraries(public_action_identity_test PRIVATE ygo_m4)
add_test(NAME public_action_identity_test COMMAND public_action_identity_test)

add_executable(public_safe_state_test tests/environment/public_safe_state_test.cpp)
target_link_libraries(public_safe_state_test PRIVATE ygo_m4)
target_compile_definitions(public_safe_state_test PRIVATE
    YGO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME public_safe_state_test COMMAND public_safe_state_test)

add_executable(policy_boundary_compile_test tests/policy/policy_boundary_compile_test.cpp)
target_include_directories(policy_boundary_compile_test PRIVATE
                           "${CMAKE_SOURCE_DIR}/include")
add_test(NAME policy_boundary_compile_test COMMAND policy_boundary_compile_test)

add_executable(policy_rng_test tests/policy/rng_test.cpp)
target_link_libraries(policy_rng_test PRIVATE ygo_policy)
add_test(NAME policy_rng_test COMMAND policy_rng_test)

add_executable(random_legal_test tests/policy/random_legal_test.cpp)
target_link_libraries(random_legal_test PRIVATE ygo_policy)
add_test(NAME random_legal_test COMMAND random_legal_test)

add_executable(policy_random_legal_probe tools/policy_random_legal_probe/main.cpp)
target_link_libraries(policy_random_legal_probe PRIVATE ygo_policy)

add_executable(policy_runner_integration_test tests/policy/policy_runner_integration_test.cpp)
target_include_directories(policy_runner_integration_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(policy_runner_integration_test PRIVATE ygo_policy)
add_test(NAME policy_runner_integration_test COMMAND policy_runner_integration_test)

if(DEFINED ENV{OCGFORGE_PHASE1_RAW_ROOT} AND
   DEFINED ENV{OCGFORGE_PHASE1_POST_RAW_ROOT} AND
   DEFINED ENV{OCGFORGE_PHASE1_EXPECTED_HEAD} AND
   DEFINED ENV{OCGFORGE_PHASE1_POST_PROBE_SHA256})
    add_test(NAME phase1_equivalence_acceptance
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/episodic/phase1_acceptance.py"
                --baseline-raw-root "$ENV{OCGFORGE_PHASE1_RAW_ROOT}"
                --post-raw-root "$ENV{OCGFORGE_PHASE1_POST_RAW_ROOT}"
                --expected-head "$ENV{OCGFORGE_PHASE1_EXPECTED_HEAD}"
                --expected-probe-sha256 "$ENV{OCGFORGE_PHASE1_POST_PROBE_SHA256}")
    set_tests_properties(phase1_equivalence_acceptance PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        LABELS "PHASE1_ACCEPTANCE")
endif()

add_executable(protocol_test tests/protocol/protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE ygo_m0)
add_test(NAME protocol_test COMMAND protocol_test)

add_executable(continuation_core_test tests/protocol/continuation_core_test.cpp)
target_link_libraries(continuation_core_test PRIVATE ygo_m0)
add_test(NAME continuation_core_test COMMAND continuation_core_test)

add_executable(response_builder_test tests/protocol/response_builder_test.cpp)
target_link_libraries(response_builder_test PRIVATE ygo_m0)
add_test(NAME response_builder_test COMMAND response_builder_test)

add_executable(decision_family_test tests/protocol/decision_family_test.cpp)
target_link_libraries(decision_family_test PRIVATE ygo_m0)
add_test(NAME decision_family_test COMMAND decision_family_test)

add_executable(continuation_oracle_test tests/protocol/continuation_oracle_test.cpp)
target_link_libraries(continuation_oracle_test PRIVATE ygo_m0)
add_test(NAME continuation_oracle_test COMMAND continuation_oracle_test)

add_executable(decision_fail_closed_test tests/protocol/decision_fail_closed_test.cpp)
target_link_libraries(decision_fail_closed_test PRIVATE ygo_m0)
add_test(NAME decision_fail_closed_test COMMAND decision_fail_closed_test)

add_executable(trace_semantics_test tests/trace/trace_semantics_test.cpp)
target_link_libraries(trace_semantics_test PRIVATE ygo_m0)
add_test(NAME trace_semantics_test COMMAND trace_semantics_test)

add_executable(sha256_known_answer_test tests/trace/sha256_known_answer_test.cpp)
target_link_libraries(sha256_known_answer_test PRIVATE ygo_m0)
add_test(NAME sha256_known_answer_test COMMAND sha256_known_answer_test)

add_test(NAME m35_probe_trace_starting_player_test
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/m3_5/probe_trace_starting_player_test.py"
                 "$<TARGET_FILE:ygo_core_probe>")

add_executable(player_view_test tests/privacy/player_view_test.cpp)
target_link_libraries(player_view_test PRIVATE ygo_m0)
add_test(NAME player_view_test COMMAND player_view_test)

add_executable(observation_contract_test tests/observation/observation_contract_test.cpp)
target_link_libraries(observation_contract_test PRIVATE ygo_m0)
add_test(NAME observation_contract_test COMMAND observation_contract_test)

add_executable(card_catalog_test tests/observation/card_catalog_test.cpp)
target_link_libraries(card_catalog_test PRIVATE ygo_m0)
target_compile_definitions(card_catalog_test PRIVATE
    YGO_M2_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME card_catalog_test COMMAND card_catalog_test)

add_executable(query_decoder_test tests/observation/query_decoder_test.cpp)
target_link_libraries(query_decoder_test PRIVATE ygo_m0)
target_include_directories(query_decoder_test PRIVATE "${CMAKE_SOURCE_DIR}/src/observation")
add_test(NAME query_decoder_test COMMAND query_decoder_test)

add_executable(zone_projection_test tests/observation/zone_projection_test.cpp)
target_link_libraries(zone_projection_test PRIVATE ygo_m0)
target_include_directories(zone_projection_test PRIVATE "${CMAKE_SOURCE_DIR}/src/observation")
add_test(NAME zone_projection_test COMMAND zone_projection_test)

add_executable(privacy_projection_test tests/observation/privacy_projection_test.cpp)
target_link_libraries(privacy_projection_test PRIVATE ygo_m0)
target_include_directories(privacy_projection_test PRIVATE "${CMAKE_SOURCE_DIR}/src/observation")
add_test(NAME privacy_projection_test COMMAND privacy_projection_test)

add_executable(observation_builder_test tests/observation/observation_builder_test.cpp)
target_link_libraries(observation_builder_test PRIVATE ygo_m0)
target_compile_definitions(observation_builder_test PRIVATE
    YGO_M2_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M2_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M2_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME observation_builder_test COMMAND observation_builder_test)

add_executable(m4_3_5_serialization_fixture_test
    tests/observation/m4_3_5_serialization_fixture_test.cpp
)
target_link_libraries(m4_3_5_serialization_fixture_test PRIVATE ygo_m0)
add_test(NAME m4_3_5_serialization_fixture_test COMMAND m4_3_5_serialization_fixture_test)

add_executable(m4_3_6_direct_writer_test
    tests/observation/m4_3_6_direct_writer_test.cpp
)
target_link_libraries(m4_3_6_direct_writer_test PRIVATE ygo_m0)
target_include_directories(m4_3_6_direct_writer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/observation"
)
add_test(NAME m4_3_6_direct_writer_test COMMAND m4_3_6_direct_writer_test)

add_executable(mechanics_projection_test tests/observation/mechanics_projection_test.cpp)
target_link_libraries(mechanics_projection_test PRIVATE ygo_m0)
target_include_directories(mechanics_projection_test PRIVATE "${CMAKE_SOURCE_DIR}/src/observation")
target_compile_definitions(mechanics_projection_test PRIVATE
    YGO_M2_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M2_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M2_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M2_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m2_setup.lua"
)
add_test(NAME mechanics_projection_test COMMAND mechanics_projection_test)

add_executable(m2_1_xyz_api_test tests/observation/m2_1_xyz_api_test.cpp)
target_link_libraries(m2_1_xyz_api_test PRIVATE ygo_m0)
target_include_directories(m2_1_xyz_api_test PRIVATE "${CMAKE_SOURCE_DIR}/src/observation")
target_compile_definitions(m2_1_xyz_api_test PRIVATE
    YGO_M2_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M2_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M2_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M2_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m2_setup.lua"
    YGO_M2_SETUP_XYZ_A="${CMAKE_SOURCE_DIR}/fixtures/m2_1_setup_xyz_a.lua"
    YGO_M2_SETUP_XYZ_B="${CMAKE_SOURCE_DIR}/fixtures/m2_1_setup_xyz_b.lua"
)
add_test(NAME m2_1_xyz_api_test COMMAND m2_1_xyz_api_test)

add_executable(m35_xyz_material_query_test
    tests/m3_5/core/public_core_harness.cpp
    tests/m3_5/core/xyz_material_query_test.cpp
)
target_link_libraries(m35_xyz_material_query_test PRIVATE ygo_m0)
target_include_directories(m35_xyz_material_query_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/observation"
)
target_compile_definitions(m35_xyz_material_query_test PRIVATE
    YGO_M35_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M35_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M35_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M35_XYZ_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m2_setup.lua"
    YGO_M35_XYZ_EMPTY_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m35_xyz_empty.lua"
)
add_test(NAME m35_xyz_material_query_test COMMAND m35_xyz_material_query_test)

add_executable(m35_starting_player_api_test
    tests/m3_5/core/public_core_harness.cpp
    tests/m3_5/core/public_core_starting_api.cpp
    tests/m3_5/core/starting_player_api_test.cpp
)
target_link_libraries(m35_starting_player_api_test PRIVATE ygo_m0)
target_include_directories(m35_starting_player_api_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/observation"
)
target_compile_definitions(m35_starting_player_api_test PRIVATE
    YGO_M35_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M35_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M35_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME m35_starting_player_api_test COMMAND m35_starting_player_api_test)

add_executable(event_projection_test tests/observation/event_projection_test.cpp)
target_link_libraries(event_projection_test PRIVATE ygo_m0)
target_include_directories(event_projection_test PRIVATE "${CMAKE_SOURCE_DIR}/src/observation")
add_test(NAME event_projection_test COMMAND event_projection_test)

add_test(NAME observation_probe_test COMMAND ygo_observation_probe --fixture m2 --seed 123 --player 0)

add_test(NAME observation_coverage_test
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/observation/observation_coverage_test.py")

add_executable(decision_observation_test tests/observation/decision_observation_test.cpp)
target_link_libraries(decision_observation_test PRIVATE ygo_m0)
add_test(NAME decision_observation_test COMMAND decision_observation_test)

add_executable(continuation_privacy_test tests/privacy/continuation_privacy_test.cpp)
target_link_libraries(continuation_privacy_test PRIVATE ygo_m0)
add_test(NAME continuation_privacy_test COMMAND continuation_privacy_test)

add_executable(core_lifecycle_test tests/core/core_lifecycle_test.cpp)
target_link_libraries(core_lifecycle_test PRIVATE ygo_m0)
target_compile_definitions(core_lifecycle_test PRIVATE
    YGO_M0_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M0_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M0_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME core_lifecycle_test COMMAND core_lifecycle_test)

add_executable(core_metrics_test tests/core/core_metrics_test.cpp)
target_link_libraries(core_metrics_test PRIVATE ygo_m0)
target_compile_definitions(core_metrics_test PRIVATE
    YGO_M0_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M0_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M0_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME core_metrics_test COMMAND core_metrics_test)

add_executable(m3_rules_mode_test tests/m3/runtime/rules_mode_test.cpp)
target_link_libraries(m3_rules_mode_test PRIVATE ygo_m0)
target_compile_definitions(m3_rules_mode_test PRIVATE
    YGO_M0_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M0_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M0_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME m3_rules_mode_test COMMAND m3_rules_mode_test)

add_executable(deck_loader_test tests/m3/runtime/deck_loader_test.cpp)
target_link_libraries(deck_loader_test PRIVATE ygo_m0)
target_compile_definitions(deck_loader_test PRIVATE
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M0_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
)
add_test(NAME deck_loader_test COMMAND deck_loader_test)

add_executable(card_instantiation_test tests/m3/runtime/card_instantiation_test.cpp)
target_link_libraries(card_instantiation_test PRIVATE ygo_m0)
target_compile_definitions(card_instantiation_test PRIVATE
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME card_instantiation_test COMMAND card_instantiation_test)

add_executable(m3_fixture_test tests/m3/runtime/m3_fixture_test.cpp)
target_link_libraries(m3_fixture_test PRIVATE ygo_m0)
target_include_directories(m3_fixture_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/observation"
)
target_compile_definitions(m3_fixture_test PRIVATE
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M3_SS01_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss01_moye_token.lua"
    YGO_M3_SS07_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss07_token_restriction.lua"
    YGO_M3_SS08_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss08_baxia_real.lua"
    YGO_M3_SS12_CONDITION_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss12_tenyi_condition.lua"
    YGO_M3_SS05_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss05_chixiao_negate.lua"
    YGO_M3_SS06_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss06_longyuan.lua"
    YGO_M3_INT01_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_int01_ash_blossom.lua"
    YGO_M3_INT02_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_int02_effect_veiler.lua"
    YGO_M3_INT03_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_int03_impermanence.lua"
    YGO_M3_INT04_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_int04_ghost_belle.lua"
    YGO_M3_INT05_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_int05_called_by.lua"
    YGO_M3_SG01_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg01_fire_balelynx.lua"
    YGO_M3_SG02_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg02_deck_link.lua"
    YGO_M3_SG03_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg02_deck_link.lua"
    YGO_M3_SG04_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg02_deck_link.lua"
    YGO_M3_SS10_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss10_blackout.lua"
    YGO_M3_SS10_BANISH_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss10_blackout_banish_chengying.lua"
    YGO_M3_SS12_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss12_heavenly_dragon_circle.lua"
    YGO_M3_SS14_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss14_ashuna_restriction.lua"
    YGO_M3_SS15_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss15_vishuda_return.lua"
    YGO_M3_SS16_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss16_adhara_recovery.lua"
    YGO_M3_SS16_CHENGYING_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss10_blackout_banish_chengying.lua"
    YGO_M3_SS17_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_ss17_qixing_interaction.lua"
    YGO_M3_SG05_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg05_gazelle_gy.lua"
    YGO_M3_SG06_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg06_gazelle_gy.lua"
    YGO_M3_SG07_JACK_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg07_jack_jaguar.lua"
    YGO_M3_SG07_WEASEL_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg07_weasel.lua"
    YGO_M3_SG07_FALCO_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg07_falco.lua"
    YGO_M3_SG08_REAL_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg08_miragestallio_real.lua"
    YGO_M3_SG09_DIRECT_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg08_miragestallio.lua"
    YGO_M3_SG11_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg11_sunlight_wolf_link.lua"
    YGO_M3_SG11_PLAYER0_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg11_sunlight_wolf_link_player0.lua"
    YGO_M3_SG12_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg12_sunlight_wolf_st.lua"
    YGO_M3_SG13_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg13_raging_phoenix.lua"
    YGO_M3_SG14_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg14_pyro_phoenix.lua"
    YGO_M3_SG15_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg15_heatleo.lua"
    YGO_M3_SG18_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg18_promethean.lua"
    YGO_M3_SG19_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg19_hiita.lua"
    YGO_M3_SG19_NO_TARGET_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg19_hiita_no_target.lua"
    YGO_M3_SG16_NEGATE_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg16_roar.lua"
    YGO_M3_SG16_RECOVERY_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg16_roar_recovery.lua"
    YGO_M3_SG17_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg17_rage.lua"
    YGO_M3_BTL01_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_btl01_battle_phase.lua"
    YGO_M3_SG20_SETUP="${CMAKE_SOURCE_DIR}/fixtures/m3_sg11_sunlight_wolf_link.lua"
)
add_test(NAME m3_ss01_fixture COMMAND m3_fixture_test ss01)
add_test(NAME m3_ss07_fixture COMMAND m3_fixture_test ss07)
add_test(NAME m3_ss08_fixture COMMAND m3_fixture_test ss08)
add_test(NAME m3_ss12_condition_fixture COMMAND m3_fixture_test ss12_condition)
add_test(NAME m3_ss18_fixture COMMAND m3_fixture_test ss18)
add_test(NAME m3_ss05_fixture COMMAND m3_fixture_test ss05)
add_test(NAME m3_ss06_fixture COMMAND m3_fixture_test ss06)
add_test(NAME m3_int01_fixture COMMAND m3_fixture_test int01)
add_test(NAME m3_int02_fixture COMMAND m3_fixture_test int02)
add_test(NAME m3_int03_fixture COMMAND m3_fixture_test int03)
add_test(NAME m3_int04_fixture COMMAND m3_fixture_test int04)
add_test(NAME m3_int05_fixture COMMAND m3_fixture_test int05)
add_test(NAME m3_sg01_fixture COMMAND m3_fixture_test sg01)
add_test(NAME m3_sg02_fixture COMMAND m3_fixture_test sg02)
add_test(NAME m3_sg03_fixture COMMAND m3_fixture_test sg03)
add_test(NAME m3_sg04_fixture COMMAND m3_fixture_test sg04)
add_test(NAME m3_ss10_fixture COMMAND m3_fixture_test ss10)
add_test(NAME m3_ss10_banish_fixture COMMAND m3_fixture_test ss10_banish)
add_test(NAME m3_ss12_fixture COMMAND m3_fixture_test ss12)
add_test(NAME m3_ss14_fixture COMMAND m3_fixture_test ss14)
add_test(NAME m3_ss15_fixture COMMAND m3_fixture_test ss15)
add_test(NAME m3_ss16_fixture COMMAND m3_fixture_test ss16)
add_test(NAME m3_ss16_chengying_fixture COMMAND m3_fixture_test ss16_chengying)
add_test(NAME m3_ss17_fixture COMMAND m3_fixture_test ss17)
add_test(NAME m3_sg05_fixture COMMAND m3_fixture_test sg05)
add_test(NAME m3_sg06_fixture COMMAND m3_fixture_test sg06)
add_test(NAME m3_sg07_jack_fixture COMMAND m3_fixture_test sg07_jack)
add_test(NAME m3_sg07_weasel_fixture COMMAND m3_fixture_test sg07_weasel)
add_test(NAME m3_sg07_falco_fixture COMMAND m3_fixture_test sg07_falco)
add_test(NAME m3_sg08_real_fixture COMMAND m3_fixture_test sg08_real)
add_test(NAME m3_sg09_direct_fixture COMMAND m3_fixture_test sg09_direct)
add_test(NAME m3_sg11_fixture COMMAND m3_fixture_test sg11)
add_test(NAME m3_sg12_fixture COMMAND m3_fixture_test sg12)
add_test(NAME m3_sg13_fixture COMMAND m3_fixture_test sg13)
add_test(NAME m3_sg14_fixture COMMAND m3_fixture_test sg14)
add_test(NAME m3_sg15_fixture COMMAND m3_fixture_test sg15)
add_test(NAME m3_sg18_fixture COMMAND m3_fixture_test sg18)
add_test(NAME m3_sg19_fixture COMMAND m3_fixture_test sg19)
add_test(NAME m3_sg19_no_target_fixture COMMAND m3_fixture_test sg19_no_target)
add_test(NAME m3_sg16_negate_fixture COMMAND m3_fixture_test sg16_negate)
add_test(NAME m3_sg16_recovery_fixture COMMAND m3_fixture_test sg16_recovery)
add_test(NAME m3_sg17_fixture COMMAND m3_fixture_test sg17)
add_test(NAME m3_btl01_fixture COMMAND m3_fixture_test btl01)
add_test(NAME m3_sg20_fixture COMMAND m3_fixture_test sg20)

add_executable(m3_real_deck_privacy_test tests/m3/runtime/real_deck_privacy_test.cpp)
target_link_libraries(m3_real_deck_privacy_test PRIVATE ygo_m0)
target_compile_definitions(m3_real_deck_privacy_test PRIVATE
    YGO_M3_DECK_A="${CMAKE_SOURCE_DIR}/fixtures/decks/swordsoul_tenyi_ml_v1.ydk"
    YGO_M3_DECK_B="${CMAKE_SOURCE_DIR}/fixtures/decks/salamangreat_ml_v1.ydk"
    YGO_M3_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME m3_real_deck_privacy_test COMMAND m3_real_deck_privacy_test)

add_executable(m1_engine_fixture_test tests/core/m1_engine_fixture_test.cpp)
target_link_libraries(m1_engine_fixture_test PRIVATE ygo_m0)
target_compile_definitions(m1_engine_fixture_test PRIVATE
    YGO_M1_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_a.deck"
    YGO_M1_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/m1_tribute_b.deck"
    YGO_M1_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME m1_engine_fixture_test COMMAND m1_engine_fixture_test)

add_executable(m1_engine_conformance_test tests/core/m1_engine_conformance_test.cpp)
target_link_libraries(m1_engine_conformance_test PRIVATE ygo_m0)
target_compile_definitions(m1_engine_conformance_test PRIVATE
    YGO_M1_ENGINE_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M1_ENGINE_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M1_ENGINE_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
    YGO_M1_ENGINE_TRACE_DIR="${CMAKE_SOURCE_DIR}/artifacts/m1-engine"
)
add_test(NAME m1_engine_select_card_multi COMMAND m1_engine_conformance_test select_card_multi)
add_test(NAME m1_engine_select_option COMMAND m1_engine_conformance_test select_option)
add_test(NAME m1_engine_select_sum COMMAND m1_engine_conformance_test select_sum)
add_test(NAME m1_engine_select_counter COMMAND m1_engine_conformance_test select_counter)
add_test(NAME m1_engine_sort_card COMMAND m1_engine_conformance_test sort_card)
add_test(NAME m1_engine_select_disfield COMMAND m1_engine_conformance_test select_disfield)

add_test(NAME m1_engine_semantic_determinism_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/determinism/m1_engine_determinism_test.py"
            --probe "$<TARGET_FILE:m1_engine_conformance_test>"
)

add_test(NAME determinism_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/determinism/determinism_test.py"
            --probe "$<TARGET_FILE:ygo_core_probe>"
)

add_test(NAME fixture_deck_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/python/test_fixture_decks.py"
            --database "${M0_BABELCDB_DIR}/cards.cdb"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
            --deck "${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
)

add_test(NAME unsupported_diagnostic_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/diagnostics/unsupported_diagnostic_test.py"
            --probe "$<TARGET_FILE:ygo_core_probe>"
)

add_test(NAME decision_coverage_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/protocol/decision_coverage_test.py"
)

add_test(NAME m4_worker_protocol_test
    COMMAND "${Python3_EXECUTABLE}" -m unittest tests/m4/test_worker_protocol.py -v
)
set_tests_properties(m4_worker_protocol_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(NAME m4_failure_isolation_test
    COMMAND "${Python3_EXECUTABLE}" -m unittest tests/m4/test_failure_isolation.py -v
)
set_tests_properties(m4_failure_isolation_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    LABELS "M4_HEAVY_LIFECYCLE"
)

add_test(NAME m4_failure_isolation_fast_test
    COMMAND "${Python3_EXECUTABLE}" -m unittest
            tests.m4.test_failure_isolation.FailureIsolationTests.test_result_then_exit_is_not_published_as_primary_evidence
            -v
)
set_tests_properties(m4_failure_isolation_fast_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    LABELS "M4_FAST"
)

add_test(NAME m4_worker_integration_test
    COMMAND "${Python3_EXECUTABLE}" -m unittest tests/m4/test_worker_integration.py -v
)
set_tests_properties(m4_worker_integration_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "YGO_M4_WORKER=$<TARGET_FILE:ygo_m4_worker>"
    LABELS "M4_ACCEPTANCE_SCALE"
)

add_test(NAME m4_worker_integration_fast_test
    COMMAND "${Python3_EXECUTABLE}" -m unittest tests/m4/test_worker_integration_fast.py -v
)
set_tests_properties(m4_worker_integration_fast_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "YGO_M4_WORKER=$<TARGET_FILE:ygo_m4_worker>"
    LABELS "M4_HOSTED_FAST"
)
