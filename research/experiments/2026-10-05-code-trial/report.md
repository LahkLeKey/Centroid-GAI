# Finite native C range-predicate experiment

This improvement is scoped to an authored integer-range fixture and four declared C candidates. It does not establish broad coding improvement or edit project source.

Parent SHA256: `e0a751b3accfd409bd29d7ead1b5dee298f73a74119a38116ae757c3f4d9a94d`

Independent evaluator SHA256: `9e0787c60bdb5de17226c4cd8b41f10a00431aa9906311e5d1dbcac451c710f4`

Frozen input SHA256: `c172aef6fb52209e61d11e8f817049bc2d558607373c8ec9f1e081283fefe04d`

Compiler: `clang`

TRAIN families: symmetric, singleton, reversed (13 authored cases). AUDIT families: negative, wide, extremes (17 separately authored cases). Every raw case, expected value, actual value and failure is retained in each candidate's logs.

| Action | Candidate | TRAIN failures / 13 | AUDIT failures / 17 |
| --- | --- | --- | --- |
| 0 | OR baseline | 7 | 8 |
| 1 | Lower only | 4 | 3 |
| 2 | Upper only | 4 | 3 |
| 3 | Both inclusive | 0 | 0 |

Winner action: 3. Selection uses TRAIN failures only; ties use the lowest action ID. AUDIT targets and outcomes stay in HOLDOUT records and never select a training target.

Frozen model choice before targets were revealed: 3. Measured TRAIN winner probability: 0.264724873699 before, 0.691566375875 after. Measured-label cross entropy: 1.32906420477 before, 0.368796144233 after.

Life generations: 8192 to 8448; genuine contacts: 256; model updates: 3; completed tasks: 3. Three independently measured TRAIN-family receipts are enqueued; only Life contact participants can update.

Review `winner.c`, `input.bin`, `evaluator.c`, each `candidate-N/candidate.c`, `compile.log`, `train.log`, `audit.log`, and `memory.centroid`. No candidate is applied to the codebase.

Attempted candidates: 4 / 4; verified native evaluations: 4 / 4.

Frozen publication gate: strictly fewer TRAIN failures than baseline and zero AUDIT failures. Gate passed: yes. Rejected candidates remain reviewable; only a passed gate gets `accepted.sha256`.

Elapsed experiment time: 1171 ms. Frozen input bytes: 6017. Checkpoint bytes: 748541; SHA256: `d337e85c52ec2de1c8adca22f88eea94b2bec6fee7391a0822177abcdb66f003`.

| Action | Compile ms | TRAIN ms | AUDIT ms | Raw log bytes (compile/TRAIN/AUDIT) | Executable bytes |
| --- | --- | --- | --- | --- | --- |
| 0 | 187 | 47 | 31 | 0 / 568 / 926 | 139264 |
| 1 | 188 | 47 | 31 | 0 / 568 / 926 | 139264 |
| 2 | 172 | 62 | 32 | 0 / 568 / 926 | 139264 |
| 3 | 156 | 63 | 31 | 0 / 568 / 926 | 139264 |

Action 0 executable SHA256: `472c5e448706e9d56231b9d6906eea49c0259863e61318144187bcafd8f4f909`.


Action 1 executable SHA256: `ed23e272e2da57b9c874943dc5cbdc98a89be04ed7ab9f97dc7e8ec223c18ae2`.


Action 2 executable SHA256: `8f65b7a0f6fc1fc2a1a049d102203ff406ee7a3f91d4ac905d3e6282428131bb`.


Action 3 executable SHA256: `c03d3091e834310b2d5bce974b59df75afc226d6574abb01f7adf115a9bd4fdf`.

