cmake_minimum_required(VERSION 3.20)
project(centroid_gai VERSION 0.4.0 LANGUAGES C)

option(CGAI_BUILD_TESTS "Build the test suite" ON)
option(CGAI_BUILD_DOCS "Build API documentation with Doxygen" OFF)
option(CGAI_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Require ISO C11 for every library, executable, and test target.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(cgai_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endfunction()

include(cmake/Lint.cmake)

# Each knowledge catalog category is an independent C translation unit.
file(GLOB CGAI_KNOWLEDGE_CATALOG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/knowledge/knowledge_catalog/*.c")
file(GLOB CGAI_GAMEPLAY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/*.c")

add_library(centroid_gai
    src/bark/bark_model.c
    src/bark/bark_session.c
    src/chat/chat_codec.c
    src/chat/chat_evaluation.c
    src/chat/chat_model.c
    src/chat/chat_prompt.c
    src/core/error.c
    src/core/file_utils.c
    src/core/json_writer.c
    src/core/model_random.c
    src/core/tokenizer.c
    src/core/vocabulary.c
    src/knowledge/knowledge_catalog.c
    src/knowledge/knowledge_clusters.c
    src/knowledge/knowledge_query.c
    src/knowledge/static_knowledge.c
    src/model/centroid_gai.c
    src/model/model_centroid.c
    src/model/model_composition.c
    src/model/model_decode.c
    src/model/model_embedding.c
    src/model/model_encode.c
    src/model/model_file.c
    src/model/model_generation.c
    src/model/model_generation_workspace.c
    src/model/model_sampling.c
    src/model/model_training.c
    src/neural/neural_evaluation.c
    src/neural/neural_checkpoint.c
    src/neural/neural_file.c
    src/neural/neural_generation.c
    src/neural/neural_math.c
    src/neural/neural_model.c
    src/neural/neural_resources.c
    src/neural/neural_training.c
    src/neural/neural_vocabulary.c
    src/spatial/spatial_index.c
    src/spatial/spatial_query.c
    ${CGAI_KNOWLEDGE_CATALOG_SOURCES}
    ${CGAI_GAMEPLAY_SOURCES}
)
add_library(centroid_gai::centroid_gai ALIAS centroid_gai)
set_target_properties(centroid_gai PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(centroid_gai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(centroid_gai PUBLIC c_std_11)

cgai_enable_warnings(centroid_gai)

add_library(centroid_gai_abi SHARED
    src/abi/abi.c
    src/abi/abi_buffer.c
    src/abi/abi_config.c
    src/abi/abi_generation.c
    src/abi/abi_inspection.c
    src/abi/abi_knowledge.c
    src/abi/abi_metadata.c
    src/abi/abi_model.c
    src/abi/abi_serialization.c
    src/abi/abi_spatial.c
)
add_library(centroid_gai::abi ALIAS centroid_gai_abi)
target_compile_features(centroid_gai_abi PUBLIC c_std_11)
target_compile_definitions(centroid_gai_abi PRIVATE CGAI_ABI_BUILD)
target_include_directories(centroid_gai_abi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(centroid_gai_abi PRIVATE centroid_gai::centroid_gai)
cgai_enable_warnings(centroid_gai_abi)

if(WIN32)
    target_compile_definitions(centroid_gai PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(centroid_gai PUBLIC m)
endif()

if(CGAI_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(centroid_gai PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(centroid_gai PUBLIC -fsanitize=address,undefined)
endif()

add_executable(cgai
    src/cli/cli.c
    src/cli/cli_arguments.c
    src/cli/cli_generation.c
    src/cli/cli_knowledge.c
    src/cli/cli_neural_arguments.c
    src/cli/cli_neural_candidate.c
    src/cli/cli_neural_checkpoint.c
    src/cli/cli_neural_evaluation.c
    src/cli/cli_neural_generation.c
    src/cli/cli_neural_training.c
    src/cli/cli_neural_step.c
    src/cli/cli_training.c
    src/cli/main.c
)
target_include_directories(cgai PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
cgai_enable_warnings(cgai)
target_link_libraries(cgai PRIVATE centroid_gai::centroid_gai)
add_executable(cgai_gameplay_demo examples/gameplay_generation.c)
target_link_libraries(cgai_gameplay_demo PRIVATE centroid_gai::centroid_gai)
cgai_enable_warnings(cgai_gameplay_demo)
add_executable(cgai_bark_demo examples/bark_selector.c)
target_link_libraries(cgai_bark_demo PRIVATE centroid_gai::centroid_gai)
cgai_enable_warnings(cgai_bark_demo)
add_executable(cgai_bark_tool tools/bark/bark_main.c tools/bark/bark_training.c
    tools/bark/bark_report.c tools/bark/bark_evaluation.c tools/bark/bark_benchmark.c
    tools/bark/bark_fixture.c)
target_include_directories(cgai_bark_tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/bark)
target_link_libraries(cgai_bark_tool PRIVATE centroid_gai::centroid_gai)
cgai_enable_warnings(cgai_bark_tool)
if(WIN32)
    target_compile_definitions(cgai_bark_tool PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
include(cmake/NeuralModel.cmake)
file(GLOB CGAI_GAMEPLAY_TOOL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay/*.c")
add_executable(cgai_gameplay_tool ${CGAI_GAMEPLAY_TOOL_SOURCES})
target_include_directories(cgai_gameplay_tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay)
target_link_libraries(cgai_gameplay_tool PRIVATE centroid_gai::centroid_gai)
cgai_enable_warnings(cgai_gameplay_tool)
add_executable(cgai_composed_demo examples/composed_gameplay.c tools/gameplay/gameplay_tasks.c)
target_include_directories(cgai_composed_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay)
target_link_libraries(cgai_composed_demo PRIVATE centroid_gai::centroid_gai)
cgai_enable_warnings(cgai_composed_demo)
if(WIN32)
    target_compile_definitions(cgai_gameplay_tool PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(CGAI_NODE_EXECUTABLE)
    foreach(action IN ITEMS train verify replay)
        add_custom_target(bark_${action}
            COMMAND "${CGAI_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bark/workflow.mjs"
                ${action} --tool "$<TARGET_FILE:cgai_bark_tool>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/bark"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Running the deterministic bark ${action} workflow"
            VERBATIM)
    endforeach()
    add_dependencies(bark_train cgai_bark_tool)
    add_dependencies(bark_replay cgai_bark_tool)
    foreach(action IN ITEMS train verify replay)
        add_custom_target(gameplay_${action}
            COMMAND "${CGAI_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay/workflow.mjs"
                ${action} --tool "$<TARGET_FILE:cgai_gameplay_tool>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/gameplay"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
    endforeach()
    add_dependencies(gameplay_train cgai_gameplay_tool)
    add_dependencies(gameplay_verify cgai_gameplay_tool)
    add_dependencies(gameplay_replay cgai_gameplay_tool)
endif()
if(WIN32)
    target_compile_definitions(cgai PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(CGAI_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/centroid_gai)
configure_package_config_file(
    cmake/centroid_gaiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/centroid_gaiConfig.cmake
    INSTALL_DESTINATION ${CGAI_CMAKE_INSTALL_DIR}
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/centroid_gaiConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(TARGETS centroid_gai centroid_gai_abi cgai
    EXPORT centroid_gaiTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT centroid_gaiTargets
    NAMESPACE centroid_gai::
    DESTINATION ${CGAI_CMAKE_INSTALL_DIR}
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/centroid_gaiConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/centroid_gaiConfigVersion.cmake
    DESTINATION ${CGAI_CMAKE_INSTALL_DIR}
)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/centroid_gai)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/centroid_gai)

if(CGAI_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(DOXYGEN_INPUT_FILTER
        "\\\"${Python3_EXECUTABLE}\\\" \\\"${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/doxygen_filter.py\\\"")
    if(WIN32)
        # cmd.exe needs a command before a quoted executable passed through popen.
        string(PREPEND DOXYGEN_INPUT_FILTER "call ")
    endif()
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
    configure_file(Doxyfile.in Doxyfile @ONLY)
    add_custom_target(docs
        COMMAND "${CMAKE_COMMAND}"
            "-DDOXYGEN_EXECUTABLE=${DOXYGEN_EXECUTABLE}"
            "-DDOXYGEN_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
            "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
            "-DDOXYGEN_INPUT_DIR=${DOXYGEN_INPUT_DIR}"
            "-DDOXYGEN_XML_DIR=${DOXYGEN_OUTPUT_DIR}/xml"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DoxygenRun.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Centroid-GAI API documentation"
        VERBATIM
    )
endif()

if(CGAI_BUILD_TESTS)
    enable_testing()
    if(CGAI_NODE_EXECUTABLE)
        file(GLOB CGAI_GAMEPLAY_WORKFLOW_TESTS CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay/*.test.mjs")
        add_test(NAME centroid_gai_gameplay_workflow COMMAND ${CMAKE_COMMAND} -E env
            "CGAI_GAMEPLAY_TOOL=$<TARGET_FILE:cgai_gameplay_tool>"
            "CGAI_COMPOSED_DEMO=$<TARGET_FILE:cgai_composed_demo>"
            "${CGAI_NODE_EXECUTABLE}" --test ${CGAI_GAMEPLAY_WORKFLOW_TESTS})
        set_tests_properties(centroid_gai_gameplay_workflow PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 900)
        add_test(NAME centroid_gai_gameplay_release COMMAND "${CGAI_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay/workflow.mjs" verify
            --tool "$<TARGET_FILE:cgai_gameplay_tool>")
        set_tests_properties(centroid_gai_gameplay_release PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 120)
        add_test(NAME centroid_gai_gameplay_legacy COMMAND "${CGAI_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay/workflow.mjs" verify-legacy)
        set_tests_properties(centroid_gai_gameplay_legacy PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME centroid_gai_bark_workflow COMMAND ${CMAKE_COMMAND} -E env
            "CGAI_BARK_TOOL=$<TARGET_FILE:cgai_bark_tool>"
            "${CGAI_NODE_EXECUTABLE}" --test
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bark/workflow.test.mjs"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bark/git-policy.test.mjs")
        set_tests_properties(centroid_gai_bark_workflow PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 120)
        add_test(NAME centroid_gai_bark_release COMMAND "${CGAI_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bark/workflow.mjs" verify
            --tool "$<TARGET_FILE:cgai_bark_tool>")
        set_tests_properties(centroid_gai_bark_release PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    foreach(suite IN ITEMS math core codec)
        add_executable(centroid_gai_gameplay_${suite}_tests
            tests/test_gameplay_${suite}.c tests/test_utils.c)
        target_include_directories(centroid_gai_gameplay_${suite}_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(centroid_gai_gameplay_${suite}_tests PRIVATE centroid_gai::centroid_gai)
        cgai_enable_warnings(centroid_gai_gameplay_${suite}_tests)
        add_test(NAME centroid_gai_gameplay_${suite} COMMAND centroid_gai_gameplay_${suite}_tests)
    endforeach()
    add_executable(centroid_gai_gameplay_tasks_tests tests/test_gameplay_tasks.c
        tools/gameplay/gameplay_tasks.c tests/test_utils.c)
    target_include_directories(centroid_gai_gameplay_tasks_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay)
    target_link_libraries(centroid_gai_gameplay_tasks_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_gameplay_tasks_tests)
    add_test(NAME centroid_gai_gameplay_tasks COMMAND centroid_gai_gameplay_tasks_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/data/gameplay/composed-v1)
    add_executable(centroid_gai_gameplay_quality_tests tests/test_gameplay_quality.c
        tools/gameplay/gameplay_evaluation.c tools/gameplay/gameplay_tasks.c tests/test_utils.c)
    target_include_directories(centroid_gai_gameplay_quality_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay)
    target_link_libraries(centroid_gai_gameplay_quality_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_gameplay_quality_tests)
    add_test(NAME centroid_gai_gameplay_quality COMMAND centroid_gai_gameplay_quality_tests)
    add_executable(centroid_gai_gameplay_initialization_tests tests/test_gameplay_initialization.c
        tools/gameplay/gameplay_initialization.c tools/gameplay/gameplay_tasks.c tests/test_utils.c)
    target_include_directories(centroid_gai_gameplay_initialization_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay)
    target_link_libraries(centroid_gai_gameplay_initialization_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_gameplay_initialization_tests)
    add_test(NAME centroid_gai_gameplay_initialization COMMAND centroid_gai_gameplay_initialization_tests)
    add_executable(centroid_gai_bark_tests tests/test_bark.c tests/test_utils.c)
    target_include_directories(centroid_gai_bark_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(centroid_gai_bark_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_bark_tests)
    add_test(NAME centroid_gai_bark COMMAND centroid_gai_bark_tests)
    add_executable(centroid_gai_bark_fixture_tests tests/test_bark_fixture.c
        tools/bark/bark_fixture.c tests/test_utils.c)
    target_include_directories(centroid_gai_bark_fixture_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/bark)
    target_link_libraries(centroid_gai_bark_fixture_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_bark_fixture_tests)
    add_test(NAME centroid_gai_bark_fixture COMMAND centroid_gai_bark_fixture_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/data/gameplay/barks-v1)
    add_executable(centroid_gai_bark_quality_tests tests/test_bark_quality.c
        tools/bark/bark_evaluation.c tools/bark/bark_fixture.c tests/test_utils.c)
    target_include_directories(centroid_gai_bark_quality_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools/bark)
    target_link_libraries(centroid_gai_bark_quality_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_bark_quality_tests)
    add_test(NAME centroid_gai_bark_quality COMMAND centroid_gai_bark_quality_tests)
    add_executable(centroid_gai_chat_tests tests/test_chat.c tests/test_utils.c)
    target_link_libraries(centroid_gai_chat_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_chat_tests)
    add_test(NAME centroid_gai_chat COMMAND centroid_gai_chat_tests)
    add_executable(centroid_gai_neural_tests tests/test_neural.c tests/test_neural_math.c
        tests/test_neural_training.c tests/test_neural_io.c tests/test_neural_session.c
        tests/test_utils.c)
    target_include_directories(centroid_gai_neural_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(centroid_gai_neural_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_neural_tests)
    add_test(NAME centroid_gai_neural_tests COMMAND centroid_gai_neural_tests)
    add_test(NAME centroid_gai_neural_cli_tests COMMAND ${CMAKE_COMMAND}
        -DCGAI_EXE=$<TARGET_FILE:cgai> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_neural_cli.cmake)
    foreach(suite IN ITEMS resume checkpoint examples)
        add_executable(centroid_gai_neural_${suite}_tests
            tests/test_neural_${suite}.c tests/test_utils.c)
        target_include_directories(centroid_gai_neural_${suite}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(centroid_gai_neural_${suite}_tests PRIVATE centroid_gai::centroid_gai)
        cgai_enable_warnings(centroid_gai_neural_${suite}_tests)
        add_test(NAME centroid_gai_neural_${suite} COMMAND centroid_gai_neural_${suite}_tests)
    endforeach()
    add_test(NAME centroid_gai_neural_workflow COMMAND ${CMAKE_COMMAND}
        -DCGAI_EXE=$<TARGET_FILE:cgai> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_neural_workflow.cmake)
    add_test(NAME centroid_gai_neural_candidate COMMAND ${CMAKE_COMMAND}
        -DCGAI_EXE=$<TARGET_FILE:cgai> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_neural_candidate.cmake)
    add_test(NAME centroid_gai_neural_model_validate COMMAND ${CMAKE_COMMAND}
        -DCGAI_EXE=$<TARGET_FILE:cgai>
        -DCGAI_MODEL_DIR=${CGAI_MODEL_DIR}
        -DCGAI_MODEL_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/model-validation
        -DCGAI_MODEL_ACTION=validate -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NeuralModelRun.cmake)
    add_executable(centroid_gai_tests tests/test_centroid_gai.c tests/test_utils.c)
    target_link_libraries(centroid_gai_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_tests)
    add_test(NAME centroid_gai_tests COMMAND centroid_gai_tests)

    add_executable(centroid_gai_abi_tests tests/test_abi.c tests/test_utils.c)
    target_link_libraries(centroid_gai_abi_tests PRIVATE centroid_gai::abi)
    cgai_enable_warnings(centroid_gai_abi_tests)
    add_test(NAME centroid_gai_abi_tests COMMAND centroid_gai_abi_tests)
    add_executable(centroid_gai_spatial_tests tests/test_spatial.c tests/test_utils.c)
    target_link_libraries(centroid_gai_spatial_tests PRIVATE centroid_gai::centroid_gai centroid_gai::abi)
    cgai_enable_warnings(centroid_gai_spatial_tests)
    add_test(NAME centroid_gai_spatial_tests COMMAND centroid_gai_spatial_tests)
    add_executable(centroid_gai_knowledge_tests tests/test_knowledge.c tests/test_utils.c)
    target_link_libraries(centroid_gai_knowledge_tests PRIVATE centroid_gai::centroid_gai centroid_gai::abi)
    cgai_enable_warnings(centroid_gai_knowledge_tests)
    add_test(NAME centroid_gai_knowledge_tests COMMAND centroid_gai_knowledge_tests)
    add_test(NAME centroid_gai_knowledge_cli_tests COMMAND ${CMAKE_COMMAND}
        -DCGAI_EXE=$<TARGET_FILE:cgai> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_knowledge_cli.cmake)

    add_executable(centroid_gai_unit_tests
        tests/test_runner.c
        tests/test_tokenizer.c
        tests/test_vocabulary.c
        tests/test_model_math.c
        tests/test_model_io.c
        tests/test_composition.c
        tests/test_sampling.c
        tests/test_file_utils.c
        tests/test_utils.c
    )
    target_include_directories(centroid_gai_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(centroid_gai_unit_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_unit_tests)
    add_test(NAME centroid_gai_unit_tests COMMAND centroid_gai_unit_tests)
endif()
