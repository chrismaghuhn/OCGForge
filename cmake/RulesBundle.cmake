if(NOT DEFINED Python3_EXECUTABLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

set(M0_RULES_CACHE "${CMAKE_SOURCE_DIR}/.cache/rules_bundle" CACHE PATH "Ignored rules-bundle cache")
set(M0_RULES_LOCK "${CMAKE_SOURCE_DIR}/third_party/rules_bundle.lock.json" CACHE FILEPATH "Rules-bundle lock")
option(M0_AUTO_FETCH_RULES "Fetch missing exact rule snapshots during configure" ON)

if(M0_AUTO_FETCH_RULES)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/fetch_rules_bundle.py"
                --lock "${M0_RULES_LOCK}" --cache "${M0_RULES_CACHE}"
        RESULT_VARIABLE M0_FETCH_RESULT
        OUTPUT_VARIABLE M0_FETCH_OUTPUT
        ERROR_VARIABLE M0_FETCH_ERROR
    )
    if(NOT M0_FETCH_RESULT EQUAL 0)
        message(FATAL_ERROR "rules bundle fetch failed:\n${M0_FETCH_OUTPUT}\n${M0_FETCH_ERROR}")
    endif()
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/verify_rules_bundle.py"
            --lock "${M0_RULES_LOCK}" --cache "${M0_RULES_CACHE}"
    RESULT_VARIABLE M0_VERIFY_RESULT
    OUTPUT_VARIABLE M0_VERIFY_OUTPUT
    ERROR_VARIABLE M0_VERIFY_ERROR
)
if(NOT M0_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "rules bundle verification failed:\n${M0_VERIFY_OUTPUT}\n${M0_VERIFY_ERROR}")
endif()
string(STRIP "${M0_VERIFY_OUTPUT}" M0_VERIFY_OUTPUT)
message(STATUS "${M0_VERIFY_OUTPUT}")

set(M0_CORE_BASE_DIR "${M0_RULES_CACHE}/ocgcore" CACHE PATH "Immutable pinned OCG core checkout")
set(M0_CORE_DIR "${CMAKE_SOURCE_DIR}/.cache/derived/ocgcore" CACHE PATH "Repository-patched OCG core checkout" FORCE)
set(M0_CARDSCRIPTS_DIR "${M0_RULES_CACHE}/cardscripts" CACHE PATH "Pinned CardScripts checkout")
set(M0_BABELCDB_DIR "${M0_RULES_CACHE}/babelcdb" CACHE PATH "Pinned BabelCDB checkout")

file(READ "${M0_RULES_LOCK}" M0_RULES_LOCK_JSON)
string(JSON M0_RULES_BUNDLE_ID GET "${M0_RULES_LOCK_JSON}" bundle_id)
string(JSON M0_RULES_FORMAT_ID GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs format_id)
string(JSON M0_RULES_DUEL_MODE GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs duel_mode)
string(JSON M0_RULES_DUEL_FLAGS GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs duel_flags value)
string(JSON M0_RULES_CORE_PATCHSET_ID GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs core patchset id)
string(JSON M0_RULES_CORE_PATCHSET_SHA256 GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs core patchset sha256)
string(JSON M0_CORE_API_VERSION GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs core api_version)
string(JSON M0_CORE_COMMIT GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs core commit)
string(JSON M0_CORE_RESOLVED_CHECKOUT_SHA256 GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs core resolved_checkout_sha256)
string(JSON M0_CARDSCRIPTS_COMMIT GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs card_scripts commit)
string(JSON M0_CARDSCRIPTS_RESOLVED_CHECKOUT_SHA256 GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs card_scripts resolved_checkout_sha256)
string(JSON M0_DATABASE_COMMIT GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs database commit)
string(JSON M0_DATABASE_RESOLVED_CHECKOUT_SHA256 GET "${M0_RULES_LOCK_JSON}" rule_affecting_inputs database resolved_checkout_sha256)
string(JSON M0_DATABASE_ARTIFACT_SHA256 GET "${M0_RULES_LOCK_JSON}" database_artifact sha256)

if(NOT M0_RULES_FORMAT_ID STREQUAL "TCG_ADVANCED_2026_05_18")
    message(FATAL_ERROR "canonical M3 format mismatch: ${M0_RULES_FORMAT_ID}")
endif()
if(NOT M0_RULES_DUEL_MODE STREQUAL "DUEL_MODE_MR5")
    message(FATAL_ERROR "canonical M3 duel mode mismatch: ${M0_RULES_DUEL_MODE}")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_ocgcore_patchset.py"
            --lock "${M0_RULES_LOCK}"
            --cache "${M0_RULES_CACHE}"
            --patchset-root "${CMAKE_SOURCE_DIR}/third_party/patches/ocgcore"
            --output "${M0_CORE_DIR}"
    RESULT_VARIABLE M0_PATCHSET_RESULT
    OUTPUT_VARIABLE M0_PATCHSET_OUTPUT
    ERROR_VARIABLE M0_PATCHSET_ERROR
)
if(NOT M0_PATCHSET_RESULT EQUAL 0)
    message(FATAL_ERROR "ocgcore patchset preparation failed:\n${M0_PATCHSET_OUTPUT}\n${M0_PATCHSET_ERROR}")
endif()
string(STRIP "${M0_PATCHSET_OUTPUT}" M0_PATCHSET_OUTPUT)
message(STATUS "${M0_PATCHSET_OUTPUT}")
