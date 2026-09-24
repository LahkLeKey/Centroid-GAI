/**
 * @file prisma.config.ts
 * @brief Prisma CLI configuration for the `@centroid-gai/db` package.
 *
 * Every `prisma` CLI invocation (`contract:emit`, `db:init`, `db:verify`, `migration:plan`,
 * `db:migrate`) reads this file to find the contract source, the connection string, and the
 * migrations directory. It intentionally stays inside `persistence/db` rather than
 * `persistence/api`, because migrations and contract generation are database-lifecycle
 * operations that the API package does not perform.
 */
import "dotenv/config";

import { defineConfig as ormConfig } from "@prisma/orm-postgres/config";
import { definePrismaConfig } from "prisma/config";

const connection = process.env.DATABASE_URL;

// Step 1: Require an explicit connection string. The CLI has no safe default, and a missing
// value should stop the command instead of silently targeting an unintended database.
if (!connection) {
    throw new Error("DATABASE_URL is required");
}

// Step 2: Point the ORM at the single contract source file and the migrations directory that
// `prisma migration plan`/`db migrate` read from and write into.
export default definePrismaConfig({
    orm: ormConfig({
        contract: "./src/prisma/contract.prisma",
        db: { connection },
        migrations: { dir: "./migrations" },
    }),
    skills: { check: false },
});
