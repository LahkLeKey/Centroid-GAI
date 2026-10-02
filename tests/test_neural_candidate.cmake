# Verify finite JSON evidence, frozen vocabulary, and deterministic C11 proposals.
if(NOT DEFINED CGAI_EXE OR NOT EXISTS "${CGAI_EXE}")
    message(FATAL_ERROR "CGAI_EXE must name the compiled cgai executable")
endif()

function(cgai_candidate_run expected result)
    execute_process(COMMAND "${CGAI_EXE}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL expected)
        message(FATAL_ERROR "${ARGN}: status ${status}; ${output}${error}")
    endif()
    if(expected EQUAL 0)
        string(JSON kind ERROR_VARIABLE json_error TYPE "${output}")
        if(NOT json_error STREQUAL "NOTFOUND" OR NOT kind STREQUAL "OBJECT")
            message(FATAL_ERROR "${ARGN}: expected one JSON object; ${output}${error}")
        endif()
    elseif(NOT output STREQUAL "")
        message(FATAL_ERROR "${ARGN}: failure printed success evidence: ${output}")
    endif()
    set("${result}" "${output}" PARENT_SCOPE)
endfunction()

function(cgai_candidate_same_metrics document stage split score)
    foreach(key IN ITEMS tokens unknownTokens crossEntropy accuracy)
        string(JSON measured GET "${document}" "${stage}" "${split}" "${key}")
        string(JSON scored GET "${score}" "${key}")
        if(NOT measured STREQUAL scored)
            message(FATAL_ERROR "${stage}/${split}/${key}: candidate ${measured}, score ${scored}")
        endif()
    endforeach()
endfunction()

function(cgai_candidate_unchanged path expected)
    file(SHA256 "${path}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "failed candidate changed ${path}")
    endif()
endfunction()

set(cgai_dir "${CMAKE_CURRENT_BINARY_DIR}/neural-candidate")
file(MAKE_DIRECTORY "${cgai_dir}")
set(cgai_train "${cgai_dir}/train.txt")
set(cgai_development "${cgai_dir}/development.txt")
set(cgai_unknown "${cgai_dir}/unknown.txt")
set(cgai_empty "${cgai_dir}/empty.txt")
set(cgai_missing "${cgai_dir}/missing.txt")
set(cgai_initial "${cgai_dir}/initial.checkpoint")
set(cgai_first "${cgai_dir}/first.checkpoint")
set(cgai_second "${cgai_dir}/second.checkpoint")
set(cgai_combined "${cgai_dir}/combined.checkpoint")
set(cgai_ineffective "${cgai_dir}/ineffective.checkpoint")
set(cgai_preserved "${cgai_dir}/preserved.checkpoint")
string(REPEAT "a b left b a right " 16 cgai_training_text)
string(REPEAT "b a right a b left " 7 cgai_development_text)
file(WRITE "${cgai_train}" "${cgai_training_text}\n")
file(WRITE "${cgai_development}" "${cgai_development_text}unseen\n")
file(WRITE "${cgai_unknown}" "a b left unseen\n")
file(WRITE "${cgai_empty}" "")
file(WRITE "${cgai_preserved}" "preserve this existing destination\n")
file(REMOVE "${cgai_missing}")
file(SHA256 "${cgai_preserved}" cgai_preserved_hash)

execute_process(COMMAND "${CGAI_EXE}" neural-init "${cgai_train}" "${cgai_initial}"
    RESULT_VARIABLE cgai_status OUTPUT_VARIABLE cgai_output ERROR_VARIABLE cgai_error)
if(NOT cgai_status EQUAL 0)
    message(FATAL_ERROR "initialization failed: ${cgai_output}${cgai_error}")
endif()
file(SHA256 "${cgai_initial}" cgai_initial_hash)

# Parent and candidate evidence must match independent read-only scoring exactly.
cgai_candidate_run(0 cgai_train_before neural-score "${cgai_initial}" "${cgai_train}")
cgai_candidate_run(0 cgai_dev_before neural-score "${cgai_initial}" "${cgai_development}")
cgai_candidate_run(0 cgai_first_json neural-candidate "${cgai_initial}" "${cgai_train}"
    "${cgai_development}" "${cgai_first}")
cgai_candidate_same_metrics("${cgai_first_json}" before training "${cgai_train_before}")
cgai_candidate_same_metrics("${cgai_first_json}" before development "${cgai_dev_before}")
cgai_candidate_run(0 cgai_train_after neural-score "${cgai_first}" "${cgai_train}")
cgai_candidate_run(0 cgai_dev_after neural-score "${cgai_first}" "${cgai_development}")
cgai_candidate_same_metrics("${cgai_first_json}" after training "${cgai_train_after}")
cgai_candidate_same_metrics("${cgai_first_json}" after development "${cgai_dev_after}")
cgai_candidate_unchanged("${cgai_initial}" "${cgai_initial_hash}")
string(JSON cgai_version GET "${cgai_first_json}" version)
string(JSON cgai_epochs GET "${cgai_first_json}" epochs)
string(JSON cgai_steps GET "${cgai_first_json}" steps)
string(JSON cgai_unknown_count GET "${cgai_first_json}" after development unknownTokens)
string(JSON cgai_train_unknown GET "${cgai_first_json}" after training unknownTokens)
if(NOT cgai_version EQUAL 1 OR NOT cgai_epochs EQUAL 1 OR NOT cgai_steps EQUAL 97 OR
    NOT cgai_unknown_count EQUAL 1 OR NOT cgai_train_unknown EQUAL 0)
    message(FATAL_ERROR "unexpected candidate progress or vocabulary coverage: ${cgai_first_json}")
endif()

# Split calls must preserve optimizer and shuffle state across process boundaries.
cgai_candidate_run(0 cgai_second_json neural-candidate "${cgai_first}" "${cgai_train}"
    "${cgai_development}" "${cgai_second}" 1 0.015)
cgai_candidate_run(0 cgai_combined_json neural-candidate "${cgai_initial}" "${cgai_train}"
    "${cgai_development}" "${cgai_combined}" 2 0.015)
file(SHA256 "${cgai_second}" cgai_second_hash)
file(SHA256 "${cgai_combined}" cgai_combined_hash)
if(NOT cgai_second_hash STREQUAL cgai_combined_hash)
    message(FATAL_ERROR "split candidate optimization differs from combined full epochs")
endif()
string(JSON cgai_epochs GET "${cgai_combined_json}" epochs)
string(JSON cgai_steps GET "${cgai_combined_json}" steps)
if(NOT cgai_epochs EQUAL 2 OR NOT cgai_steps EQUAL 194)
    message(FATAL_ERROR "combined candidate has incorrect continuation counters")
endif()

# Candidate creation does not implement the separate promotion/improvement policy.
cgai_candidate_run(0 cgai_ineffective_json neural-candidate "${cgai_initial}" "${cgai_train}"
    "${cgai_development}" "${cgai_ineffective}" 1 1e-300)
cgai_candidate_same_metrics("${cgai_ineffective_json}" after training "${cgai_train_before}")
cgai_candidate_same_metrics("${cgai_ineffective_json}" after development "${cgai_dev_before}")

# Unknown training words are refused before saving, while unknown evaluation words are scored.
cgai_candidate_run(1 cgai_failed neural-candidate "${cgai_initial}" "${cgai_unknown}"
    "${cgai_development}" "${cgai_preserved}")
cgai_candidate_unchanged("${cgai_preserved}" "${cgai_preserved_hash}")
cgai_candidate_run(0 cgai_unknown_score neural-score "${cgai_initial}" "${cgai_unknown}")
string(JSON cgai_unknown_count GET "${cgai_unknown_score}" unknownTokens)
if(NOT cgai_unknown_count EQUAL 1)
    message(FATAL_ERROR "checkpoint score omitted unknown-token coverage")
endif()

# Invalid inputs and options emit no JSON or destination changes.
foreach(cgai_epochs IN ITEMS 0 10001 -1 1.5)
    cgai_candidate_run(2 cgai_failed neural-candidate "${cgai_initial}" "${cgai_train}"
        "${cgai_development}" "${cgai_preserved}" "${cgai_epochs}")
endforeach()
foreach(cgai_rate IN ITEMS 0 1.01 nan inf -0.01)
    cgai_candidate_run(2 cgai_failed neural-candidate "${cgai_initial}" "${cgai_train}"
        "${cgai_development}" "${cgai_preserved}" 1 "${cgai_rate}")
endforeach()
cgai_candidate_run(2 cgai_failed neural-candidate)
cgai_candidate_run(2 cgai_failed neural-score)
cgai_candidate_run(2 cgai_failed neural-score "${cgai_initial}")
cgai_candidate_run(1 cgai_failed neural-score "${cgai_missing}" "${cgai_train}")
cgai_candidate_run(1 cgai_failed neural-score "${cgai_initial}" "${cgai_empty}")
cgai_candidate_run(1 cgai_failed neural-score "${cgai_initial}" "${cgai_missing}")
cgai_candidate_run(1 cgai_failed neural-candidate "${cgai_missing}" "${cgai_train}"
    "${cgai_development}" "${cgai_preserved}")
cgai_candidate_run(1 cgai_failed neural-candidate "${cgai_initial}" "${cgai_train}"
    "${cgai_empty}" "${cgai_preserved}")
cgai_candidate_run(1 cgai_failed neural-candidate "${cgai_initial}" "${cgai_missing}"
    "${cgai_development}" "${cgai_preserved}")
cgai_candidate_unchanged("${cgai_preserved}" "${cgai_preserved_hash}")

file(REMOVE "${cgai_train}" "${cgai_development}" "${cgai_unknown}" "${cgai_empty}"
    "${cgai_initial}" "${cgai_first}" "${cgai_second}" "${cgai_combined}"
    "${cgai_ineffective}" "${cgai_preserved}")
