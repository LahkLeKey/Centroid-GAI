if(NOT CGAI_BUILD_EVOLUTION_TOOLS)
    return()
endif()

get_filename_component(EVOLVE_CMAKE_BIN "${CMAKE_COMMAND}" DIRECTORY)
find_program(EVOLVE_CTEST_COMMAND NAMES ctest HINTS "${EVOLVE_CMAKE_BIN}" REQUIRED)
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
set(EVOLVE_CLANG_TIDY "")
set(EVOLVE_CLANG_FORMAT "")
if(CLANG_TIDY_EXECUTABLE AND EXISTS "${CLANG_TIDY_EXECUTABLE}"
        AND NOT IS_DIRECTORY "${CLANG_TIDY_EXECUTABLE}")
    get_filename_component(EVOLVE_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE}" REALPATH)
    set(CLANG_TIDY_EXECUTABLE "${EVOLVE_CLANG_TIDY}")
endif()
if(CLANG_FORMAT_EXECUTABLE AND EXISTS "${CLANG_FORMAT_EXECUTABLE}"
        AND NOT IS_DIRECTORY "${CLANG_FORMAT_EXECUTABLE}")
    get_filename_component(EVOLVE_CLANG_FORMAT "${CLANG_FORMAT_EXECUTABLE}" REALPATH)
    set(CLANG_FORMAT_EXECUTABLE "${EVOLVE_CLANG_FORMAT}")
