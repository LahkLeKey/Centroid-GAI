cmake_minimum_required(VERSION 3.20)
project(centroid_life VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
# Package paths are relative to the prefix so exported targets and native tools
# retain their meaning after the complete SDK tree is moved.
foreach(_centroid_install_directory CMAKE_INSTALL_BINDIR CMAKE_INSTALL_LIBDIR
    CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_DATADIR)
  if(IS_ABSOLUTE "${${_centroid_install_directory}}" OR
     "${${_centroid_install_directory}}" MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)")
    message(FATAL_ERROR "${_centroid_install_directory} must remain within the relocatable SDK prefix")
  endif()
endforeach()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(BUILD_SHARED_LIBS "Build the deployment runtime as a shared library" OFF)
option(CENTROID_BUILD_TRAINING "Build the optional legacy training/workflow library" ON)
option(CENTROID_BUILD_TOOLS "Build the optional legacy command-line tools" ${CENTROID_BUILD_TRAINING})
option(CENTROID_BUILD_EXAMPLES "Build the native code-context host example" ON)
option(CENTROID_BUILD_TESTS "Build native C tests" ON)
option(CENTROID_BUILD_PACKAGE_TESTS "Validate an independently installed and relocated SDK" ON)
set(CENTROID_TRIAL_COMPILER "" CACHE STRING
  "Native C compiler executable for mandatory candidate tests (empty: auto-select)")

# This target must remain independent of the legacy library: no backward
# kernels, Life authority, process launcher or research fixtures are linked.
add_library(centroid_runtime
  src/runtime/model.c src/runtime/bundle.c src/runtime/context.c
  src/runtime/code_helper.c src/runtime/framing.c)
add_library(Centroid::Runtime ALIAS centroid_runtime)
if(CENTROID_BUILD_TESTS)
  add_executable(centroid_bundle_conformance tests/bundle_conformance.c)
  target_link_libraries(centroid_bundle_conformance PRIVATE Centroid::Runtime)
  add_executable(centroid_sdk_resource_check tests/sdk_resource_check.c)
  target_link_libraries(centroid_sdk_resource_check PRIVATE Centroid::Runtime)
  if(WIN32)
    target_compile_definitions(centroid_bundle_conformance PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(centroid_sdk_resource_check PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endif()
set_target_properties(centroid_runtime PROPERTIES EXPORT_NAME Runtime
  VERSION ${PROJECT_VERSION} SOVERSION 0 C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)
target_compile_features(centroid_runtime PUBLIC c_std_11)
target_include_directories(centroid_runtime
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE src/runtime)
if(BUILD_SHARED_LIBS)
  target_compile_definitions(centroid_runtime PUBLIC CENTROID_RUNTIME_SHARED
    PRIVATE CENTROID_RUNTIME_EXPORTS)
endif()
if(MSVC)
  target_compile_options(centroid_runtime PRIVATE /W4)
else()
  target_compile_options(centroid_runtime PRIVATE -Wall -Wextra -Wpedantic)
  if(NOT WIN32)
    target_link_libraries(centroid_runtime PUBLIC m)
  endif()
endif()
if(WIN32)
  target_compile_definitions(centroid_runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
install(TARGETS centroid_runtime EXPORT CentroidRuntimeTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES include/centroid_runtime.h include/centroid_context.h
  include/centroid_code_helper.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Runtime)
install(EXPORT CentroidRuntimeTargets FILE CentroidRuntimeTargets.cmake
  NAMESPACE Centroid:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Centroid
  COMPONENT Runtime)

if(CENTROID_BUILD_TOOLS AND NOT CENTROID_BUILD_TRAINING)
  message(FATAL_ERROR "CENTROID_BUILD_TOOLS requires CENTROID_BUILD_TRAINING; runtime-only builds should disable both")
endif()
if(CENTROID_BUILD_TRAINING)
# Compatibility training package includes the existing capture and research
# workflows. It is an optional development component, not an inference dependency.
add_library(centroid STATIC
  src/core/core.c src/core/codec.c src/platform/filesystem.c
  src/platform/process.c src/model/model.c src/context/records.c
  src/context/retrieve.c src/context/ingest.c src/context/codec.c
  src/context/work.c
  src/life/world.c src/life/policy.c src/life/trainer.c src/life/export.c src/domain/text.c src/domain/source.c src/domain/session.c src/domain/algorithms.c src/experiment/checkpoint.c
  src/experiment/contact.c src/experiment/experiment.c src/experiment/benchmark.c src/experiment/research.c src/experiment/code_suite.c src/experiment/extensions.c)
add_library(Centroid::Training ALIAS centroid)
set_target_properties(centroid PROPERTIES EXPORT_NAME Training)
target_compile_features(centroid PUBLIC c_std_11)
target_include_directories(centroid
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE src)
target_link_libraries(centroid PUBLIC Centroid::Runtime)
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
install(TARGETS centroid EXPORT CentroidTrainingTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Training)
install(FILES include/centroid.h include/centroid_training.h
  include/centroid_algorithms.h include/centroid_extensions.h
  include/centroid_session.h include/centroid_source.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Training)
install(EXPORT CentroidTrainingTargets FILE CentroidTrainingTargets.cmake
  NAMESPACE Centroid:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Centroid
  COMPONENT Training)
endif()
if(CENTROID_BUILD_TOOLS)
  add_executable(centroid_cli cli/main.c)
  set_target_properties(centroid_cli PROPERTIES OUTPUT_NAME centroid EXPORT_NAME CLI)
  target_link_libraries(centroid_cli PRIVATE centroid)
  add_executable(centroid_sdk_qualify tests/sdk_qualify.c)
  set_target_properties(centroid_sdk_qualify PROPERTIES
    OUTPUT_NAME centroid-sdk-qualify EXPORT_NAME SDKQualify)
  target_include_directories(centroid_sdk_qualify PRIVATE src)
  target_link_libraries(centroid_sdk_qualify PRIVATE centroid)
  if(WIN32)
    target_compile_definitions(centroid_sdk_qualify PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_definitions(centroid_sdk_qualify PRIVATE _POSIX_C_SOURCE=200809L)
  endif()
  install(TARGETS centroid_cli centroid_sdk_qualify EXPORT CentroidToolsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Tools)
  install(EXPORT CentroidToolsTargets FILE CentroidToolsTargets.cmake
    NAMESPACE Centroid:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Centroid
    COMPONENT Tools)
endif()
if(CENTROID_BUILD_EXAMPLES)
  add_subdirectory(examples/code_helper)
  install(TARGETS centroid_code_helper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Examples)
  install(FILES examples/code_helper/main.c examples/code_helper/CMakeLists.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/centroid/examples/code_helper
    COMPONENT Examples)
endif()
if(UNIX AND NOT APPLE)
  file(RELATIVE_PATH _centroid_runtime_rpath
    "/centroid-prefix/${CMAKE_INSTALL_BINDIR}"
    "/centroid-prefix/${CMAKE_INSTALL_LIBDIR}")
  foreach(_centroid_executable centroid_cli centroid_sdk_qualify centroid_code_helper)
    if(TARGET ${_centroid_executable})
      set_target_properties(${_centroid_executable} PROPERTIES
        INSTALL_RPATH "$ORIGIN/${_centroid_runtime_rpath}")
    endif()
  endforeach()
endif()

configure_package_config_file(cmake/CentroidConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CentroidConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Centroid)
# 0.x packages promise exact package versions while the ABI is prerelease.
# Portable bundle schema/version compatibility is a separate runtime contract.
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/CentroidConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CentroidConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/CentroidConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Centroid COMPONENT Runtime)

if(CENTROID_BUILD_TESTS)
  enable_testing()
  foreach(runtime_test runtime_context runtime_only runtime_framing)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_${runtime_test}.c")
      add_executable(test_${runtime_test} tests/test_${runtime_test}.c)
      target_link_libraries(test_${runtime_test} PRIVATE Centroid::Runtime)
      if(runtime_test STREQUAL "runtime_framing" AND CENTROID_BUILD_TRAINING)
        target_link_libraries(test_${runtime_test} PRIVATE Centroid::Training)
        target_compile_definitions(test_${runtime_test} PRIVATE CENTROID_TEST_TRAINING=1)
      endif()
      if(WIN32)
        target_compile_definitions(test_${runtime_test} PRIVATE _CRT_SECURE_NO_WARNINGS)
      else()
        target_compile_definitions(test_${runtime_test} PRIVATE _POSIX_C_SOURCE=200809L)
      endif()
      if(runtime_test STREQUAL "runtime_context")
        add_test(NAME ${runtime_test} COMMAND test_${runtime_test} "${CMAKE_SOURCE_DIR}")
      else()
        add_test(NAME ${runtime_test} COMMAND test_${runtime_test})
      endif()
    endif()
  endforeach()
  if(CENTROID_BUILD_PACKAGE_TESTS)
    include(cmake/InstalledConsumerTests.cmake)
  endif()
endif()
if(CENTROID_BUILD_TESTS AND CENTROID_BUILD_EXAMPLES)
  add_test(NAME context_lifecycle COMMAND centroid_code_helper --context-lifecycle)
endif()
if(CENTROID_BUILD_TESTS AND CENTROID_BUILD_TRAINING)
  if(CENTROID_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sdk_learning.c")
    add_executable(test_sdk_learning tests/test_sdk_learning.c)
    target_include_directories(test_sdk_learning PRIVATE src)
    target_link_libraries(test_sdk_learning PRIVATE centroid)
    if(WIN32)
      target_compile_definitions(test_sdk_learning PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
      target_compile_definitions(test_sdk_learning PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    add_test(NAME sdk_learning COMMAND test_sdk_learning
      "${CMAKE_CURRENT_BINARY_DIR}" "$<TARGET_FILE:centroid_sdk_qualify>"
      "${CMAKE_SOURCE_DIR}")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runtime.c")
    add_executable(test_runtime tests/test_runtime.c)
    target_include_directories(test_runtime PRIVATE src)
    target_link_libraries(test_runtime PRIVATE centroid)
    if(WIN32)
      target_compile_definitions(test_runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
      target_compile_definitions(test_runtime PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    add_test(NAME runtime COMMAND test_runtime)
  endif()
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
    if(name STREQUAL "work" AND NOT CENTROID_BUILD_TOOLS)
      message(STATUS "Skipping the CLI work test because CENTROID_BUILD_TOOLS=OFF")
      continue()
    endif()
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
include(cmake/CPackSDK.cmake)
include(cmake/SourcePackage.cmake)
