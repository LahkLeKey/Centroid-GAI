# Reading the C implementation

This guide is for readers who know the application but are still learning C.
The application boundary is the compiled C ABI plus the TypeScript API and its
Prisma 8/PostgreSQL persistence layer. The C library learns model state and
serializes model bytes; the TypeScript layer owns database operations.

## Where to begin

Follow one operation from the outside inward. Read the function's documentation
first, then its numbered `Step` comments. Those comments describe execution
phases; a branch may exit before reaching a later step.

| Question | Starting point | Continue with |
| --- | --- | --- |
| How does TypeScript invoke C? | `native/addon.c` under the persistence adapter | `node_training.c`, `node_generation.c`, `node_metadata.c` |
| What can an ABI consumer call? | `include/centroid_gai_abi.h` | The corresponding `src/abi_*.c` implementation |
| Who owns the model? | `src/centroid_gai.c` | `src/internal/cgai_internal.h` |
| How does training learn? | `src/model_training.c` | Tokenization, vocabulary, embedding, and centroid modules |
| How is the next word chosen? | `src/model_generation.c` | Generation workspace and sampling modules |
| What is stored as model bytes? | `src/model_encode.c` | `src/model_decode.c` and `src/internal/model_format.h` |
| What examples show correct API usage? | `tests/test_centroid_gai.c` | `tests/test_abi.c` and focused unit tests |

In generated HTML, function pages include their source and links to referenced
functions. Private helpers and test functions are included, so a reader can
follow the implementation without guessing which file contains the next step.

## Pointers, values, and output arguments

A value such as `size_t count` stores the number directly. A pointer such as
`cgai_model *model` stores an address. `NULL` represents the absence of an
address; it must be checked before code reads through it.

| C expression | Meaning in this code |
| --- | --- |
| `model->config` | Read a structure field through a pointer. |
| `config.dimensions` | Read a field from a structure value. |
| `&model` | Take the address of the caller's pointer variable. |
| `*output = model` | Write a value into the caller's variable through an output pointer. |
| `const cgai_model *model` | This pointer permits reading the model, not changing it through that pointer. |
| `sizeof(float)` | Number of bytes needed for one element of that type. |
| `items[i]` | Read or write the element at zero-based index `i`. |

A `cgai_abi_model **output` parameter is a pointer to a pointer variable. The ABI
uses it to give the caller a newly allocated handle while returning a separate
status code. The handle is opaque: users can hold it and pass it to functions,
but cannot inspect its private structure through the public header.

The `const` qualifier does not make an object immortal or add synchronization.
The owner still has to keep it alive and prevent incompatible concurrent use.

## Follow ownership through every call

An **owned** pointer comes with responsibility for eventual cleanup. A
**borrowed** pointer may be used for the documented duration, but must not be
freed by the borrower. Copying a pointer copies its address; it does not copy
the pointed-to allocation or create a second independently owned object.

| Resource | Owner after success | Cleanup |
| --- | --- | --- |
| Core model from create/load | Calling C code | `cgai_model_destroy()` |
| ABI model from create/import | ABI caller | `cgai_abi_model_destroy()` |
| Data returned in an ABI buffer | Caller must arrange release through the ABI | `cgai_abi_buffer_free()` |
| Token list | Workspace or test that initialized the list | `cgai_token_list_destroy()` |
| Copied Node string argument | Native helper receiving the copy | `free()` |
| JavaScript Buffer/String | Node runtime | No native `free()` |
| Diagnostic/version/schema pointer | Library storage, borrowed by caller | Do not free |

An ABI buffer descriptor can live on the caller's stack while its `data` points
to an ABI allocation. Freeing that data and clearing the descriptor does not
free the stack variable. Another copied descriptor would still contain the old
address, so copying descriptors does not grant permission to free both copies.

