# Centroid-GAI

The goal is centroid models trained through the C11 Centroid Life engine. Its
evolving world creates collisions that train participants until validated merge,
useful separation or deferral; learned conflicts also change cell evolution.
The current native build retires the old public APIs,
HTTP/Node/TypeScript/database stack and non-C executable workflows. Removed Git
material stays removed. The broader [next deliverables](docs/centroid-next-deliverables.md)
remain in progress.

The new [native context pipeline](docs/native-context.md) consumes working source
files and explicitly injected LLM/activity notes, fits lexical centroid memory at
physical Life contacts, checkpoints the complete state, and retrieves attributed
source excerpts locally. The [native source evolution loop](docs/native-code-evolution.md)
uses this context and separately measured training feedback to learn choices
among a bounded C initialization catalog. General code generation and coding
quality remain unvalidated.

The network learns token embeddings, an ordered context encoder, centroid routing,
and next-token predictions through backpropagation. The current design does not
require a transformer. Useful conversation still requires dialogue training,
native runtime integration, and measured answer quality.

## Current native entry points

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
```

`cgai_life` runs, resumes, evaluates and inspects the Life world. `cgai_context`
ingests the codebase, injects labelled notes, continues collision-gated fitting,
and queries stored excerpts. `cgai_life_evolve` creates isolated C source
candidates, checks native build/test/fitness results, teaches contact-owned
choice heads from TRAIN measurements, and resumes complete search generations
from immutable bundles. See [native context usage](docs/native-context.md)
and [source evolution usage](docs/native-code-evolution.md) for commands and limits.
The installed native boundary is `include/centroid_life.h` plus
`include/centroid_life_domain.h` and `include/centroid_life_npc.h`. The version-three package supports two through
eight ownership groups, with four as the default. `cgai_life_probe` runs the
first typed native domain adapter with complete round proposals and restart
state; see [native domain training](docs/native-domain-training.md).
`cgai_life_npc` trains and checkpoints complete native NPC episodes with their
own action history; see [native NPC training](docs/native-npc-training.md).

## Historical models and migration inputs

The table below records the pre-migration components and measured experiments.
The removed service paths are historical references, not active commands.

| Component | Historical status |
| --- | --- |
| C11 Life training migration | Planned sole production training method and new native interface; see the next deliverables |
| Centroid neural network | Implemented in C; train, evaluate, generate, save and load through the native API and CLI |
| Composed gameplay models | C11 shared encoder, learned specialist centroids and typed bark/intent heads; deterministic joint training, gated releases and task scaffolding |
| Observable NPC policy v1 | C11 simulator, observed memory, joint training and sealed evaluation; first candidate rejected, no accepted v1 release |
| Iterative NPC training v2 | Accepted C11 recovery policy: 95.31% audit completion/survival, bounded sessions and pinned planner comparison; planner equivalence unmet |
| Hazard recovery and specialization v3 | C11 exploratory training and accepted-v2 comparison; 100% development completion, rejected before audit because module complementarity was not demonstrated |
| Centroid Life experiment | C11 collision teaching, learned cell edits, gated consolidation and exact continuation; browser viewer retires during migration |
| Neural chatbot | Native structured dialogue engine, bounded workers, immutable artifacts and persistent HTTP conversations |
| Baseline HTTP routes | Serve the original count-based centroid engine and `.cgai` artifacts alongside separate neural chat routes |
| Docker Compose | API and PostgreSQL with committed chat/memory migrations and restart/reload integration tests |
| Research and memory | Model-free startup, automatic bounded Wikipedia research, optional SearXNG and scoped memory controls; source results remain separate from neural synthesis |
| Repository conversations | Offline commit-pinned excerpts, reviewed next-step actions, durable follow-up context and explicit snapshot switching |
| Training data workflow | Authored seeds generate 200 repository candidates; content-bound review, local immutable releases, training requests and vocabulary coverage experiments |

The historical neural prototype uses `.cgnn` files, distinct from the old `.cgai`
HTTP artifacts. Neither engine provides a validated general purpose
conversational assistant. The synthetic extraction fixture scored 0/6
native exact responses and 6/6 source-baseline responses; this is an engineering
diagnostic, not general factuality evidence. Historical neural replies admitted current
evidence and return only checked complete quotations, otherwise source excerpts
or abstention. Training requires independent development checks before publication.
The historical default chat mode was source excerpts. The
[chat service guide](docs/chat-service.md) retains the old requirements and
measured outcomes; its service commands are retired.

## Start here

1. [Review the ordered C11 deliverables](docs/centroid-next-deliverables.md).
2. [Read the new training contract](docs/centroid-training-parity-plan.md).
3. [Inspect the existing Centroid Life implementation](docs/centroid-life.md).
4. [Review reusable native model mathematics](docs/neural-centroid.md).
5. [Understand historical NPC outcomes](docs/npc-planner-v2.md).

Older chatbot/API/service roadmaps are historical architecture references. Their
workflow restoration and service integration sequences are superseded.

The [documentation reconciliation](docs/centroid-documentation-reconciliation.md)
records retained requirements, historical specifications and retired scope used
by the [conflict training plan](docs/centroid-training-parity-plan.md).

The [documentation index](docs/README.md) connects the architecture, implementation
status, and research/memory design. Each guide labels planned behavior explicitly.

## Code map

- `include/centroid_life.h`, `src/life/`, `tools/life/`: installed C11 Life boundary,
  world, collision learning, exact checkpoints and native inspection.
- `tools/context/`: native working-codebase admission and LLM/activity context CLI.
- `tools/evolve/`: isolated native C candidate generation, build/test and fitness gates.
- `src/neural/`: retained private neural inference and numerical components.
- `src/gameplay/` and `tools/gameplay/`: composed specialists,
  deterministic joint training, quality/performance gates and C11 task scaffolding.
- `tools/npc/` and `data/gameplay/npc-pilot-v1/`:
  bounded episode policy, shared-model host example and frozen observable contracts.
- `tools/npc_v2/` and `data/gameplay/npc-pilot-v2/`: iterative recovery training,
  independently reserved detours and authoritative comparator replay.
- `tools/npc_v3/` and `data/gameplay/npc-pilot-v3/`: exploratory recovery,
  observed specialist roles and isolated build/evaluation against accepted v2.
- `src/chat/`: retained private conversation protocol and codec.
- `tests/` and `data/chat/`: correctness checks and authored synthetic fixtures; generated repository training releases stay local under `build/`.
- `src/knowledge/knowledge_catalog/`: supporting catalog
  and source-retrieval tools, distinct from neural model weights.

Licensed under [MIT](LICENSE).
