# Standalone Clang invocations also work with generators without compilation databases.
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
file(GLOB_RECURSE CGAI_CORE_LINT_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.c" "${PROJECT_SOURCE_DIR}/tests/*.c")
file(GLOB_RECURSE CGAI_NODE_LINT_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/persistence/prisma-postgres/native/*.c")
file(GLOB_RECURSE CGAI_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.c" "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/tests/*.c" "${PROJECT_SOURCE_DIR}/tests/*.h"
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/persistence/prisma-postgres/native/*.c"
    "${PROJECT_SOURCE_DIR}/persistence/prisma-postgres/native/*.h")

# node-gyp keeps downloaded headers under the running Node version.
find_program(CGAI_NODE_EXECUTABLE NAMES node)
if(CGAI_NODE_EXECUTABLE)
    execute_process(COMMAND "${CGAI_NODE_EXECUTABLE}" --version
        OUTPUT_VARIABLE CGAI_NODE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "^v" "" CGAI_NODE_VERSION "${CGAI_NODE_VERSION}")
endif()
find_path(CGAI_NODE_INCLUDE_DIR node_api.h
    HINTS "$ENV{LOCALAPPDATA}/node-gyp/Cache/${CGAI_NODE_VERSION}/include/node"
          "$ENV{HOME}/.cache/node-gyp/${CGAI_NODE_VERSION}/include/node"
          "$ENV{HOME}/Library/Caches/node-gyp/${CGAI_NODE_VERSION}/include/node"
    PATH_SUFFIXES node include/node
    DOC "Directory containing Node-API headers for addon linting")

if(CLANG_TIDY_EXECUTABLE)
    set(CGAI_LINT_FLAGS -x c -std=c11 -Wall -Wextra -Wpedantic -Wconversion
        -DCGAI_ABI_BUILD -D_CRT_SECURE_NO_WARNINGS
        "-I${PROJECT_SOURCE_DIR}/include" "-I${PROJECT_SOURCE_DIR}/src")
    add_custom_target(cgai_lint_core
        COMMAND "${CLANG_TIDY_EXECUTABLE}" ${CGAI_CORE_LINT_SOURCES} -- ${CGAI_LINT_FLAGS}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM
        COMMENT "Linting all core, ABI, CLI, and test C sources as ISO C11")
    if(CGAI_NODE_INCLUDE_DIR)
        add_custom_target(cgai_lint_addon
            COMMAND "${CLANG_TIDY_EXECUTABLE}" ${CGAI_NODE_LINT_SOURCES} -- ${CGAI_LINT_FLAGS}
                -DNAPI_VERSION=10 "-isystem${CGAI_NODE_INCLUDE_DIR}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM
            COMMENT "Linting all Node-API C sources as ISO C11")
    else()
        add_custom_target(cgai_lint_addon
            COMMAND ${CMAKE_COMMAND} -E echo
                "Addon lint requires Node headers: set CGAI_NODE_INCLUDE_DIR to the directory containing node_api.h."
            COMMAND ${CMAKE_COMMAND} -E false VERBATIM)
    endif()
    add_custom_target(cgai_lint DEPENDS cgai_lint_core cgai_lint_addon)
endif()

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(cgai_format_check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${CGAI_FORMAT_SOURCES}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM
        COMMENT "Checking formatting of all owned C sources and headers")
endif()
