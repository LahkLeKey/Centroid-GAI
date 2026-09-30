# Exercise the complete neural CLI with isolated training and held-out files.
function(cgai_check_neural expected pattern)
    execute_process(COMMAND "${CGAI_EXE}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL expected)
        message(FATAL_ERROR "${ARGN}: status ${status}; ${output}${error}")
    endif()
    if(NOT "${output}${error}" MATCHES "${pattern}")
        message(FATAL_ERROR "${ARGN}: expected ${pattern}; got ${output}${error}")
    endif()
endfunction()

set(cgai_train "${CMAKE_CURRENT_BINARY_DIR}/neural-cli-train.txt")
set(cgai_heldout "${CMAKE_CURRENT_BINARY_DIR}/neural-cli-heldout.txt")
set(cgai_artifact "${CMAKE_CURRENT_BINARY_DIR}/neural-cli-model.cgnn")
file(WRITE "${cgai_train}" "a b left b a right a b left b a right")
file(WRITE "${cgai_heldout}" "b a right a b left unseen")
cgai_check_neural(0 "training after:.*cross-entropy=.*held-out after:.*unknown=1"
    neural-train "${cgai_train}" "${cgai_artifact}" 3 0.01 "${cgai_heldout}")
cgai_check_neural(0 "held-out:.*cross-entropy=.*unknown=1"
    neural-evaluate "${cgai_artifact}" "${cgai_heldout}")
cgai_check_neural(0 ".*" neural-generate "${cgai_artifact}" "a b" 8 0 42)
cgai_check_neural(2 ".+" neural-train "${cgai_train}" "${cgai_artifact}" 0)
cgai_check_neural(2 ".+" neural-train "${cgai_train}" "${cgai_artifact}" 1 nan)
cgai_check_neural(2 ".+" neural-generate "${cgai_artifact}" "a" 8 -1)
cgai_check_neural(2 "Usage:" neural-evaluate)
file(REMOVE "${cgai_train}" "${cgai_heldout}" "${cgai_artifact}")
