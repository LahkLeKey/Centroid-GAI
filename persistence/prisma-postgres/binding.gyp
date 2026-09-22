{
  "targets": [
    {
      "target_name": "centroid_gai_native",
      "sources": [
        "native/addon.c",
        "../../src/abi.c",
        "../../src/abi_config.c",
        "../../src/abi_model.c",
        "../../src/centroid_gai.c",
        "../../src/error.c",
        "../../src/file_utils.c",
        "../../src/model_decode.c",
        "../../src/model_encode.c",
        "../../src/model_generation.c",
        "../../src/model_math.c",
        "../../src/model_sampling.c",
        "../../src/model_training.c",
        "../../src/tokenizer.c",
        "../../src/vocabulary.c"
      ],
      "include_dirs": ["../../include", "../../src"],
      "defines": ["CGAI_ABI_BUILD", "_CRT_SECURE_NO_WARNINGS", "NAPI_VERSION=10"],
      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "AdditionalOptions": ["/W4"]
            }
          }
        }],
        ["OS!='win'", {
          "cflags": ["-std=c11", "-Wall", "-Wextra", "-Wpedantic"],
          "libraries": ["-lm"]
        }]
      ]
    }
  ]
}
