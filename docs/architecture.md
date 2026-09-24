# Architecture

Centroid-GAI separates its stable API from its implementation details. Only
headers under `include/` are installed: `centroid_gai.h` and `centroid_gai_abi.h`.
Headers under `src/internal/` may change
without compatibility guarantees.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `centroid_gai.c` | Model configuration, lifecycle, and state queries |
| `model_training.c` | Training workspace and online centroid updates |
| `model_generation.c` | Generation validation, sampling loop, and output spacing |
| `model_generation_workspace.c` | Prompt tokenization, checked history allocation, and workspace ownership |
| `vocabulary.c` | Vocabulary ownership and next-token count storage |
| `tokenizer.c` | Text-to-token conversion and token-list ownership |
| `model_embedding.c` | Token hashing and weighted context vectors |
| `model_centroid.c` | Nearest-centroid search |
| `model_random.c` | Deterministic splitmix64 state |
| `model_sampling.c` | Greedy and temperature-weighted token selection |
| `model_encode.c`, `model_decode.c` | In-memory artifact encoding and validation |
| `model_file.c`, `file_utils.c` | Model-file operations and whole-file byte I/O |
| `error.c` | Thread-local error reporting |
| `abi.c`, `abi_config.c` | ABI version/schema queries and default configuration |
| `abi_model.c`, `abi_metadata.c` | ABI model lifecycle/training and metadata conversion |
| `abi_serialization.c`, `abi_generation.c`, `abi_buffer.c` | ABI artifact conversion, text generation, and returned-buffer ownership |
| `cli.c`, `cli_arguments.c` | Command dispatch and shared numeric parsing |
| `cli_training.c`, `cli_generation.c` | Command-specific parsing, execution, and cleanup |

Each math operation has a matching private header. Generation workspace declarations
stay in `internal/model_generation.h`; command handlers stay in
`internal/cli_commands.h`. These boundaries keep temporary storage and command
details out of the installed API.

The Node bridge under `persistence/prisma-postgres/native/` uses `addon.c` only
for registration and version/schema queries. Training, generation, and metadata
callbacks live in their respective `node_*.c` files. `node_artifact.c` owns the
Buffer/model conversion boundary, `node_arguments.c` copies strings and reads
configuration, and `node_error.c` translates failures into JavaScript exceptions.
Callbacks release ABI models and buffers before returning; JavaScript receives
copies of artifact bytes and generated text.

Database persistence is an outer adapter, not a core-library responsibility.
The Prisma ORM 8/PostgreSQL boundary is documented in
[`persistence.md`](persistence.md).

## Domain types

Token identifiers and centroid identifiers are intentionally different struct
types (`cgai_token_id` and `cgai_centroid_id`). Both contain an array index, but
the compiler rejects accidentally passing one where the other is expected.
Conversions to raw indices occur only at storage boundaries.

Fallible public operations return `cgai_status`. Pointer-returning constructors
use `NULL` for failure. Detailed diagnostics are available through
`cgai_last_error()`.

## Data flow

Training tokenizes text, interns its vocabulary, builds a weighted context
embedding, updates the nearest centroid, and records the observed next token.
Generation repeats the context lookup and samples the selected centroid's token
distribution. Persistence serializes the same model state without participating
in training or inference.
