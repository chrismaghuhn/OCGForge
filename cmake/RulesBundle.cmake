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

set(M0_CORE_DIR "${M0_RULES_CACHE}/ocgcore" CACHE PATH "Pinned OCG core checkout")
set(M0_CARDSCRIPTS_DIR "${M0_RULES_CACHE}/cardscripts" CACHE PATH "Pinned CardScripts checkout")
set(M0_BABELCDB_DIR "${M0_RULES_CACHE}/babelcdb" CACHE PATH "Pinned BabelCDB checkout")
