# Persistence architecture

PostgreSQL is the durable model store. Prisma ORM 8 owns the database contract,
migrations, and queries in `persistence/db/`; the HTTP/CLI application boundary
lives in `persistence/api/`, which depends on `persistence/db/` and is the only
way callers reach it. The C library does not link a Node.js or PostgreSQL
client.

The integration boundary is a versioned `.cgai` artifact:

1. the C library trains and serializes a complete model;
2. the Prisma adapter validates its fixed header and calculates SHA-256;
3. PostgreSQL stores the payload atomically as `bytea` plus queryable metadata;
4. loading returns the exact bytes to `cgai_model_load`.

Storing an artifact as one row guarantees that centroids, token counts, and the
vocabulary cannot become partially updated relative to one another. Metadata is
kept in ordinary columns for discovery and compatibility checks. Large training
corpora, evaluations, and lineage should be separate related models rather than
fields on the artifact row.

The version-1 C format currently uses native numeric representations. Until a
portable format is introduced, producer and consumer must use compatible byte
order and floating-point representations. Database storage does not remove that
constraint.

