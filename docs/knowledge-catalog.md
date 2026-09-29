# Manual C knowledge catalog

The catalog is nine hand-authored records in three C files under
`src/knowledge_catalog/`: build, database, and native-core. The former trained
vectors and imported `knowledge/encyclopedia` release have been removed.
No model, JSON data, database, or download is needed to build or query this catalog.

## Read a vector

Every vector has 32 components to preserve the native API's dimension contract.
Only the first eight have meanings; components 8 through 31 are reserved and zero.
C designated initializers label each nonzero component. Omitted components are zero.

| Index | Feature |
| --- | --- |
| 0 | Configuration and build work |
| 1 | Data schema |
| 2 | Reading data |
| 3 | Writing data |
| 4 | Validation |
| 5 | Memory ownership |
| 6 | Numeric computation |
| 7 | Serialization |

Weights are manually chosen: `1.0` means a primary feature, `0.5` a supporting
feature, `0.25` a minor feature, and zero means absent. They are illustrative
weights, not learned embeddings or probabilities. All records share these axes.

For example, `database:lookup` contains:

```c
.vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
    [1 /* schema */] = 1.0F,
    [2 /* read */] = 1.0F,
    [4 /* validation */] = 0.5F,
}
```

Its stored description is: "Read a record by key; return the matching record or
an explicit not-found result." This describes the operation represented by the
record. Looking up this catalog record returns that description; it does not
execute a database lookup.

## Records and returned data

| Category | IDs |
| --- | --- |
| Build | `build:compile`, `build:configure`, `build:test` |
| Database | `database:lookup`, `database:schema`, `database:write` |
| Native C | `native-core:allocate`, `native-core:distance`, `native-core:serialize` |

Each C module returns a borrowed pointer to immutable category metadata and its
record array. A record contains its ID, category index, description, cluster ID,
and vector. `observations` is zero because no training took place. The release
SHA string is empty because this baseline has no imported release manifest.
Existing consumers of the former IDs and category indices must update their references.

`cgai_static_knowledge_neighbors()` compares vectors using squared Euclidean
distance and writes row indices and distances into your hit array. It excludes
the source cluster and sorts by distance, then ID. Read only `stats.count` hits.
Resolve each hit through `cgai_static_knowledge_centroid_at()` to get its stored
record. For a database-filtered query from `database:lookup`, the results are:

| ID | Squared distance |
| --- | --- |
| `database:schema` | 1.25 |
| `database:write` | 2.0 |

The first distance is `1*1 + 0.5*0.5`: schema lacks the read feature and has
0.5 more validation weight. This small baseline makes the ranking inspectable.

```c
#include "centroid_gai_knowledge.h"
#include <stdio.h>

int main(void) {
    cgai_static_knowledge_index *index = cgai_static_knowledge_open();
    cgai_spatial_hit hits[3];
    cgai_spatial_stats stats;
    size_t category = cgai_static_knowledge_category_find("database");
    if (!index || category == SIZE_MAX ||
        cgai_static_knowledge_neighbors(index, "database:lookup", 3,
            (uint32_t)category, hits, &stats) != CGAI_STATUS_OK) {
        cgai_static_knowledge_close(index);
        return 1;
    }
    for (size_t i = 0; i < stats.count; ++i) {
        const cgai_static_knowledge_centroid *record =
            cgai_static_knowledge_centroid_at(hits[i].row);
        printf("%s: %s (%g)\n", record->id, record->description,
               hits[i].squared_distance);
    }
    cgai_static_knowledge_close(index);
    return 0;
}
```

The search context is owned and must be closed. Catalog records and strings have
process lifetime and must never be freed. Failed queries clear statistics and
return an error status; `cgai_last_error()` supplies diagnostics.

The ABI serializes metadata into owned JSON buffers released with
`cgai_abi_buffer_free()`. It omits vector coordinates. The CLI prints records with
tab separators, prefixing nearest-neighbor results with squared distance:

```powershell
.\build\cgai.exe knowledge list database
.\build\cgai.exe knowledge find database:lookup
.\build\cgai.exe knowledge nearest database:lookup 3 database
```

## Edit the baseline

Edit IDs, descriptions, and feature weights directly in the category C files.
Keep existing IDs stable, keep records sorted by ID, and use the shared feature
meanings above. Each unique vector needs a dense cluster ID. The existing compiler
can normalize C records and rebuild the registry and clusters without training:

```powershell
node persistence/api/src/knowledge/compile-native-knowledge.ts --source-c src/knowledge_catalog.c --output src/knowledge_catalog.c
cmake --build build
ctest --test-dir build --output-on-failure
node --test persistence/api/src/knowledge/compile-native-knowledge.test.ts
```

The compiler accepts labeled sparse C initializers and preserves them for this
manual baseline. It does not learn or choose the weights. Historical import tools
remain available for separate experiments; write their output under `build/`.
