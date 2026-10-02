# Exercise resumable neural checkpoints through the public C11 CLI.
if(NOT DEFINED CGAI_EXE OR NOT EXISTS "${CGAI_EXE}")
    message(FATAL_ERROR "CGAI_EXE must name the compiled cgai executable")
endif()

function(cgai_workflow_check expected)
    execute_process(COMMAND "${CGAI_EXE}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(expected STREQUAL "failure")
        if(status EQUAL 0)
            message(FATAL_ERROR "${ARGN}: unexpectedly succeeded; ${output}${error}")
        endif()
    elseif(NOT status EQUAL expected)
        message(FATAL_ERROR "${ARGN}: status ${status}; ${output}${error}")
    endif()
endfunction()

function(cgai_workflow_same first second)
    if(NOT EXISTS "${first}" OR NOT EXISTS "${second}")
        message(FATAL_ERROR "expected checkpoints ${first} and ${second}")
    endif()
    file(SHA256 "${first}" first_hash)
    file(SHA256 "${second}" second_hash)
    if(NOT first_hash STREQUAL second_hash)
        message(FATAL_ERROR "checkpoint bytes differ: ${first} and ${second}")
    endif()
endfunction()

function(cgai_workflow_absent path)
    if(EXISTS "${path}")
        message(FATAL_ERROR "failed command published ${path}")
    endif()
endfunction()

set(cgai_dir "${CMAKE_CURRENT_BINARY_DIR}/neural-workflow")
file(MAKE_DIRECTORY "${cgai_dir}")
set(cgai_train "${cgai_dir}/train.txt")
set(cgai_heldout "${cgai_dir}/heldout.txt")
set(cgai_initial "${cgai_dir}/initial.checkpoint")
set(cgai_duplicate "${cgai_dir}/duplicate.checkpoint")
set(cgai_first "${cgai_dir}/first.checkpoint")
set(cgai_second "${cgai_dir}/second.checkpoint")
set(cgai_combined "${cgai_dir}/combined.checkpoint")
set(cgai_replayed "${cgai_dir}/replayed.checkpoint")
set(cgai_rejected "${cgai_dir}/rejected.checkpoint")
set(cgai_exported "${cgai_dir}/exported.cgnn")
set(cgai_missing "${cgai_dir}/missing.txt")

# Different lengths and starting phases keep held-out data separate from training.
string(REPEAT "a b left b a right " 16 cgai_training_text)
string(REPEAT "b a right a b left " 7 cgai_heldout_text)
file(WRITE "${cgai_train}" "${cgai_training_text}\n")
file(WRITE "${cgai_heldout}" "${cgai_heldout_text}\n")
file(REMOVE "${cgai_rejected}" "${cgai_missing}")

# Fixed initialization and resumed Adam/shuffle state must be byte reproducible.
cgai_workflow_check(0 neural-init "${cgai_train}" "${cgai_initial}")
cgai_workflow_check(0 neural-init "${cgai_train}" "${cgai_duplicate}")
cgai_workflow_same("${cgai_initial}" "${cgai_duplicate}")
cgai_workflow_check(0 neural-step "${cgai_initial}" "${cgai_train}"
    "${cgai_heldout}" "${cgai_first}")
cgai_workflow_check(0 neural-step "${cgai_first}" "${cgai_train}"
    "${cgai_heldout}" "${cgai_second}")
cgai_workflow_check(0 neural-step "${cgai_initial}" "${cgai_train}"
    "${cgai_heldout}" "${cgai_combined}" 2 0.015)
cgai_workflow_same("${cgai_second}" "${cgai_combined}")

# Replay must verify the source state before it publishes the reconstructed file.
cgai_workflow_check(0 neural-replay "${cgai_second}" "${cgai_train}"
    "${cgai_replayed}")
cgai_workflow_same("${cgai_second}" "${cgai_replayed}")
cgai_workflow_check(0 neural-export "${cgai_second}" "${cgai_exported}")
cgai_workflow_check(0 neural-evaluate "${cgai_exported}" "${cgai_heldout}")
cgai_workflow_check(failure neural-replay "${cgai_second}" "${cgai_train}"
    "${cgai_rejected}" 0.0075)
cgai_workflow_absent("${cgai_rejected}")

# Invalid settings and unreadable inputs must leave no candidate checkpoint.
cgai_workflow_check(2 neural-step "${cgai_initial}" "${cgai_train}"
    "${cgai_heldout}" "${cgai_rejected}" 0)
cgai_workflow_absent("${cgai_rejected}")
cgai_workflow_check(2 neural-step "${cgai_initial}" "${cgai_train}"
    "${cgai_heldout}" "${cgai_rejected}" 1 nan)
cgai_workflow_absent("${cgai_rejected}")
cgai_workflow_check(failure neural-init "${cgai_missing}" "${cgai_rejected}")
cgai_workflow_absent("${cgai_rejected}")
cgai_workflow_check(failure neural-step "${cgai_missing}" "${cgai_train}"
    "${cgai_heldout}" "${cgai_rejected}")
cgai_workflow_absent("${cgai_rejected}")
cgai_workflow_check(failure neural-replay "${cgai_second}" "${cgai_missing}"
    "${cgai_rejected}")
cgai_workflow_absent("${cgai_rejected}")

# A numerically ineffective positive rate cannot pass strict loss improvement.
# Preserve an existing destination even when the training/evaluation gate rejects.
file(WRITE "${cgai_rejected}" "preserve existing checkpoint\n")
file(SHA256 "${cgai_rejected}" cgai_preserved_hash)
cgai_workflow_check(failure neural-step "${cgai_initial}" "${cgai_train}"
    "${cgai_heldout}" "${cgai_rejected}" 1 1e-300)
file(SHA256 "${cgai_rejected}" cgai_rejected_hash)
if(NOT cgai_preserved_hash STREQUAL cgai_rejected_hash)
    message(FATAL_ERROR "rejected training step overwrote its destination")
endif()

file(REMOVE "${cgai_train}" "${cgai_heldout}" "${cgai_initial}"
    "${cgai_duplicate}" "${cgai_first}" "${cgai_second}"
    "${cgai_combined}" "${cgai_replayed}" "${cgai_rejected}" "${cgai_exported}")
