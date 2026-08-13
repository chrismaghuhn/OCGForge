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
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/prepare_card_data.py"
        "${M0_BABELCDB_DIR}/cards.cdb"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
        "${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
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
    src/protocol/message_decoder.cpp
    src/trace/engine_trace.cpp
    src/trace/sha256.cpp
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
)
if(MSVC)
    target_compile_options(ygo_m0 PRIVATE /W4 /permissive-)
else()
    target_compile_options(ygo_m0 PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ygo_core_probe tools/ygo_core_probe/main.cpp)
target_link_libraries(ygo_core_probe PRIVATE ygo_m0)
target_compile_definitions(ygo_core_probe PRIVATE
    YGO_M0_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M0_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M0_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)

enable_testing()
add_executable(protocol_test tests/protocol/protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE ygo_m0)
add_test(NAME protocol_test COMMAND protocol_test)

add_executable(player_view_test tests/privacy/player_view_test.cpp)
target_link_libraries(player_view_test PRIVATE ygo_m0)
add_test(NAME player_view_test COMMAND player_view_test)

add_executable(core_lifecycle_test tests/core/core_lifecycle_test.cpp)
target_link_libraries(core_lifecycle_test PRIVATE ygo_m0)
target_compile_definitions(core_lifecycle_test PRIVATE
    YGO_M0_PLAYER_A="${CMAKE_SOURCE_DIR}/fixtures/decks/player_a.deck"
    YGO_M0_PLAYER_B="${CMAKE_SOURCE_DIR}/fixtures/decks/player_b.deck"
    YGO_M0_CARDSCRIPTS="${M0_CARDSCRIPTS_DIR}"
)
add_test(NAME core_lifecycle_test COMMAND core_lifecycle_test)

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
