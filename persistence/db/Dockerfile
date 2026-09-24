# @file Dockerfile
# @brief Builds the database-lifecycle image for `@centroid-gai/db` using bun as the package
# manager and script runner.
#
# This image never links or builds the native C addon; it only needs the Prisma CLI. Keeping it
# separate from `persistence/api/Dockerfile` means the `database-init` Compose service starts
# (and rebuilds) without paying for a C toolchain layer.

# Step 1: Install workspace dependencies for `@centroid-gai/db`. The root workspace manifest and
# lockfile are required so bun recognizes this as a workspace member and resolves exact versions.
FROM oven/bun:1 AS build

WORKDIR /workspace/persistence

COPY persistence/package.json persistence/bun.lock ./
COPY persistence/db/package.json ./db/package.json
COPY persistence/api/package.json ./api/package.json
RUN bun install --frozen-lockfile

COPY persistence/db/ ./db/

WORKDIR /workspace/persistence/db

# Step 2: Regenerate the contract types from `contract.prisma` during the image build so a stale
# checked-in `contract.json`/`contract.d.ts` can never silently ship. The connection string here
# only needs to parse; `contract emit` does not open a database connection.
RUN DATABASE_URL=postgresql://contract:emit@localhost/unused bun run contract:emit \
    && bun run typecheck

# Step 3: Copy only the finished package into the runtime image; no build tools ship to
# production.
FROM oven/bun:1 AS runtime

ENV NODE_ENV=production
WORKDIR /app/persistence/db

COPY --from=build /workspace/persistence/db/ ./

# The default command verifies the live schema and falls back to first-time initialization.
# Compose's `database-init` service uses this default; `db:migrate`/`migration:plan` are run
# manually by an operator, not on every container start.
CMD ["bun", "run", "db:verify"]
