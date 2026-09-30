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
| Neural chatbot | Native structured dialogue engine, bounded workers, immutable artifacts and persistent HTTP conversations |
| Existing HTTP API | Serves the original count-based centroid engine and `.cgai` artifacts |
| Docker Compose | API and PostgreSQL with committed chat/memory migrations and restart/reload integration tests |
| Research and memory | Model-free startup, automatic bounded Wikipedia research, optional SearXNG and scoped memory controls; source results remain separate from neural synthesis |

The neural prototype uses `.cgnn` files. They cannot be served by the existing
`.cgai` HTTP endpoints. Neither engine currently provides a validated general
purpose conversational assistant. The fixed six-case chat fixture scored 0 exact
answers with 6 EOS terminations. Neural replies are experimental; the default chat
mode returns source excerpts, clarification or abstention. See the
[chat service guide](docs/chat-service.md) for commands and remaining release gates.

## Start here

1. [Build and check the project](docs/development.md).
2. [Train and evaluate the current neural prototype](docs/neural-centroid.md).
3. [Review the existing API and curl workflow](docs/api-contract.md).
4. [Train and chat through the API](docs/chat-service.md).

The [chatbot roadmap](docs/chatbot-plan.md) tracks the remaining quality and release gates.

The [documentation index](docs/README.md) connects the architecture, implementation
status, and research/memory design. Each guide labels planned behavior explicitly.

## Code map

- `src/neural_*.c` and `include/centroid_gai_neural.h`: working neural prototype.
- `include/centroid_gai_chat.h`, `src/chat_*.c`: implemented conversation protocol and codec.
- `persistence/api/`: HTTP service, native Node bridge, chat workers, research and memory.
- `persistence/db/`: model storage contracts and database migrations.
- `persistence/shared/`: versioned service, chat and research contracts.
- `tests/` and `examples/neural/`: correctness checks and small learning fixtures.
- `src/knowledge_catalog/` and `persistence/api/src/knowledge/`: supporting catalog
  and source-retrieval tools, distinct from neural model weights.

Licensed under [MIT](LICENSE).
