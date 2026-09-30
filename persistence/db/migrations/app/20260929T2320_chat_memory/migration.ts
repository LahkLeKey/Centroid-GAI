#!/usr/bin/env -S node
import type { Contract as Start } from '../../snapshots/cb4c739525a83a6be29faf37f6155532e07d066335cf3675a268a28577561a4a/contract';
import startContract from '../../snapshots/cb4c739525a83a6be29faf37f6155532e07d066335cf3675a268a28577561a4a/contract.json' with { type: 'json' };
import type { Contract as End } from '../../snapshots/eac349e27f857f29cbde0f728f674e45725aecc3dd4d18bebd7fbc97d073359d/contract';
import endContract from '../../snapshots/eac349e27f857f29cbde0f728f674e45725aecc3dd4d18bebd7fbc97d073359d/contract.json' with { type: 'json' };
import { Migration, MigrationCLI, col, fn, primaryKey } from '@prisma/orm-postgres/migration';

export default class M extends Migration<Start, End> {
  override readonly startContractJson = startContract;
  override readonly endContractJson = endContract;

  override get operations() {
    return [
      this.createTable({
        schema: 'public',
        table: 'chat_memory_state',
        columns: [
          col('document', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('ownerId', 'text', { notNull: true, codecRef: { codecId: 'pg/text@1' } }),
          col('revision', 'int4', { notNull: true, codecRef: { codecId: 'pg/int4@1' } }),
          col('updatedAt', 'timestamptz', {
            notNull: true,
            default: fn('now()'),
            codecRef: { codecId: 'pg/timestamptz-string@1' },
          }),
        ],
        constraints: [primaryKey(['ownerId'])],
      }),
    ];
  }
}

MigrationCLI.run(import.meta.url, M);
