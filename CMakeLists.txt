cmake_minimum_required(VERSION 3.20)
project(centroid_gai VERSION 0.2.0 LANGUAGES C)

option(CGAI_BUILD_TESTS "Build the test suite" ON)
option(CGAI_BUILD_DOCS "Build API documentation with Doxygen" OFF)
option(CGAI_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)

add_library(centroid_gai
    src/centroid_gai.c
    src/error.c
    src/file_utils.c
    src/model_generation.c
    src/model_decode.c
    src/model_encode.c
    src/model_file.c
    src/model_math.c
    src/model_sampling.c
    src/model_training.c
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

if(CLANG_TIDY_EXECUTABLE)
    set(CGAI_CLANG_TIDY_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/abi.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/abi_config.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/abi_model.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/centroid_gai.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/cli.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/error.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/file_utils.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model_decode.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model_encode.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model_file.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model_generation.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model_math.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model_sampling.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model_training.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tokenizer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vocabulary.c
    )
    add_custom_target(cgai_lint
        COMMAND ${CLANG_TIDY_EXECUTABLE} -p ${CMAKE_BINARY_DIR} ${CGAI_CLANG_TIDY_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running clang-tidy C11 readability checks"
        VERBATIM
    )
endif()

if(MSVC)
    target_compile_options(centroid_gai PRIVATE /W4 /permissive-)
else()
    target_compile_options(centroid_gai PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_library(centroid_gai_abi SHARED src/abi.c src/abi_model.c src/abi_config.c)
add_library(centroid_gai::abi ALIAS centroid_gai_abi)
target_compile_features(centroid_gai_abi PUBLIC c_std_11)
target_compile_definitions(centroid_gai_abi PRIVATE CGAI_ABI_BUILD)
target_include_directories(centroid_gai_abi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(centroid_gai_abi PRIVATE centroid_gai::centroid_gai)
if(MSVC)
    target_compile_options(centroid_gai_abi PRIVATE /W4 /permissive-)
else()
    target_compile_options(centroid_gai_abi PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(WIN32)
    target_compile_definitions(centroid_gai PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(centroid_gai PUBLIC m)
endif()

if(CGAI_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(centroid_gai PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(centroid_gai PUBLIC -fsanitize=address,undefined)
endif()

add_executable(cgai src/main.c src/cli.c)
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
    add_test(NAME centroid_gai_tests COMMAND centroid_gai_tests)

    add_executable(centroid_gai_abi_tests tests/test_abi.c tests/test_utils.c)
    target_link_libraries(centroid_gai_abi_tests PRIVATE centroid_gai::abi)
    add_test(NAME centroid_gai_abi_tests COMMAND centroid_gai_abi_tests)

    add_executable(centroid_gai_unit_tests
        tests/test_runner.c
        tests/test_tokenizer.c
        tests/test_vocabulary.c
        tests/test_model_math.c
        tests/test_model_io.c
        tests/test_sampling.c
        tests/test_file_utils.c
        tests/test_utils.c
    )
    target_include_directories(centroid_gai_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(centroid_gai_unit_tests PRIVATE centroid_gai::centroid_gai)
    add_test(NAME centroid_gai_unit_tests COMMAND centroid_gai_unit_tests)
endif()
