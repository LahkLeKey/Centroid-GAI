cmake_minimum_required(VERSION 3.20)
project(centroid_life VERSION 3.0.0 LANGUAGES C)

option(CGAI_BUILD_TESTS "Build native C11 tests" ON)
option(CGAI_BUILD_DOCS "Build native API documentation" OFF)
option(CGAI_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(CGAI_BUILD_EVOLUTION_TOOLS "Build C11 Life-driven source candidate tools" ON)
set(CGAI_MUTATION_SOURCE_FILE "" CACHE FILEPATH "Isolated gameplay initialization candidate")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

function(cgai_enable_sanitizers target scope)
    if(CGAI_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target} ${scope} -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} ${scope} -fsanitize=address,undefined)
    endif()
endfunction()

function(cgai_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    cgai_enable_sanitizers(${target} PRIVATE)
endfunction()

set(LIFE_GAMEPLAY_MODEL_SOURCE src/gameplay/gameplay_model.c)
if(CGAI_MUTATION_SOURCE_FILE)
    get_filename_component(LIFE_GAMEPLAY_MODEL_SOURCE "${CGAI_MUTATION_SOURCE_FILE}" REALPATH)
    if(NOT EXISTS "${LIFE_GAMEPLAY_MODEL_SOURCE}" OR IS_DIRECTORY "${LIFE_GAMEPLAY_MODEL_SOURCE}")
        message(FATAL_ERROR "CGAI_MUTATION_SOURCE_FILE must name an existing C source candidate")
    endif()
endif()
set(LIFE_NUMERICAL_SOURCES
    src/core/error.c
    src/core/model_random.c
    ${LIFE_GAMEPLAY_MODEL_SOURCE}
    src/gameplay/gameplay_session.c
    src/gameplay/gameplay_forward.c
    src/gameplay/gameplay_backward.c
    src/gameplay/gameplay_codec.c)
set(LIFE_RUNTIME_SOURCES
    src/life/life_world.c
    src/life/life_policy.c
    src/life/life_optimizer.c
    src/life/life_training.c
    src/life/life_events.c
    src/life/life_io.c
    src/life/life_api.c
    src/life/life_context.c
    src/life/life_context_feedback.c
    src/life/life_context_io.c
    src/life/life_domain.c
    src/life/life_domain_round.c
    src/life/life_domain_state.c
    src/life/life_domain_io.c
    src/life/life_probe.c
    src/life/life_adapter.c
    src/life/life_npc_world.c
    src/life/life_npc.c
    src/life/life_npc_state.c
    src/life/life_npc_io.c
    src/life/life_npc_evaluation.c)
add_library(centroid_life STATIC ${LIFE_NUMERICAL_SOURCES} ${LIFE_RUNTIME_SOURCES})
add_library(centroid_life::centroid_life ALIAS centroid_life)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/public")
configure_file(include/centroid_life.h public/centroid_life.h COPYONLY)
configure_file(include/centroid_life_domain.h public/centroid_life_domain.h COPYONLY)
configure_file(include/centroid_life_npc.h public/centroid_life_npc.h COPYONLY)
set(LIFE_NPC_RECIPE_SOURCES ${LIFE_NUMERICAL_SOURCES} ${LIFE_RUNTIME_SOURCES}
    include/centroid_life.h include/centroid_life_domain.h include/centroid_life_npc.h
    src/life/life_npc.h src/life/life_npc_world.h src/life/life_adapter.h src/life/life_probe.h)
set(LIFE_NPC_RECIPE_IDENTITIES "native-life-npc-recipe1")
foreach(source IN LISTS LIFE_NPC_RECIPE_SOURCES)
    get_filename_component(source_path "${source}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    file(SHA256 "${source_path}" source_identity)
    string(APPEND LIFE_NPC_RECIPE_IDENTITIES "${source}:${source_identity}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_path}")
endforeach()
string(SHA256 LIFE_NPC_RECIPE_IDENTITY "${LIFE_NPC_RECIPE_IDENTITIES}")
target_compile_definitions(centroid_life PRIVATE
    CGAI_LIFE_NPC_RECIPE_SHA="${LIFE_NPC_RECIPE_IDENTITY}")
target_include_directories(centroid_life
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/public>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE include src src/life src/gameplay)
set_target_properties(centroid_life PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(centroid_life PUBLIC c_std_11)
cgai_enable_warnings(centroid_life)
if(NOT WIN32)
    target_link_libraries(centroid_life PUBLIC m)
endif()
cgai_enable_sanitizers(centroid_life PUBLIC)

add_executable(cgai_life tools/life/life_main.c)
target_include_directories(cgai_life PRIVATE include src src/life)
target_link_libraries(cgai_life PRIVATE centroid_life::centroid_life)
cgai_enable_warnings(cgai_life)

add_executable(cgai_life_probe tools/life/life_domain_main.c)
target_link_libraries(cgai_life_probe PRIVATE centroid_life::centroid_life)
cgai_enable_warnings(cgai_life_probe)
add_executable(cgai_life_npc tools/life/life_npc_main.c)
target_link_libraries(cgai_life_npc PRIVATE centroid_life::centroid_life)
cgai_enable_warnings(cgai_life_npc)

set(LIFE_CONTEXT_TOOL_SOURCES tools/context/context_main.c tools/context/context_repository.c)
add_library(centroid_context_repository STATIC tools/context/context_repository.c)
target_include_directories(centroid_context_repository PRIVATE include tools/context)
target_link_libraries(centroid_context_repository PUBLIC centroid_life::centroid_life)
cgai_enable_warnings(centroid_context_repository)
add_executable(cgai_context tools/context/context_main.c)
target_include_directories(cgai_context PRIVATE include tools/context)
target_link_libraries(cgai_context PRIVATE centroid_context_repository)
cgai_enable_warnings(cgai_context)

add_executable(cgai_life_example examples/life_training.c)
target_link_libraries(cgai_life_example PRIVATE centroid_life::centroid_life)
cgai_enable_warnings(cgai_life_example)

if(CGAI_BUILD_TESTS)
    enable_testing()
    add_test(NAME centroid_life_public_consumer COMMAND cgai_life_example)
    foreach(suite IN ITEMS world policy training io api events context feedback domain groups npc npc_domain npc_geometry)
        add_executable(centroid_life_${suite}_tests
            tools/life/tests/test_${suite}.c tests/test_utils.c)
        target_include_directories(centroid_life_${suite}_tests PRIVATE
            tests include src src/life)
        target_link_libraries(centroid_life_${suite}_tests PRIVATE centroid_life::centroid_life)
        cgai_enable_warnings(centroid_life_${suite}_tests)
        add_test(NAME centroid_life_${suite} COMMAND centroid_life_${suite}_tests
            "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
    add_executable(centroid_context_repository_tests tools/context/tests/test_repository.c
        tests/test_utils.c)
    target_include_directories(centroid_context_repository_tests PRIVATE
        tests include src src/life tools/context)
    target_link_libraries(centroid_context_repository_tests PRIVATE centroid_context_repository)
    cgai_enable_warnings(centroid_context_repository_tests)
    add_test(NAME centroid_context_repository COMMAND centroid_context_repository_tests
        "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(centroid_life_cli_tests tools/life/tests/test_cli.c)
    cgai_enable_warnings(centroid_life_cli_tests)
    add_test(NAME centroid_life_cli COMMAND centroid_life_cli_tests
        "$<TARGET_FILE:cgai_life>" "${CMAKE_CURRENT_BINARY_DIR}/cli-check")
    set_tests_properties(centroid_life_cli PROPERTIES TIMEOUT 180)
    include(cmake/InferenceTests.cmake)
    target_include_directories(centroid_life_npc_geometry_tests PRIVATE tools/npc)
    target_link_libraries(centroid_life_npc_geometry_tests PRIVATE centroid_npc_v3_geometry)
endif()

include(cmake/Evolution.cmake)
include(cmake/Lint.cmake)
include(CMakePackageConfigHelpers)
set(LIFE_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/centroid_life)
configure_package_config_file(cmake/centroid_lifeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/centroid_lifeConfig.cmake
    INSTALL_DESTINATION ${LIFE_CMAKE_INSTALL_DIR})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/centroid_lifeConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS centroid_life cgai_life cgai_context cgai_life_probe cgai_life_npc EXPORT centroid_lifeTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/centroid_life.h include/centroid_life_domain.h include/centroid_life_npc.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT centroid_lifeTargets NAMESPACE centroid_life::
    DESTINATION ${LIFE_CMAKE_INSTALL_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/centroid_lifeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/centroid_lifeConfigVersion.cmake
    DESTINATION ${LIFE_CMAKE_INSTALL_DIR})

if(CGAI_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(LIFE_DOXYGEN_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/include/centroid_life.h")
    set(LIFE_DOXYGEN_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/docs")
    configure_file(Doxyfile.in Doxyfile @ONLY)
    add_custom_target(docs COMMAND "${DOXYGEN_EXECUTABLE}"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" VERBATIM)
endif()
