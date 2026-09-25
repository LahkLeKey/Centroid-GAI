#!/usr/bin/env -S node
import type { Contract as End } from '../../snapshots/41715d3222bc02b3e0efe52e4c24c9d3f4b3b00add860c91e6640164421f9c5b/contract';
import endContract from '../../snapshots/41715d3222bc02b3e0efe52e4c24c9d3f4b3b00add860c91e6640164421f9c5b/contract.json' with { type: 'json' };
import { Migration, MigrationCLI, col, fn, primaryKey } from '@prisma/orm-postgres/migration';

export default class M extends Migration<never, End> {
  override readonly endContractJson = endContract;

  override get operations() {
    return [
      this.createSchema({ schema: 'public' }),
      this.createTable({
        schema: 'public',
        table: 'model_artifact',
        columns: [
          col('centroidCount', 'int4', { notNull: true, codecRef: { codecId: 'pg/int4@1' } }),
          col('checksumSha256', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('contextWindow', 'int4', { notNull: true, codecRef: { codecId: 'pg/int4@1' } }),
          col('createdAt', 'timestamptz', {
            notNull: true,
            default: fn('now()'),
            codecRef: { codecId: 'pg/timestamptz-string@1' },
          }),
          col('dimensions', 'int4', { notNull: true, codecRef: { codecId: 'pg/int4@1' } }),
          col('examplesSeen', 'int8', { notNull: true, codecRef: { codecId: 'pg/int8@1' } }),
          col('formatVersion', 'int4', { notNull: true, codecRef: { codecId: 'pg/int4@1' } }),
          col('id', 'uuid', { notNull: true, codecRef: { codecId: 'pg/uuid@1' } }),
          col('libraryVersion', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('name', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('payload', 'bytea', { notNull: true, codecRef: { codecId: 'pg/bytea@1' } }),
          col('updatedAt', 'timestamptz', {
            notNull: true,
            default: fn('now()'),
            codecRef: { codecId: 'pg/timestamptz-string@1' },
          }),
          col('vocabularySize', 'int8', { notNull: true, codecRef: { codecId: 'pg/int8@1' } }),
        ],
        constraints: [primaryKey(['id'])],
      }),
      this.addUnique({
        schema: 'public',
        table: 'model_artifact',
        constraint: 'model_artifact_name_key',
        columns: ['name'],
      }),
      this.createIndex({
        schema: 'public',
        table: 'model_artifact',
        index: 'model_artifact_checksumSha256_idx_2437b8b7',
        columns: ['checksumSha256'],
      }),
    ];
  }
}

MigrationCLI.run(import.meta.url, M);