Workspace initialization with `{0}` starts all ownership fields empty. If the
third allocation fails after the first two succeeded, the common cleanup path
can release the two valid pointers and safely pass the remaining NULL pointers
to `free()`. Some destructors also reset their descriptors; others expect the
caller to discard them after cleanup. Each contract states the applicable rule.

## Strings and binary data are different

C strings terminate at a zero byte, written `'\0'`. `strlen()` counts bytes up
to that terminator and excludes it from the length. Copying a string therefore
usually needs `length + 1` bytes. Embedded zero bytes end what string-based core
operations observe, even if a JavaScript string originally contained more text.

An artifact is binary data and can contain zero bytes anywhere. Its explicit
byte count determines its length. Do not apply `strlen()` to an artifact. The
codec writes a length before each token spelling and adds a terminator only
when reconstructing that spelling in memory.

UTF-8 uses a variable number of bytes per character. These implementation
lengths are byte counts, not displayed-character counts. Tokenization is a
small byte-oriented scanner, not full Unicode segmentation or case folding.

## Understand the two status conventions

The core uses `CGAI_STATUS_OK == 1` and `CGAI_STATUS_ERROR == 0`. The ABI uses
`CGAI_ABI_OK == 0` and several nonzero failure codes. Compare named constants at
the boundary; treating an ABI result as a core-style boolean reverses success.

The calling thread's last diagnostic is separate from the returned status.
Status determines success. Copy a borrowed diagnostic if it must survive another
call that could overwrite the thread-local message.

`&&` and `||` evaluate left to right and stop as soon as the result is known.
For example, `model != NULL && train(model)` never calls `train` when the pointer
is NULL. This property also orders codec writes and Node-API conversions.

## Read the arrays as tables

The model stores rectangular numeric tables as contiguous one-dimensional
arrays. For a table with `width` columns, the element at a row and column is
`array[row * width + column]`. Multiplying skips complete rows; adding the column
selects the element within that row.

Centroid rows have `config.dimensions` float components. Token-count rows have
`vocabulary_size` integer counts. Adding a vocabulary entry changes the latter
row width, so `resize_counts()` copies each old row into a wider zeroed table.
The strong token-ID and centroid-ID wrapper types help the compiler catch
accidental mixing of the two index domains.

## Trace one learned transition

1. Tokenization creates owned normalized spellings from the borrowed corpus.
2. Vocabulary insertion maps each spelling to a stable token ID.
3. BOS IDs supply context before the first real token; EOS is appended as a final target.
4. Recent history is converted to deterministic token vectors and a recency-weighted average.
5. Training initializes a spare centroid or chooses the nearest learned centroid.
6. The centroid's mean moves toward the context using a one-over-observation-count update.
7. The chosen centroid's count for the observed target increases.
8. That target is appended to history, making it context for the next transition.

Generation reads those same centroid means and count rows. It selects a token,
appends its spelling to output, and feeds its ID back into temporary history.
It stops at EOS or the requested token limit. Generation does not add vocabulary
or train the model.

## Limits that comments must preserve

Model bytes use native numeric representations and are intended for trusted,
compatible producers and consumers. Bounds checks are not a complete hostile-file
security boundary. File saves replace a destination directly and are not atomic
transactions. Training failure does not promise rollback of earlier mutation.

Allocation counts use unsigned `size_t`, whose arithmetic can wrap. Checked
addition and multiplication prove relevant sizes fit before allocation. A count
of elements and a count of bytes are different units; look for `sizeof` at the
conversion. A reserved text-buffer estimate is also different from a guarantee
about token lengths: long tokens may cause generation to report insufficient
output space.

## Keep the walkthroughs accurate

Update the function contract and numbered comments whenever behavior changes.
Describe the current implementation, including partial results and ownership,
rather than a stronger contract it does not enforce. Tests illustrate those
contracts, and their assertion macro stays active in release builds.

Build the `docs` target with `CGAI_BUILD_DOCS=ON` to check Doxygen comments, and
run `cgai_lint` plus `cgai_format_check` for C11 analysis and source formatting.
