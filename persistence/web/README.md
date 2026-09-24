# `@centroid-gai/web`

Frontend-only React console for the Centroid-GAI persistence API
([`docs/api-contract.md`](../../docs/api-contract.md)). Built with
[Bun](https://bun.sh), [Vite](https://vite.dev), React, and Tailwind CSS. It
only talks to the versioned `/api/v1` REST endpoints served by
[`@centroid-gai/api`](../api/README.md); it never touches PostgreSQL, Prisma,
or the native C ABI directly.

## Features

- List, refresh, and delete persisted models.
- Train a new model from pasted text and optional config overrides.
- Upload a `.cgai` artifact directly, or download an existing one.
- Generate a continuation from a selected model's persisted artifact.

## Setup

```sh
cd persistence
bun install
cd web
bun run dev
```

The dev server proxies `/api/*` requests to `http://localhost:3000` (see
`vite.config.ts`); start `@centroid-gai/api` (or the Compose stack) first.
Copy `.env.example` to `.env` to point at a different API origin instead of
using the dev proxy.

## Build

```sh
bun run build
```

Outputs a static bundle in `dist/` that can be served by any static host or
reverse proxy in front of the API.

## End-to-end tests

```sh
bunx playwright install chromium   # first run only
bun run test:e2e
```

Playwright drives a real Chromium browser against the Vite dev server (auto
started on port 4173) and a running `@centroid-gai/api` instance (defaults to
`http://localhost:3000`; override with `VITE_API_PROXY_TARGET`). Tests create
uniquely named fixture models through the API and delete them in
`afterAll`/inline cleanup, so they are safe to run against a shared database
without disturbing seeded demo models.