endif()
set(EVOLVE_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/evolve")
file(MAKE_DIRECTORY "${EVOLVE_BUILD_INCLUDE_DIR}")
set(EVOLVE_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set(EVOLVE_INPUT_MANIFEST "${EVOLVE_BUILD_INCLUDE_DIR}/inputs.txt")
set(EVOLVE_CMAKE_PROGRAM "${CMAKE_COMMAND}")
set(EVOLVE_CTEST_PROGRAM "${EVOLVE_CTEST_COMMAND}")
set(EVOLVE_CMAKE_GENERATOR "${CMAKE_GENERATOR}")
set(EVOLVE_CMAKE_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
set(EVOLVE_CMAKE_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
set(EVOLVE_C_COMPILER "${CMAKE_C_COMPILER}")
foreach(value IN ITEMS SOURCE_DIRECTORY INPUT_MANIFEST CMAKE_PROGRAM CTEST_PROGRAM
        CMAKE_GENERATOR CMAKE_PLATFORM CMAKE_TOOLSET C_COMPILER CLANG_TIDY CLANG_FORMAT)
    string(REPLACE "\\" "\\\\" EVOLVE_${value}_ESCAPED "${EVOLVE_${value}}")
    string(REPLACE "\"" "\\\"" EVOLVE_${value}_ESCAPED "${EVOLVE_${value}_ESCAPED}")
endforeach()
set(EVOLVE_SANITIZER_VALUE 0)
if(CGAI_ENABLE_SANITIZERS)
    set(EVOLVE_SANITIZER_VALUE 1)
endif()
configure_file(tools/evolve/build_config.h.in "${EVOLVE_BUILD_INCLUDE_DIR}/build_config.h" @ONLY)

# Pin the current working source, fixtures, validation settings, and native tools.
# Deleted files are absent from these globs; no Git checkout participates in evolution.
file(GLOB_RECURSE EVOLVE_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.in" "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/data/gameplay/*.tsv")
list(APPEND EVOLVE_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/life/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/evolve/build_config.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format" "${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
    "${CMAKE_C_COMPILER}" "${CMAKE_COMMAND}" "${EVOLVE_CTEST_COMMAND}")
foreach(check_tool IN ITEMS EVOLVE_CLANG_TIDY EVOLVE_CLANG_FORMAT)
    if(${check_tool})
        list(APPEND EVOLVE_INPUTS "${${check_tool}}")
    endif()
endforeach()
list(REMOVE_DUPLICATES EVOLVE_INPUTS)
list(SORT EVOLVE_INPUTS)
string(REPLACE ";" "\n" EVOLVE_INPUT_TEXT "${EVOLVE_INPUTS}")
file(WRITE "${EVOLVE_INPUT_MANIFEST}" "${EVOLVE_INPUT_TEXT}\n")

set(EVOLVE_SOURCES tools/evolve/mutation.c tools/evolve/process.c tools/evolve/evolve_io.c
    tools/evolve/evolve_main.c tools/evolve/evolve_runner.c tools/evolve/evolve_report.c
    tools/evolve/evolve_checkpoint.c tools/evolve/evolve_checkpoint_codec.c
    tools/evolve/fitness.c tools/evolve/fitness_main.c)
add_library(centroid_evolve_mutation STATIC tools/evolve/mutation.c)
target_include_directories(centroid_evolve_mutation PRIVATE include tools/evolve)
target_link_libraries(centroid_evolve_mutation PRIVATE centroid_life::centroid_life)
cgai_enable_warnings(centroid_evolve_mutation)
add_library(centroid_evolve_process STATIC tools/evolve/process.c)
target_include_directories(centroid_evolve_process PRIVATE tools/evolve)
cgai_enable_warnings(centroid_evolve_process)
add_library(centroid_evolve_fitness STATIC tools/evolve/fitness.c)
target_include_directories(centroid_evolve_fitness PRIVATE src src/life tools/evolve)
target_link_libraries(centroid_evolve_fitness PRIVATE centroid_life::centroid_life)
cgai_enable_warnings(centroid_evolve_fitness)
add_executable(cgai_life_fitness tools/evolve/fitness_main.c)
target_include_directories(cgai_life_fitness PRIVATE src src/life tools/evolve)
target_link_libraries(cgai_life_fitness PRIVATE centroid_evolve_fitness)
cgai_enable_warnings(cgai_life_fitness)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/evolve-tools-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:cgai_life_fitness>\n")

add_executable(cgai_life_evolve tools/evolve/evolve_main.c tools/evolve/evolve_io.c
    tools/evolve/evolve_runner.c tools/evolve/evolve_report.c tools/evolve/evolve_checkpoint.c
    tools/evolve/evolve_checkpoint_codec.c)
target_include_directories(cgai_life_evolve PRIVATE src src/life tools/evolve
    tools/context "${EVOLVE_BUILD_INCLUDE_DIR}")
target_link_libraries(cgai_life_evolve PRIVATE centroid_evolve_mutation centroid_evolve_process
    centroid_evolve_fitness centroid_context_repository centroid_life::centroid_life)
cgai_enable_warnings(cgai_life_evolve)

if(CGAI_BUILD_TESTS)
    set(EVOLVE_TEST_SOURCES tools/evolve/tests/test_mutation.c
        tools/evolve/tests/test_fitness.c tools/evolve/tests/test_process.c
        tools/evolve/tests/test_inputs.c tools/evolve/tests/test_checkpoint.c)
    foreach(suite IN ITEMS mutation fitness process)
        set(evolve_test_source tools/evolve/tests/test_${suite}.c)
        add_executable(centroid_evolve_${suite}_tests ${evolve_test_source} tests/test_utils.c)
        target_include_directories(centroid_evolve_${suite}_tests PRIVATE include tests src src/life tools/evolve)
        target_link_libraries(centroid_evolve_${suite}_tests PRIVATE centroid_evolve_${suite})
        cgai_enable_warnings(centroid_evolve_${suite}_tests)
    endforeach()
    add_test(NAME centroid_evolve_mutation COMMAND centroid_evolve_mutation_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/gameplay_model.c")
    add_test(NAME centroid_evolve_fitness COMMAND centroid_evolve_fitness_tests)
    add_test(NAME centroid_evolve_process COMMAND centroid_evolve_process_tests)
    add_executable(centroid_evolve_inputs_tests tools/evolve/tests/test_inputs.c
        tools/evolve/evolve_runner.c tools/evolve/evolve_io.c tests/test_utils.c)
    target_include_directories(centroid_evolve_inputs_tests PRIVATE include tests src src/life
        tools/evolve tools/context "${EVOLVE_BUILD_INCLUDE_DIR}")
    target_link_libraries(centroid_evolve_inputs_tests PRIVATE centroid_evolve_mutation
        centroid_evolve_process centroid_evolve_fitness centroid_context_repository
        centroid_life::centroid_life)
    cgai_enable_warnings(centroid_evolve_inputs_tests)
    add_test(NAME centroid_evolve_inputs COMMAND centroid_evolve_inputs_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/gameplay_model.c")
    add_executable(centroid_evolve_checkpoint_tests tools/evolve/tests/test_checkpoint.c
        tools/evolve/evolve_checkpoint.c tools/evolve/evolve_checkpoint_codec.c
        tools/evolve/evolve_runner.c
        tools/evolve/evolve_io.c tests/test_utils.c)
    target_include_directories(centroid_evolve_checkpoint_tests PRIVATE include tests src src/life
        tools/evolve tools/context "${EVOLVE_BUILD_INCLUDE_DIR}")
    target_link_libraries(centroid_evolve_checkpoint_tests PRIVATE centroid_evolve_mutation
        centroid_evolve_process centroid_evolve_fitness centroid_context_repository
        centroid_life::centroid_life)
    cgai_enable_warnings(centroid_evolve_checkpoint_tests)
    add_test(NAME centroid_evolve_checkpoint COMMAND centroid_evolve_checkpoint_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/gameplay_model.c" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(centroid_evolve_mutation centroid_evolve_fitness centroid_evolve_process
        centroid_evolve_inputs centroid_evolve_checkpoint PROPERTIES TIMEOUT 180)
endif()
