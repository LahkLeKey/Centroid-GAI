# Centroid-GAI

The goal is a chatbot powered by a trainable centroid neural network written in
C11. Users will train models, send messages, and inspect persistent conversations
through an HTTP API using curl and Docker Compose. A browser application is
outside the current scope.

The network learns token embeddings, an ordered context encoder, centroid routing,
and next-token predictions through backpropagation. The current design does not
require a transformer. Useful conversation still requires dialogue training,
service integration, and measured answer quality.

## Where the project stands

| Component | Current state |
| --- | --- |
| Centroid neural network | Implemented in C; train, evaluate, generate, save and load through the native API and CLI |
| Composed gameplay models | C11 shared encoder, learned specialist centroids and typed bark/intent heads; deterministic joint training, gated releases and task scaffolding |
| Neural chatbot | Native structured dialogue engine, bounded workers, immutable artifacts and persistent HTTP conversations |
| Existing HTTP API | Serves the original count-based centroid engine and `.cgai` artifacts |
| Docker Compose | API and PostgreSQL with committed chat/memory migrations and restart/reload integration tests |
| Research and memory | Model-free startup, automatic bounded Wikipedia research, optional SearXNG and scoped memory controls; source results remain separate from neural synthesis |
| Repository conversations | Offline commit-pinned excerpts, reviewed next-step actions, durable follow-up context and explicit snapshot switching |
| Training data workflow | Authored seeds generate 200 repository candidates; content-bound review, local immutable releases, training requests and vocabulary coverage experiments |

The neural prototype uses `.cgnn` files. They cannot be served by the existing
`.cgai` HTTP endpoints. Neither engine currently provides a validated general
purpose conversational assistant. The new synthetic extraction fixture scores 0/6
native exact responses and 6/6 source-baseline responses; this is an engineering
diagnostic, not general factuality evidence. Neural replies now admit current
evidence and return only checked complete quotations, otherwise source excerpts
or abstention. Training requires independent development checks before publication.
The default chat mode remains source excerpts. See the
[chat service guide](docs/chat-service.md) for commands and remaining release gates.

## Start here

1. [Build and check the project](docs/development.md).
2. [Train and evaluate the current neural prototype](docs/neural-centroid.md).
3. [Review the existing API and curl workflow](docs/api-contract.md).
4. [Train and chat through the API](docs/chat-service.md).
5. [Ask about this codebase and continue a next-step loop](docs/repository-chat.md).
6. [Train, verify and extend composed gameplay models](docs/neural-centroid.md#composed-centroid-gameplay-network).

The [chatbot roadmap](docs/chatbot-plan.md) tracks the remaining quality and release gates.

The [documentation index](docs/README.md) connects the architecture, implementation
status, and research/memory design. Each guide labels planned behavior explicitly.

## Code map

- `src/neural/` and `include/centroid_gai_neural.h`: working neural prototype.
- `src/gameplay/`, `include/centroid_gai_gameplay.h` and `tools/gameplay/`: composed specialists,
  deterministic joint training, quality/performance gates and C11 task scaffolding.
- `include/centroid_gai_chat.h`, `src/chat/`: implemented conversation protocol and codec.
- `persistence/api/`: HTTP service, native Node bridge, chat workers, research and memory.
- `persistence/db/`: model storage contracts and database migrations.
- `persistence/shared/`: versioned service, chat and research contracts.
- `tests/` and `data/chat/`: correctness checks and authored synthetic fixtures; generated repository training releases stay local under `build/`.
- `persistence/api/src/training/`: source ingestion, explicit review and dataset release commands.
- `src/knowledge/knowledge_catalog/` and `persistence/api/src/knowledge/`: supporting catalog
  and source-retrieval tools, distinct from neural model weights.

Licensed under [MIT](LICENSE).
