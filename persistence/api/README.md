# Centroid-GAI API

This package owns the HTTP boundary and Node-API bridge to native C. The product
goal is a centroid neural chatbot accessed through HTTP, curl and Docker Compose.
The router serves baseline `.cgai` artifacts and the separate neural chat engine.

See the [documentation index](../../docs/README.md),
[current HTTP contract](../../docs/api-contract.md), and
[chatbot plan](../../docs/chatbot-plan.md).

## Neural conversations

The native bridge, workers, conversation routes, scoped memory and migrations
are implemented. See the [chat service guide](../../docs/chat-service.md) for
authentication, curl fixtures, source-mode behavior and remaining quality gates.
The fixed six-case neural fixture has not demonstrated usable answer quality.

## Compose workflow

Run from the repository root with Docker running:

```sh
docker compose up --build --detach --wait
curl --fail-with-body http://localhost:3000/api/v1/health
curl --fail-with-body -X POST http://localhost:3000/api/v1/models/demo/train -H "Content-Type: application/json" --data-binary @examples/api/train.json
curl --fail-with-body -X POST http://localhost:3000/api/v1/models/demo/generate -H "Content-Type: application/json" --data-binary @examples/api/generate.json
```

Use `curl.exe` in Windows PowerShell if `curl` is an alias. The example trains or
replaces `demo` and returns a baseline continuation. It does not create a neural
conversation. Compose builds the native addon, runs migrations and schema
verification, and starts PostgreSQL plus the API. `docker compose down` retains
the model volume. `API_PORT` and `POSTGRES_PORT` override exposed ports 3000/5432.

## Package development

Use Node 24.11+ and Bun 1.3.9. From the repository root:

```sh
cd persistence
bun install --frozen-lockfile --ignore-scripts
bun run --cwd api native:build
bun run --cwd api typecheck
bun run --cwd api native:test
```

The addon build needs a C toolchain, Python and Node headers. For a host API
process, configure `DATABASE_URL` in `persistence/api/.env`, prepare the database
through the [database package](../db/README.md), then run `bun run --cwd api start`
from `persistence/`. Keep credentials out of source control.

`bun run test:e2e` from `persistence/` builds an isolated Compose project and runs
HTTP integration tests. It defaults to ports 3100/55432, configurable with
`CGAI_E2E_API_PORT`/`CGAI_E2E_POSTGRES_PORT`, then removes its own containers and
volumes. It leaves the normal development stack in place. See
[development](../../docs/development.md) for the complete validation workflow.
