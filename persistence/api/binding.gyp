{
  "targets": [
    {
      "target_name": "centroid_gai_native",
      "sources": [
        "../../src/abi/abi.c",
        "../../src/abi/abi_buffer.c",
        "../../src/abi/abi_config.c",
        "../../src/abi/abi_generation.c",
        "../../src/abi/abi_inspection.c",
        "../../src/abi/abi_metadata.c",
        "../../src/abi/abi_model.c",
        "../../src/abi/abi_serialization.c",
        "../../src/abi/abi_spatial.c",
        "../../src/chat/chat_codec.c",
        "../../src/chat/chat_evaluation.c",
        "../../src/chat/chat_model.c",
        "../../src/chat/chat_prompt.c",
        "../../src/core/error.c",
        "../../src/core/file_utils.c",
        "../../src/core/json_writer.c",
        "../../src/core/model_random.c",
        "../../src/core/tokenizer.c",
        "../../src/core/vocabulary.c",
        "../../src/model/centroid_gai.c",
        "../../src/model/model_centroid.c",
        "../../src/model/model_composition.c",
        "../../src/model/model_decode.c",
        "../../src/model/model_embedding.c",
        "../../src/model/model_encode.c",
        "../../src/model/model_file.c",
        "../../src/model/model_generation.c",
        "../../src/model/model_generation_workspace.c",
        "../../src/model/model_sampling.c",
        "../../src/model/model_training.c",
        "../../src/neural/neural_evaluation.c",
        "../../src/neural/neural_generation.c",
        "../../src/neural/neural_math.c",
        "../../src/neural/neural_model.c",
        "../../src/neural/neural_resources.c",
        "../../src/neural/neural_training.c",
        "../../src/neural/neural_vocabulary.c",
        "../../src/spatial/spatial_index.c",
        "../../src/spatial/spatial_query.c",
        "native/addon.c",
        "native/node_arguments.c",
        "native/node_artifact.c",
        "native/node_chat.c",
        "native/node_chat_arguments.c",
        "native/node_chat_reply.c",
        "native/node_chat_settings.c",
        "native/node_composition.c",
        "native/node_error.c",
        "native/node_generation.c",
        "native/node_metadata.c",
        "native/node_spatial.c",
        "native/node_training.c"
      ],
      "include_dirs": [
        "../../include",
        "../../src"
      ],
      "defines": [
        "CGAI_ABI_BUILD",
        "_CRT_SECURE_NO_WARNINGS",
        "NAPI_VERSION=10"
      ],
      "conditions": [
        [
          "OS=='win'",
          {
            "msvs_settings": {
              "VCCLCompilerTool": {
                "WarningLevel": 4,
                "AdditionalOptions!": [
                  "-std:c++20",
                  "/std:c++20",
                  "/Zc:__cplusplus"
                ],
                "LanguageStandard_C": "stdc11"
              }
            }
          }
        ],
        [
          "OS!='win'",
          {
            "cflags": [
              "-std=c11",
              "-Wall",
              "-Wextra",
              "-Wpedantic",
              "-Wconversion"
            ],
            "libraries": [
              "-lm"
            ]
          }
        ]
      ]
    }
  ]
}
