/**
 * @file model-repository.ts
 * @brief Persists and retrieves complete `.cgai` model artifacts through `@centroid-gai/db`.
 *
 * This module is the only place in `persistence/api` that talks to the database. It hides the
 * generated Prisma contract shape (`db.orm.public.ModelArtifact`) behind small, intention-named
 * functions so `server.ts` and `cli.ts` never depend on the ORM query API directly.
 */
// The shared PostgreSQL client and generated contract types live in the sibling `db` package;
// see persistence/db/README.md for why they are not duplicated here.

import {db} from "@centroid-gai/db";
import {createHash, randomUUID} from "node:crypto";

import type {ModelMetadata} from "./native.ts";

/** Complete input required to atomically persist a model artifact. */
export interface SaveModelArtifact {
    readonly name: string;
    readonly payload: Uint8Array;
    readonly metadata: ModelMetadata;
}

/** Returns the lowercase hex SHA-256 digest stored as the artifact's integrity checksum. */
function checksum(payload: Uint8Array): string {
    return createHash("sha256").update(payload).digest("hex");
}

/**
 * Inserts or replaces a named model and returns its persisted row.
 *
 * The payload is treated as an opaque, already validated `.cgai` byte sequence. Metadata is
 * copied from native inspection rather than inferred from the request, and the SHA-256 checksum
 * is calculated here before the ORM call. The operation owns no input buffers and does not close
 * the shared database client; callers receive the ORM row or the database error unchanged.
 *
 * @param input Borrowed artifact name, bytes, and native-authoritative metadata. `name` is the
 * unique logical key; `payload` remains owned by the caller.
 * @returns The inserted or updated model row after PostgreSQL accepts the upsert.
 * @throws Any database or connection error raised by the shared Prisma client.
 */
export async function saveModelArtifact(input: SaveModelArtifact) {
    // Step 1: Build the mutable column values once so `create` and `update` cannot drift apart;
    // both branches of the upsert must persist an identical row shape for the same input.
    const values = {
        formatVersion : input.metadata.formatVersion,
        libraryVersion : input.metadata.libraryVersion,
        dimensions : input.metadata.dimensions,
        centroidCount : input.metadata.centroidCount,
        contextWindow : input.metadata.contextWindow,
        vocabularySize : input.metadata.vocabularySize,
        examplesSeen : input.metadata.examplesSeen,
        checksumSha256 : checksum(input.payload),
        payload : input.payload,
        updatedAt : new Date().toISOString(),
    };

    // Step 2: Upsert on the unique `name` column. A new row gets a fresh id and identity; an
    // existing row keeps its id and `createdAt` while every other column is replaced atomically,
    // so a `put` of an existing name can never leave centroids and metadata partially updated
    // relative to each other.
    return db.orm.public.ModelArtifact.upsert({
        create : {id : randomUUID(), name : input.name, ...values},
        update : values,
        conflictOn : {name : input.name},
    });
}

/**
 * Loads a named artifact, or returns null when it does not exist.
 *
 * The returned payload is a database-owned byte value represented by the ORM result. This
 * function performs no native validation because validation already occurs before persistence.
 *
 * @param name Borrowed unique logical model name.
 * @returns The matching row, or null when PostgreSQL has no row for `name`.
 * @throws Any database or connection error raised by the shared Prisma client.
 */
export async function loadModelArtifact(name: string) {
    return db.orm.public.ModelArtifact.where({name}).first();
}

/**
 * Returns persisted artifacts for the model catalog endpoint.
 *
 * The complete payload column is included by the ORM query and is intentionally projected away
 * by the HTTP metadata mapper. Callers that need the bytes should load one named artifact.
 *
 * @returns All model rows in the order supplied by the database query.
 * @throws Any database or connection error raised by the shared Prisma client.
 */
export async function listModelArtifacts() { return db.orm.public.ModelArtifact.all(); }

/**
 * Deletes a named artifact, returning whether a row was removed.
 *
 * Deletion is permanent for the database row and does not return the artifact bytes. The caller
 * is responsible for deciding whether a missing name is an HTTP 404 or another application error.
 *
 * @param name Borrowed unique logical model name.
 * @returns True when the ORM reports a deleted row, otherwise false.
 * @throws Any database or connection error raised by the shared Prisma client.
 */
export async function deleteModelArtifact(name: string): Promise<boolean> {
    const deleted = await db.orm.public.ModelArtifact.where({name}).delete();
    return deleted !== null;
}

/**
 * Closes the shared PostgreSQL connection pool during process shutdown.
 *
 * This function is process-lifetime infrastructure, not request cleanup. It must run after the
 * HTTP server stops accepting work so in-flight repository calls are not racing pool shutdown.
 *
 * @returns A promise that settles after the ORM has released its connections.
 * @throws Any pool-close error raised by the ORM runtime.
 */
export async function closeModelRepository(): Promise<void> { await db.close(); }
