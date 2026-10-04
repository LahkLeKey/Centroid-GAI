# Retained backends are private verification dependencies, never installed APIs.
function(cgai_private_component target)
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} PRIVATE src)
    target_link_libraries(${target} PRIVATE centroid_life::centroid_life)
    cgai_enable_warnings(${target})
    set_property(GLOBAL APPEND PROPERTY CGAI_VERIFICATION_SOURCES ${ARGN})
endfunction()

function(cgai_inference_test suite)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBRARIES;INCLUDES;ARGUMENTS" ${ARGN})
    add_executable(centroid_${suite}_tests ${TEST_SOURCES} tests/test_utils.c)
    target_include_directories(centroid_${suite}_tests PRIVATE tests src ${TEST_INCLUDES})
    target_link_libraries(centroid_${suite}_tests PRIVATE ${TEST_LIBRARIES})
    cgai_enable_warnings(centroid_${suite}_tests)
    add_test(NAME centroid_${suite} COMMAND centroid_${suite}_tests ${TEST_ARGUMENTS})
    set_tests_properties(centroid_${suite} PROPERTIES TIMEOUT 180)
    set_property(GLOBAL APPEND PROPERTY CGAI_VERIFICATION_SOURCES ${TEST_SOURCES})
endfunction()

cgai_private_component(centroid_private_io src/core/file_utils.c src/core/tokenizer.c)
cgai_private_component(centroid_count_backend
    src/core/vocabulary.c src/model/centroid_gai.c src/model/model_centroid.c
    src/model/model_composition.c src/model/model_decode.c src/model/model_embedding.c
    src/model/model_encode.c src/model/model_file.c src/model/model_generation.c
    src/model/model_generation_workspace.c src/model/model_sampling.c)
target_link_libraries(centroid_count_backend PRIVATE centroid_private_io)
cgai_private_component(centroid_neural_backend
    src/neural/neural_checkpoint.c src/neural/neural_evaluation.c src/neural/neural_file.c
    src/neural/neural_generation.c src/neural/neural_math.c src/neural/neural_model.c
    src/neural/neural_resources.c src/neural/neural_vocabulary.c)
target_link_libraries(centroid_neural_backend PRIVATE centroid_private_io)
cgai_private_component(centroid_chat_backend
    src/chat/chat_codec.c src/chat/chat_evaluation.c src/chat/chat_generation.c
    src/chat/chat_model.c src/chat/chat_prompt.c)
target_link_libraries(centroid_chat_backend PRIVATE centroid_neural_backend)
cgai_private_component(centroid_bark_backend src/bark/bark_model.c src/bark/bark_session.c)
target_link_libraries(centroid_bark_backend PRIVATE centroid_neural_backend)
cgai_private_component(centroid_spatial_backend src/spatial/spatial_index.c src/spatial/spatial_query.c)
cgai_private_component(centroid_knowledge_backend
    src/knowledge/static_knowledge.c src/knowledge/knowledge_catalog.c
    src/knowledge/knowledge_clusters.c src/knowledge/knowledge_query.c
    src/knowledge/knowledge_catalog/00-build-and-configuration.c
    src/knowledge/knowledge_catalog/01-database-and-persistence.c
    src/knowledge/knowledge_catalog/02-native-c-core.c)
target_link_libraries(centroid_knowledge_backend PRIVATE centroid_spatial_backend)

foreach(suite IN ITEMS bark chat knowledge spatial)
    cgai_inference_test(${suite}_inference SOURCES tests/test_${suite}.c
        LIBRARIES centroid_${suite}_backend)
endforeach()
target_link_libraries(centroid_spatial_inference_tests PRIVATE centroid_knowledge_backend)
cgai_inference_test(core_numerical
    SOURCES tests/test_runner.c tests/test_tokenizer.c tests/test_vocabulary.c
        tests/test_model_math.c tests/test_model_io.c tests/test_sampling.c tests/test_file_utils.c
    LIBRARIES centroid_count_backend)
