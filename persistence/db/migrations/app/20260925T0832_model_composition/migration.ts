#!/usr/bin/env -S node
import type { Contract as Start } from '../../snapshots/41715d3222bc02b3e0efe52e4c24c9d3f4b3b00add860c91e6640164421f9c5b/contract';
import startContract from '../../snapshots/41715d3222bc02b3e0efe52e4c24c9d3f4b3b00add860c91e6640164421f9c5b/contract.json' with { type: 'json' };
import type { Contract as End } from '../../snapshots/62ac50525fbaaab1bd1d00c278adbee7a99382172e57a7da5f8d4e4a62b6daef/contract';
import endContract from '../../snapshots/62ac50525fbaaab1bd1d00c278adbee7a99382172e57a7da5f8d4e4a62b6daef/contract.json' with { type: 'json' };
import { Migration, MigrationCLI, col } from '@prisma/orm-postgres/migration';

export default class M extends Migration<Start, End> {
  override readonly startContractJson = startContract;
  override readonly endContractJson = endContract;

  override get operations() {
    return [
      this.addColumn({
        schema: 'public',
        table: 'model_artifact',
        column: col('compositionJson', 'text', { codecRef: { codecId: 'pg/text@1' } }),
      }),
    ];
  }
}

MigrationCLI.run(import.meta.url, M);
