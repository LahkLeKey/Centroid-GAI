{
  "targets": [
    {
      "target_name": "centroid_gai_native",
      "sources": [
        "native/addon.c",
        "native/node_composition.c",
        "../../src/model_composition.c",
        "../../src/abi_inspection.c",
        "../../src/abi.c",
        "../../src/abi_config.c",
        "../../src/abi_model.c",
        "../../src/centroid_gai.c",
        "../../src/error.c",
        "../../src/file_utils.c",
        "../../src/model_decode.c",
        "../../src/model_encode.c",
        "../../src/model_generation.c",
        "../../src/model_generation_workspace.c",
        "../../src/model_sampling.c",
        "../../src/model_training.c",
        "../../src/tokenizer.c",
        "../../src/vocabulary.c",
        "../../src/model_embedding.c",
        "../../src/model_centroid.c",
        "../../src/model_random.c",
        "../../src/abi_buffer.c",
        "../../src/abi_generation.c",
        "../../src/abi_metadata.c",
        "../../src/abi_serialization.c",
        "native/node_arguments.c",
        "native/node_artifact.c",
        "native/node_error.c",
        "native/node_generation.c",
        "native/node_metadata.c",
        "native/node_training.c",
        "../../src/model_file.c"
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
              "AdditionalOptions!": ["-std:c++20", "/std:c++20", "/Zc:__cplusplus"],
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
