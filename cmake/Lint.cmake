find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
set(LIFE_LINT_SOURCES ${LIFE_NUMERICAL_SOURCES} ${LIFE_RUNTIME_SOURCES}
    tools/life/life_main.c tools/life/life_domain_main.c tools/life/life_npc_main.c ${LIFE_CONTEXT_TOOL_SOURCES}
    examples/life_training.c tests/test_utils.c)
if(CGAI_BUILD_EVOLUTION_TOOLS)
    list(APPEND LIFE_LINT_SOURCES ${EVOLVE_SOURCES})
endif()
if(CGAI_BUILD_TESTS)
    file(GLOB LIFE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/life/tests/*.c")
    get_property(VERIFICATION_SOURCES GLOBAL PROPERTY CGAI_VERIFICATION_SOURCES)
    list(APPEND LIFE_LINT_SOURCES ${LIFE_TEST_SOURCES} ${VERIFICATION_SOURCES})
    list(APPEND LIFE_LINT_SOURCES tools/context/tests/test_repository.c)
    if(CGAI_BUILD_EVOLUTION_TOOLS)
        list(APPEND LIFE_LINT_SOURCES ${EVOLVE_TEST_SOURCES})
    endif()
endif()
list(REMOVE_DUPLICATES LIFE_LINT_SOURCES)
file(GLOB LIFE_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE LIFE_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
set(LIFE_FORMAT_SOURCES ${LIFE_LINT_SOURCES} ${LIFE_PUBLIC_HEADERS} ${LIFE_PRIVATE_HEADERS})
file(GLOB LIFE_CONTEXT_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/context/*.h")
list(APPEND LIFE_FORMAT_SOURCES ${LIFE_CONTEXT_HEADERS})
if(CGAI_BUILD_EVOLUTION_TOOLS)
    file(GLOB EVOLVE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/evolve/*.h")
    list(APPEND LIFE_FORMAT_SOURCES ${EVOLVE_HEADERS})
endif()
if(CGAI_BUILD_TESTS)
    file(GLOB_RECURSE VERIFICATION_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bark/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/tools/npc/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/npc_v2/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/tools/npc_v3/*.h")
    list(APPEND LIFE_FORMAT_SOURCES ${VERIFICATION_HEADERS})
endif()
if(CLANG_TIDY_EXECUTABLE)
    set(LIFE_LINT_COMMANDS)
    foreach(source IN LISTS LIFE_LINT_SOURCES)
        set(LIFE_SOURCE_PRIVATE_INCLUDES)
        if(source IN_LIST EVOLVE_SOURCES OR source IN_LIST EVOLVE_TEST_SOURCES)
            list(APPEND LIFE_SOURCE_PRIVATE_INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/evolve"
                "-I${EVOLVE_BUILD_INCLUDE_DIR}")
        elseif(source IN_LIST BARK_VERIFICATION_SOURCES)
            list(APPEND LIFE_SOURCE_PRIVATE_INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/bark")
        elseif(source IN_LIST GAMEPLAY_VERIFICATION_SOURCES)
            list(APPEND LIFE_SOURCE_PRIVATE_INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/gameplay")
        elseif(source MATCHES "test_npc_geometry[.]c$")
            list(APPEND LIFE_SOURCE_PRIVATE_INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc")
        elseif(source IN_LIST NPC_V3_VERIFICATION_SOURCES)
            list(APPEND LIFE_SOURCE_PRIVATE_INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc_v3"
                "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc")
        elseif(source IN_LIST NPC_V2_VERIFICATION_SOURCES)
            list(APPEND LIFE_SOURCE_PRIVATE_INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc_v2"
                "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc")
        elseif(source IN_LIST NPC_V1_VERIFICATION_SOURCES)
            list(APPEND LIFE_SOURCE_PRIVATE_INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc")
        endif()
        list(APPEND LIFE_LINT_COMMANDS COMMAND "${CLANG_TIDY_EXECUTABLE}" "${source}" --
            -x c -std=c11 -Wall -Wextra -Wpedantic -Wconversion -D_CRT_SECURE_NO_WARNINGS
            "-I${CMAKE_CURRENT_SOURCE_DIR}/include" "-I${CMAKE_CURRENT_SOURCE_DIR}/src"
            "-I${CMAKE_CURRENT_SOURCE_DIR}/src/life" "-I${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay"
            "-I${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/context"
            ${LIFE_SOURCE_PRIVATE_INCLUDES})
    endforeach()
    if(CGAI_BUILD_TESTS)
        # The shared replay engine is compiled against both distinct profile contracts.
        list(APPEND LIFE_LINT_COMMANDS COMMAND "${CLANG_TIDY_EXECUTABLE}"
            tools/npc/npc_reference.c -- -x c -std=c11 -Wall -Wextra -Wpedantic -Wconversion
            -D_CRT_SECURE_NO_WARNINGS "-I${CMAKE_CURRENT_SOURCE_DIR}/src"
            "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc_v2" "-I${CMAKE_CURRENT_SOURCE_DIR}/tools/npc")
    endif()
    add_custom_target(life_lint ${LIFE_LINT_COMMANDS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
    add_custom_target(cgai_lint DEPENDS life_lint)
endif()
if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(life_format_check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${LIFE_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
    add_custom_target(cgai_format_check DEPENDS life_format_check)
endif()
