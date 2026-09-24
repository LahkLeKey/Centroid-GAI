/**
 * @file db.ts
 * @brief Shared Prisma ORM 8 PostgreSQL client for the `@centroid-gai/db` package.
 *
 * This is the only module in the repository that opens a PostgreSQL connection pool. The
 * `persistence/api` package imports the exported `db` client through the `@centroid-gai/db`
 * package export rather than reaching into `@prisma/orm-postgres` directly, so the connection
 * string, contract typings, and pool lifetime stay owned in one place.
 */

// Step 1: Load `DATABASE_URL` (and any other local overrides) from a `.env` file before anything
// else reads `process.env`. This mirrors `prisma.config.ts`, which needs the same variable.
import "dotenv/config";
// Step 2: Install the Temporal global polyfill. The generated contract types reference Temporal
// value types for `pg/*-temporal@1` codecs even though this schema currently uses string codecs,
// so the polyfill keeps those type/runtime references valid across supported Node versions.
import "temporal-polyfill/full/global";

import postgres from "@prisma/orm-postgres/runtime";

import type { Contract } from "./contract.d.ts";
// Step 3: Import the generated contract JSON as a value; it is the runtime description of the
// schema that the generated `Contract` type only describes at compile time.
import contractJson from "./contract.json" with { type: "json" };

const url = process.env.DATABASE_URL;

// Step 4: Fail fast during module load rather than surfacing a confusing error from the first
// query, since every consumer of this module needs a live connection string.
if (!url) {
    throw new Error("DATABASE_URL is required");
}

/**
 * Process-wide PostgreSQL client bound to the generated contract; import, do not re-instantiate.
 *
 * The pool is intentionally created at module load so every API request shares one contract-aware
 * connection manager. Consumers must call `close()` during process shutdown and must not create a
 * second client from the generated JSON, because separate pools make local Compose behavior and
 * graceful shutdown nondeterministic.
 */
export const db = postgres<Contract>({ contractJson, url });
