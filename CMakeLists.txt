cmake_minimum_required(VERSION 3.20)
project(centroid_gai VERSION 0.2.0 LANGUAGES C)

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

add_library(centroid_gai
    src/centroid_gai.c
    src/error.c
    src/file_utils.c
    src/model_generation.c
    src/model_generation_workspace.c
    src/model_decode.c
    src/model_encode.c
    src/model_file.c
    src/model_embedding.c
    src/model_centroid.c
    src/model_random.c
    src/model_sampling.c
    src/model_training.c
    src/model_composition.c
    src/tokenizer.c
    src/vocabulary.c
)
add_library(centroid_gai::centroid_gai ALIAS centroid_gai)
set_target_properties(centroid_gai PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(centroid_gai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(centroid_gai PUBLIC c_std_11)

cgai_enable_warnings(centroid_gai)

add_library(centroid_gai_abi SHARED
    src/abi.c
    src/abi_model.c
    src/abi_config.c
    src/abi_buffer.c
    src/abi_generation.c
    src/abi_metadata.c
    src/abi_serialization.c
    src/abi_inspection.c
)
add_library(centroid_gai::abi ALIAS centroid_gai_abi)
target_compile_features(centroid_gai_abi PUBLIC c_std_11)
target_compile_definitions(centroid_gai_abi PRIVATE CGAI_ABI_BUILD)
target_include_directories(centroid_gai_abi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
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

add_executable(cgai src/main.c src/cli.c src/cli_arguments.c src/cli_training.c src/cli_generation.c)
cgai_enable_warnings(cgai)
target_link_libraries(cgai PRIVATE centroid_gai::centroid_gai)
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
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
    configure_file(Doxyfile.in Doxyfile @ONLY)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Centroid-GAI API documentation"
        VERBATIM
    )
endif()

if(CGAI_BUILD_TESTS)
    enable_testing()
    add_executable(centroid_gai_tests tests/test_centroid_gai.c tests/test_utils.c)
    target_link_libraries(centroid_gai_tests PRIVATE centroid_gai::centroid_gai)
    cgai_enable_warnings(centroid_gai_tests)
    add_test(NAME centroid_gai_tests COMMAND centroid_gai_tests)

    add_executable(centroid_gai_abi_tests tests/test_abi.c tests/test_utils.c)
    target_link_libraries(centroid_gai_abi_tests PRIVATE centroid_gai::abi)
    cgai_enable_warnings(centroid_gai_abi_tests)
    add_test(NAME centroid_gai_abi_tests COMMAND centroid_gai_abi_tests)

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
