#!/usr/bin/env -S node
import type { Contract as Start } from '../../snapshots/62ac50525fbaaab1bd1d00c278adbee7a99382172e57a7da5f8d4e4a62b6daef/contract';
import startContract from '../../snapshots/62ac50525fbaaab1bd1d00c278adbee7a99382172e57a7da5f8d4e4a62b6daef/contract.json' with { type: 'json' };
import type { Contract as End } from '../../snapshots/cb4c739525a83a6be29faf37f6155532e07d066335cf3675a268a28577561a4a/contract';
import endContract from '../../snapshots/cb4c739525a83a6be29faf37f6155532e07d066335cf3675a268a28577561a4a/contract.json' with { type: 'json' };
import { Migration, MigrationCLI, col, fn, primaryKey } from '@prisma/orm-postgres/migration';

export default class M extends Migration<Start, End> {
  override readonly startContractJson = startContract;
  override readonly endContractJson = endContract;

  override get operations() {
    return [
      this.createTable({
        schema: 'public',
        table: 'chat_conversation',
        columns: [
          col('document', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('id', 'uuid', { notNull: true, codecRef: { codecId: 'pg/uuid@1' } }),
          col('revision', 'int4', { notNull: true, codecRef: { codecId: 'pg/int4@1' } }),
          col('updatedAt', 'timestamptz', {
            notNull: true,
            default: fn('now()'),
            codecRef: { codecId: 'pg/timestamptz-string@1' },
          }),
        ],
        constraints: [primaryKey(['id'])],
      }),
      this.createTable({
        schema: 'public',
        table: 'chat_training_job',
        columns: [
          col('document', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('id', 'uuid', { notNull: true, codecRef: { codecId: 'pg/uuid@1' } }),
          col('updatedAt', 'timestamptz', {
            notNull: true,
            default: fn('now()'),
            codecRef: { codecId: 'pg/timestamptz-string@1' },
          }),
        ],
        constraints: [primaryKey(['id'])],
      }),
      this.createTable({
        schema: 'public',
        table: 'neural_chat_artifact',
        columns: [
          col('checksumSha256', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('createdAt', 'timestamptz', {
            notNull: true,
            default: fn('now()'),
            codecRef: { codecId: 'pg/timestamptz-string@1' },
          }),
          col('id', 'uuid', { notNull: true, codecRef: { codecId: 'pg/uuid@1' } }),
          col('metadataJson', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('payload', 'bytea', { notNull: true, codecRef: { codecId: 'pg/bytea@1' } }),
          col('provenanceJson', 'text', { codecRef: { codecId: 'pg/text@1' } }),
        ],
        constraints: [primaryKey(['id'])],
      }),
      this.createTable({
        schema: 'public',
        table: 'neural_chat_model',
        columns: [
          col('checksumSha256', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('name', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('updatedAt', 'timestamptz', {
            notNull: true,
            default: fn('now()'),
            codecRef: { codecId: 'pg/timestamptz-string@1' },
          }),
        ],
        constraints: [primaryKey(['name'])],
      }),
      this.addUnique({
        schema: 'public',
        table: 'neural_chat_artifact',
        constraint: 'neural_chat_artifact_checksumSha256_key',
        columns: ['checksumSha256'],
      }),
    ];
  }
}

MigrationCLI.run(import.meta.url, M);
