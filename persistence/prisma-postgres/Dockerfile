FROM node:24-bookworm-slim AS build

RUN apt-get update \
    && apt-get install --yes --no-install-recommends build-essential python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/persistence/prisma-postgres

COPY persistence/prisma-postgres/package.json persistence/prisma-postgres/package-lock.json ./
RUN npm ci --ignore-scripts

COPY persistence/prisma-postgres/ ./
COPY include/ /workspace/include/
COPY src/ /workspace/src/

RUN npm run native:build \
    && DATABASE_URL=postgresql://contract:emit@localhost/unused npm run contract:emit \
    && npm run typecheck

FROM node:24-bookworm-slim AS runtime

ENV NODE_ENV=production
WORKDIR /app

COPY --from=build /workspace/persistence/prisma-postgres/ ./

EXPOSE 3000
CMD ["npm", "start"]