cgai_inference_test(neural_inference
    SOURCES tests/test_neural.c tests/test_neural_math.c tests/test_neural_io.c tests/test_neural_session.c
    LIBRARIES centroid_neural_backend)
cgai_inference_test(neural_checkpoint SOURCES tests/test_neural_checkpoint.c
    LIBRARIES centroid_neural_backend)
cgai_inference_test(encoded_generation SOURCES tests/test_encoded_generation.c
    LIBRARIES centroid_neural_backend)
foreach(suite IN ITEMS math codec)
    cgai_inference_test(gameplay_${suite} SOURCES tests/test_gameplay_${suite}.c
        LIBRARIES centroid_life::centroid_life centroid_private_io)
endforeach()

set(BARK_VERIFICATION_SOURCES tools/bark/bark_fixture.c tools/bark/bark_evaluation.c
    tests/test_bark_fixture.c tests/test_bark_quality.c)
cgai_private_component(centroid_bark_fixtures tools/bark/bark_fixture.c tools/bark/bark_evaluation.c)
target_include_directories(centroid_bark_fixtures PRIVATE tools/bark)
target_link_libraries(centroid_bark_fixtures PRIVATE centroid_bark_backend)
cgai_inference_test(bark_fixture SOURCES tests/test_bark_fixture.c
    INCLUDES tools/bark LIBRARIES centroid_bark_fixtures
    ARGUMENTS "${CMAKE_CURRENT_SOURCE_DIR}/data/gameplay/barks-v1")
cgai_inference_test(bark_quality SOURCES tests/test_bark_quality.c
    INCLUDES tools/bark LIBRARIES centroid_bark_fixtures)

set(GAMEPLAY_VERIFICATION_SOURCES tools/gameplay/gameplay_tasks.c
    tools/gameplay/gameplay_initialization.c tools/gameplay/gameplay_evaluation.c
    tests/test_gameplay_tasks.c tests/test_gameplay_initialization.c tests/test_gameplay_quality.c)
cgai_private_component(centroid_gameplay_fixtures tools/gameplay/gameplay_tasks.c
    tools/gameplay/gameplay_initialization.c tools/gameplay/gameplay_evaluation.c)
target_include_directories(centroid_gameplay_fixtures PRIVATE tools/gameplay)
cgai_inference_test(gameplay_tasks SOURCES tests/test_gameplay_tasks.c
    INCLUDES tools/gameplay LIBRARIES centroid_gameplay_fixtures
    ARGUMENTS "${CMAKE_CURRENT_SOURCE_DIR}/data/gameplay/composed-v1")
cgai_inference_test(gameplay_initialization SOURCES tests/test_gameplay_initialization.c
    INCLUDES tools/gameplay LIBRARIES centroid_gameplay_fixtures centroid_private_io)
cgai_inference_test(gameplay_quality SOURCES tests/test_gameplay_quality.c
    INCLUDES tools/gameplay LIBRARIES centroid_gameplay_fixtures)

set(NPC_V1_VERIFICATION_SOURCES tools/npc/npc_policy.c tools/npc/npc_world.c
    tools/npc/npc_teacher.c tools/npc/npc_corpus.c tools/npc/npc_records.c
    tests/test_npc_policy.c tests/test_npc_world.c)
cgai_private_component(centroid_npc_v1 tools/npc/npc_policy.c tools/npc/npc_world.c
    tools/npc/npc_teacher.c tools/npc/npc_corpus.c tools/npc/npc_records.c)
target_include_directories(centroid_npc_v1 PRIVATE tools/npc)
target_link_libraries(centroid_npc_v1 PRIVATE centroid_private_io)
cgai_inference_test(npc_records SOURCES tests/test_npc_policy.c
    INCLUDES tools/npc LIBRARIES centroid_npc_v1)
cgai_inference_test(npc_world SOURCES tests/test_npc_world.c
    INCLUDES tools/npc LIBRARIES centroid_npc_v1)

