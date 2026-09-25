# `@centroid-gai/db`

Owns every PostgreSQL and Prisma ORM 8 concern: the contract (schema) source,
generated contract types, migrations, and the shared `db` client. It has no
knowledge of HTTP, the native C addon, or Node-API bindings — those live in
[`persistence/api`](../api/README.md), which depends on this package.

Prisma ORM 8 is currently a release candidate. Package versions are pinned so
an upstream release cannot silently change the contract or query API.

## Setup

Use [bun](https://bun.sh) 1.3 or newer. `persistence/` is a bun workspace
containing both this package and `../api`; install once from the workspace
root. Copy `.env.example` to `.env`, replace the connection string, then run:

```sh
cd .. && bun install
cd db
bun run contract:emit
bun run db:init
bun run typecheck
```

## Changing the schema

After editing `src/prisma/contract.prisma`, regenerate types, plan a migration,
review it, then apply it:

```sh
bun run contract:emit
bun run migration:plan -- --name describe_the_change
bun run db:migrate
```

`db:init` is for an empty database only; use the migration flow above for an
existing one.

Committed migrations now include the initial artifact schema and the additive
`compositionJson` column for immutable superset recipes. `docker compose up -d`
runs the migration graph and verifies the schema through `database-init`; it
works from an empty database or the prior initialized artifact contract. Existing
model payloads remain unchanged. `compositionJson` is null for ordinary models.

## Consuming this package from `persistence/api`

`persistence/api` depends on this package as a bun workspace dependency
(`workspace:*` in package.json) and imports the shared client as
`@centroid-gai/db`:

```ts
import { db } from "@centroid-gai/db";
```

Run `bun install` from `persistence/` (the workspace root) after cloning or
after changing either package's dependencies, so the workspace link and both
packages' `node_modules` stay consistent.

The database package is exercised by the Compose E2E flow documented in
[`persistence/api`](../api/README.md#compose-e2e). It is started as the
`database-init` service, verifies or initializes the contract against the local
PostgreSQL container, and then remains available to the API container for the
real training/persistence requests.

The reproducible example-model seed also uses this database boundary indirectly
through the API. This keeps model initialization independent of Prisma CLI
details: a fresh database only needs `database-init` to complete before
`bun run seed:models` trains the committed corpora into PostgreSQL.
