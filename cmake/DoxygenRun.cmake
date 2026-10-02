# Doxygen 1.9.8 can ignore a filter launch failure and still exit successfully.
# QUIET documentation generation must produce neither warnings nor filter stderr.
foreach(required IN ITEMS DOXYGEN_EXECUTABLE DOXYGEN_CONFIG Python3_EXECUTABLE
        DOXYGEN_INPUT_DIR DOXYGEN_XML_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
execute_process(COMMAND "${DOXYGEN_EXECUTABLE}" "${DOXYGEN_CONFIG}"
    RESULT_VARIABLE doxygen_status OUTPUT_VARIABLE doxygen_output ERROR_VARIABLE doxygen_error)
if(NOT "${doxygen_output}" STREQUAL "")
    message("${doxygen_output}")
endif()
if(NOT "${doxygen_status}" STREQUAL "0" OR NOT "${doxygen_error}" STREQUAL "")
    message(FATAL_ERROR "Doxygen failed (exit ${doxygen_status}):\n${doxygen_error}")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/docs/verify_doxygen.py"
    --include "${DOXYGEN_INPUT_DIR}" --xml "${DOXYGEN_XML_DIR}"
    RESULT_VARIABLE verification_status OUTPUT_VARIABLE verification_output
    ERROR_VARIABLE verification_error)
if(NOT "${verification_status}" STREQUAL "0" OR NOT "${verification_error}" STREQUAL "")
    message(FATAL_ERROR "Public API documentation verification failed:\n${verification_error}")
endif()
message("${verification_output}")