# Compile comparison worlds independently; namespace every export to preserve active geometry.
foreach(profile IN ITEMS 1 2 3)
    if(profile EQUAL 1)
        set(world_source tools/npc/npc_world.c)
    else()
        set(world_source tools/npc_v${profile}/npc_world.c)
    endif()
    add_library(centroid_npc_v${profile}_geometry OBJECT ${world_source})
    target_include_directories(centroid_npc_v${profile}_geometry PRIVATE src tools/npc)
    target_link_libraries(centroid_npc_v${profile}_geometry PRIVATE centroid_life::centroid_life)
    cgai_enable_warnings(centroid_npc_v${profile}_geometry)
    foreach(export IN ITEMS cardinalities family_get world_init world_observe world_step
            memory_reset memory_observe encode observation_actions)
        target_compile_definitions(centroid_npc_v${profile}_geometry PRIVATE
            npc_${export}=npc_v${profile}_${export})
    endforeach()
endforeach()

set(NPC_V2_VERIFICATION_SOURCES tools/npc_v2/npc_world.c tools/npc/npc_reference.c
    tools/npc_v2/npc_optimizer.c tests/test_npc_v2_world.c tests/test_npc_v2_reference.c
    tests/test_npc_v2_optimizer.c)
set(NPC_V3_VERIFICATION_SOURCES tools/npc_v3/npc_world.c tools/npc/npc_reference.c
    tools/npc_v3/npc_collection.c tools/npc_v3/npc_evaluation.c tools/npc_v3/npc_optimizer.c
    tools/npc_v3/tests/test_world.c tools/npc_v3/tests/test_reference.c
    tools/npc_v3/tests/test_collection.c tools/npc_v3/tests/test_evaluation.c
    tools/npc_v3/tests/test_optimizer.c)
foreach(profile IN ITEMS 2 3)
    set(profile_sources tools/npc/npc_policy.c tools/npc_v${profile}/npc_world.c
        tools/npc/npc_teacher.c tools/npc/npc_corpus.c tools/npc/npc_reference.c)
    if(profile EQUAL 3)
        list(APPEND profile_sources tools/npc_v3/npc_collection.c tools/npc_v3/npc_evaluation.c)
        set(world_test tools/npc_v3/tests/test_world.c)
        set(reference_test tools/npc_v3/tests/test_reference.c)
        set(optimizer_test tools/npc_v3/tests/test_optimizer.c)
    else()
        set(world_test tests/test_npc_v2_world.c)
        set(reference_test tests/test_npc_v2_reference.c)
        set(optimizer_test tests/test_npc_v2_optimizer.c)
    endif()
    cgai_private_component(centroid_npc_v${profile} ${profile_sources})
    target_include_directories(centroid_npc_v${profile} PRIVATE tools/npc_v${profile} tools/npc)
    target_link_libraries(centroid_npc_v${profile} PRIVATE centroid_private_io)
    cgai_inference_test(npc_v${profile}_world SOURCES ${world_test}
        INCLUDES tools/npc_v${profile} tools/npc
        LIBRARIES centroid_npc_v${profile} centroid_npc_v1_geometry)
    cgai_inference_test(npc_v${profile}_reference SOURCES ${reference_test}
        INCLUDES tools/npc_v${profile} tools/npc LIBRARIES centroid_npc_v${profile})
    cgai_inference_test(npc_v${profile}_numerical
        SOURCES tools/npc_v${profile}/npc_optimizer.c ${optimizer_test}
        INCLUDES tools/npc_v${profile} tools/npc LIBRARIES centroid_life::centroid_life)
endforeach()
target_link_libraries(centroid_npc_v3_world_tests PRIVATE centroid_npc_v2_geometry)
cgai_inference_test(npc_collection SOURCES tools/npc_v3/tests/test_collection.c
    INCLUDES tools/npc_v3 tools/npc LIBRARIES centroid_npc_v3)
cgai_inference_test(npc_v3_evaluation SOURCES tools/npc_v3/tests/test_evaluation.c
    INCLUDES tools/npc_v3 tools/npc LIBRARIES centroid_npc_v3)
