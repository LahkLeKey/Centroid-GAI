import { createHash, randomUUID } from "node:crypto";
import { db } from "./prisma/db.ts";
import type { ModelMetadata } from "./native.ts";

/** Complete input required to atomically persist a model artifact. */
export interface SaveModelArtifact {
  readonly name: string;
  readonly payload: Uint8Array;
  readonly metadata: ModelMetadata;
}

function checksum(payload: Uint8Array): string {
  return createHash("sha256").update(payload).digest("hex");
}

/** Inserts or replaces a named model and returns its persisted row. */
export async function saveModelArtifact(input: SaveModelArtifact) {
  const values = {
    formatVersion: input.metadata.formatVersion,
    libraryVersion: input.metadata.libraryVersion,
    dimensions: input.metadata.dimensions,
    centroidCount: input.metadata.centroidCount,
    contextWindow: input.metadata.contextWindow,
    vocabularySize: input.metadata.vocabularySize,
    examplesSeen: input.metadata.examplesSeen,
    checksumSha256: checksum(input.payload),
    payload: input.payload,
    updatedAt: new Date().toISOString(),
  };

  return db.orm.public.ModelArtifact.upsert({
    create: { id: randomUUID(), name: input.name, ...values },
    update: values,
    conflictOn: { name: input.name },
  });
}

/** Loads a named artifact, or returns null when it does not exist. */
export async function loadModelArtifact(name: string) {
  return db.orm.public.ModelArtifact.where({ name }).first();
}

/** Deletes a named artifact, returning whether a row was removed. */
export async function deleteModelArtifact(name: string): Promise<boolean> {
  const deleted = await db.orm.public.ModelArtifact.where({ name }).delete();
  return deleted !== null;
}

/** Closes the shared PostgreSQL connection pool during process shutdown. */
export async function closeModelRepository(): Promise<void> {
  await db.close();
}
