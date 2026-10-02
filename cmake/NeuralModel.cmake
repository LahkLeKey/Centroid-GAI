# Reviewable model artifacts live in source; training proposals live in the build.
set(CGAI_MODEL_DIR "${PROJECT_SOURCE_DIR}/models/neural/order-v1")
set(CGAI_MODEL_BUILD_DIR "${PROJECT_BINARY_DIR}/model")
set(CGAI_MODEL_STEP_EPOCHS "1" CACHE STRING "Epochs for the next neural model proposal")
set(CGAI_MODEL_STEP_RATE "0.015" CACHE STRING "Learning rate for the next neural model proposal")

function(cgai_add_model_target target action description)
    add_custom_target(${target}
        COMMAND "${CMAKE_COMMAND}"
            "-DCGAI_EXE=$<TARGET_FILE:cgai>"
            "-DCGAI_MODEL_DIR=${CGAI_MODEL_DIR}"
            "-DCGAI_MODEL_BUILD_DIR=${CGAI_MODEL_BUILD_DIR}"
            "-DCGAI_MODEL_ACTION=${action}"
            "-DCGAI_MODEL_STEP_EPOCHS=${CGAI_MODEL_STEP_EPOCHS}"
            "-DCGAI_MODEL_STEP_RATE=${CGAI_MODEL_STEP_RATE}"
            -P "${PROJECT_SOURCE_DIR}/cmake/NeuralModelRun.cmake"
        DEPENDS cgai
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        COMMENT "${description}"
        VERBATIM)
endfunction()

cgai_add_model_target(model_validate validate
    "Validating tracked neural model integrity and held-out evaluation")
cgai_add_model_target(model_verify verify
    "Replaying tracked neural model and verifying every checkpoint byte")
cgai_add_model_target(model_step step
    "Proposing an improved neural checkpoint in the build directory")
cgai_add_model_target(model_reproduce reproduce
    "Reproducing the tracked neural model from its fixed training recipe")
