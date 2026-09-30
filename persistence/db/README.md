# Centroid-GAI database package

This package owns PostgreSQL connections, the Prisma contract, generated types
and migrations. Product clients use the [HTTP API](../../docs/api-contract.md).
The API imports the shared client through `@centroid-gai/db`.

See the [architecture](../../docs/architecture.md) for storage boundaries and the
[chatbot plan](../../docs/chatbot-plan.md) for the neural conversation goal.

## Current storage status

Committed migrations create `model_artifact` and its optional `compositionJson`
column. Each named baseline model stores complete native `.cgai` bytes, a SHA-256
checksum, native metadata and timestamps. Training/upload replaces the artifact;
composition recipes preserve source identities separately from model bytes.

The contract and migration graph also include `NeuralChatArtifact`,
`NeuralChatModel`, `ChatConversation`, `ChatTrainingJob` and `ChatMemoryState`.
The `neural_chat` and `chat_memory` migrations extend the existing baseline schema.
Chat artifact bytes are immutable by checksum; conversation and memory documents
use compare-and-swap revisions. See the [chat service](../../docs/chat-service.md).

## Schema development

From `persistence/`, install the locked workspace dependencies once. Configure
`DATABASE_URL` in `persistence/db/.env` using `.env.example` as a template, then:

```sh
bun install --frozen-lockfile --ignore-scripts
cd db
bun run contract:emit
bun run typecheck
```

Edit `src/prisma/contract.prisma` as the schema source. Regenerate the contract,
plan and review a migration, then apply it to the intended development database:

```sh
bun run contract:emit
bun run migration:plan -- --name describe_the_change
bun run db:migrate
bun run db:verify
```

`db:init` is reserved for initializing an empty database; existing databases use
the migration graph. Generated types alone do not install tables. The repository
pins Prisma ORM/CLI release-candidate versions through package manifests and the
workspace lockfile.

Compose's `database-init` service executes `db:migrate` followed by `db:verify`;
the API waits for successful completion. Its PostgreSQL data lives in a named
volume retained by `docker compose down`. The isolated E2E test runner removes
its own test volume. See [development](../../docs/development.md) for startup,
test commands and the current API build blocker.
