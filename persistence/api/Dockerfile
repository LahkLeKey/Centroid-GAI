# @file Dockerfile
# @brief Builds the HTTP API image: the native C addon plus the `@centroid-gai/db` client it
# depends on.
#
# This image needs a C toolchain (for `node-gyp`) that `persistence/db/Dockerfile` does not, so
# the two packages build as separate images even though this one embeds a copy of `db/`. It also
# needs the real Node.js runtime (not just bun) because the compiled addon is a Node-API binary
# built against Node headers; bun is used only as the package manager and script runner here.

FROM oven/bun:1 AS bun

FROM node:24-bookworm-slim AS build

# Step 1: Borrow the `bun` binary from the official bun image rather than installing Node.js on
# top of it, since node-gyp and the compiled addon both need a genuine Node.js runtime.
COPY --from=bun /usr/local/bin/bun /usr/local/bin/bun

RUN apt-get update \
    && apt-get install --yes --no-install-recommends build-essential python3 \
    && rm -rf /var/lib/apt/lists/*

# Step 2: Install both workspace members in one `bun install` so the local `@centroid-gai/db`
# workspace link (see persistence/package.json) resolves before any package-specific step runs.
WORKDIR /workspace/persistence
COPY persistence/package.json persistence/bun.lock ./
COPY persistence/db/package.json ./db/package.json
COPY persistence/api/package.json ./api/package.json
RUN bun install --frozen-lockfile --ignore-scripts

# Step 3: Regenerate the `db` package's generated contract types, then bring in the native addon
# sources and the shared C library it wraps.
COPY persistence/db/ ./db/
COPY persistence/api/ ./api/
COPY include/ /workspace/include/
COPY src/ /workspace/src/

WORKDIR /workspace/persistence/db
RUN DATABASE_URL=postgresql://contract:emit@localhost/unused bun run contract:emit

# Step 4: Compile the native addon against the copied C sources, then typecheck the API package
# together with the linked `db` package so a contract mismatch fails the build, not a container.
WORKDIR /workspace/persistence/api
RUN bun run native:build \
    && bun run typecheck

FROM node:24-bookworm-slim AS runtime

ENV NODE_ENV=production
WORKDIR /app

# Step 5: Carry both packages into the runtime image at the same relative layout so the
# `@centroid-gai/db` workspace symlink keeps resolving at container start. Only Node.js runs at
# runtime; bun is not needed once the addon is built and dependencies are installed.
COPY --from=build /workspace/persistence/node_modules/ /app/persistence/node_modules/
COPY --from=build /workspace/persistence/db/ /app/persistence/db/
COPY --from=build /workspace/persistence/api/ /app/persistence/api/

WORKDIR /app/persistence/api

EXPOSE 3000
CMD ["node", "src/server.ts"]

