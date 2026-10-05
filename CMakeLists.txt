cmake_minimum_required(VERSION 3.20)
project(centroid_life VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(CENTROID_BUILD_TESTS "Build native C tests" ON)
set(CENTROID_TRIAL_COMPILER "" CACHE STRING
  "Native C compiler executable for mandatory candidate tests (empty: auto-select)")
add_library(centroid STATIC
  src/core/core.c src/core/codec.c src/platform/filesystem.c
  src/platform/process.c src/model/model.c src/context/records.c
  src/context/retrieve.c src/context/ingest.c src/context/codec.c
  src/context/work.c
  src/life/world.c src/life/policy.c src/life/trainer.c src/domain/text.c src/domain/source.c src/domain/session.c src/domain/algorithms.c src/experiment/checkpoint.c
  src/experiment/contact.c src/experiment/experiment.c src/experiment/benchmark.c src/experiment/research.c src/experiment/code_suite.c src/experiment/extensions.c)
target_include_directories(centroid PUBLIC include PRIVATE src)
target_compile_definitions(centroid PRIVATE C_BUILD_ID="${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
if(MSVC)
  target_compile_options(centroid PRIVATE /W4)
else()
  target_compile_options(centroid PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_definitions(centroid PRIVATE _POSIX_C_SOURCE=200809L)
  if(NOT WIN32)
    target_link_libraries(centroid PUBLIC m)
  endif()
endif()
if(WIN32)
  target_link_libraries(centroid PUBLIC psapi)
  target_compile_definitions(centroid PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_executable(centroid_cli cli/main.c)
set_target_properties(centroid_cli PROPERTIES OUTPUT_NAME centroid)
target_link_libraries(centroid_cli PRIVATE centroid)
if(CENTROID_BUILD_TESTS)
  enable_testing()
  # Candidate correctness/publication tests are mandatory in a full test build.
  # Resolve an explicit override, otherwise prefer native clang/GCC and fall
  # back to the project's already identified supported C compiler.
  unset(_centroid_trial_compiler CACHE)
  unset(_centroid_trial_compiler)
  if(CENTROID_TRIAL_COMPILER)
    if(IS_ABSOLUTE "${CENTROID_TRIAL_COMPILER}" OR
       CENTROID_TRIAL_COMPILER MATCHES "[/\\\\]")
      get_filename_component(_centroid_trial_compiler
        "${CENTROID_TRIAL_COMPILER}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    else()
      find_program(_centroid_trial_compiler NAMES "${CENTROID_TRIAL_COMPILER}")
    endif()
  else()
    find_program(_centroid_trial_compiler NAMES clang gcc)
    if(NOT _centroid_trial_compiler AND
       CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang|MSVC)$")
      set(_centroid_trial_compiler "${CMAKE_C_COMPILER}")
    endif()
  endif()
  if(NOT _centroid_trial_compiler OR
     NOT EXISTS "${_centroid_trial_compiler}" OR
     IS_DIRECTORY "${_centroid_trial_compiler}")
    message(FATAL_ERROR
      "Full native tests require a runnable C compiler for experiment and code_suite. "
      "Set CENTROID_TRIAL_COMPILER to clang, GCC, cl or clang-cl; "
      "CENTROID_BUILD_TESTS=OFF builds the core without candidate-test dependencies.")
  endif()
  set(_centroid_trial_check "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/centroid-trial-compiler")
  file(MAKE_DIRECTORY "${_centroid_trial_check}")
  file(WRITE "${_centroid_trial_check}/probe.c"
    "#include <stdint.h>\n#include <stdio.h>\nint main(void) {\n"
    "  _Static_assert(sizeof(uint64_t) >= 8, \"C11 uint64_t\");\n"
    "  return puts(\"native C11 compiler probe\") == EOF;\n}\n")
  set(_centroid_trial_binary "${_centroid_trial_check}/probe${CMAKE_EXECUTABLE_SUFFIX}")
  get_filename_component(_centroid_trial_name "${_centroid_trial_compiler}" NAME)
  string(TOLOWER "${_centroid_trial_name}" _centroid_trial_name)
  if(_centroid_trial_name MATCHES "^(cl|clang-cl)(\\.exe)?$")
    set(_centroid_trial_arguments /nologo /std:c11 /W4 /WX
      "${_centroid_trial_check}/probe.c"
      "/Fo:${_centroid_trial_check}/probe.obj" "/Fe:${_centroid_trial_binary}")
  else()
    set(_centroid_trial_arguments -std=c11 -Wall -Wextra -Werror
      "${_centroid_trial_check}/probe.c" -o "${_centroid_trial_binary}")
  endif()
  execute_process(COMMAND "${_centroid_trial_compiler}" ${_centroid_trial_arguments}
    WORKING_DIRECTORY "${_centroid_trial_check}" TIMEOUT 60
    RESULT_VARIABLE _centroid_trial_status
    OUTPUT_VARIABLE _centroid_trial_stdout ERROR_VARIABLE _centroid_trial_stderr)
  if(NOT _centroid_trial_status STREQUAL "0" OR
     NOT EXISTS "${_centroid_trial_binary}")
    message(FATAL_ERROR
      "Candidate-test compiler '${_centroid_trial_compiler}' cannot compile/link the native C11 probe. "
      "For cl/clang-cl configure from a developer environment with the SDK/include/link paths. "
      "Set CENTROID_TRIAL_COMPILER to a usable native compiler, or disable tests explicitly.\n"
      "${_centroid_trial_stdout}${_centroid_trial_stderr}")
  endif()
  set(CENTROID_TRIAL_COMPILER "${_centroid_trial_compiler}" CACHE STRING
    "Native C compiler executable for mandatory candidate tests (empty: auto-select)" FORCE)
  message(STATUS "Mandatory candidate-test compiler: ${CENTROID_TRIAL_COMPILER}")
  foreach(name core context model text source life checkpoint process work experiment benchmark research session code_suite extensions)
    add_executable(test_${name} tests/test_${name}.c)
    target_include_directories(test_${name} PRIVATE src)
    target_link_libraries(test_${name} PRIVATE centroid)
    if(WIN32)
      target_compile_definitions(test_${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
      target_compile_definitions(test_${name} PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    if(name STREQUAL "experiment" OR name STREQUAL "code_suite")
      if(name STREQUAL "code_suite")
        add_test(NAME ${name} COMMAND test_${name} "${CMAKE_CURRENT_BINARY_DIR}" "${CENTROID_TRIAL_COMPILER}" "${CMAKE_SOURCE_DIR}")
      else()
        add_test(NAME ${name} COMMAND test_${name} "${CMAKE_CURRENT_BINARY_DIR}" "${CENTROID_TRIAL_COMPILER}")
      endif()
    elseif(name STREQUAL "work")
      add_dependencies(test_${name} centroid_cli)
      add_test(NAME ${name} COMMAND test_${name} "${CMAKE_CURRENT_BINARY_DIR}" "$<TARGET_FILE:centroid_cli>")
    elseif(name STREQUAL "process" OR name STREQUAL "benchmark")
      add_test(NAME ${name} COMMAND test_${name} "${CMAKE_CURRENT_BINARY_DIR}")
    else()
      add_test(NAME ${name} COMMAND test_${name})
    endif()
  endforeach()
endif()
