# Architecture

Centroid-GAI separates its stable API from its implementation details. Only
`include/centroid_gai.h` is installed. Headers under `src/internal/` may change
without compatibility guarantees.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `centroid_gai.c` | Model lifecycle plus training and generation orchestration |
| `vocabulary.c` | Vocabulary ownership and next-token count storage |
| `tokenizer.c` | Text-to-token conversion and token-list ownership |
| `model_math.c` | Hashed embeddings, context vectors, nearest-centroid search, RNG |
| `model_io.c` | Binary persistence |
| `error.c` | Thread-local error reporting |

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
