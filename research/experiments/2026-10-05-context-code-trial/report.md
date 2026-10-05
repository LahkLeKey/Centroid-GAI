# Finite native C range-predicate experiment

This improvement is scoped to an authored integer-range fixture and four declared C candidates. It does not establish broad coding improvement or edit project source.

Parent SHA256: `e0a751b3accfd409bd29d7ead1b5dee298f73a74119a38116ae757c3f4d9a94d`

Independent evaluator SHA256: `9e0787c60bdb5de17226c4cd8b41f10a00431aa9906311e5d1dbcac451c710f4`

Frozen input SHA256: `d7ea167f4dfe270af6dc33778d7c6cf4358fd6c54ff13148c24dacb20be132b4`

Compiler: `clang`

TRAIN families: symmetric, singleton, reversed (13 authored cases). AUDIT families: negative, wide, extremes (17 separately authored cases). Every raw case, expected value, actual value and failure is retained in each candidate's logs.

| Action | Candidate | TRAIN failures / 13 | AUDIT failures / 17 |
| --- | --- | --- | --- |
| 0 | OR baseline | 7 | 8 |
| 1 | Lower only | 4 | 3 |
| 2 | Upper only | 4 | 3 |
| 3 | Both inclusive | 0 | 0 |

Winner action: 3. Selection uses TRAIN failures only; ties use the lowest action ID. AUDIT targets and outcomes stay in HOLDOUT records and never select a training target.

Frozen model choice before targets were revealed: 0. Measured TRAIN winner probability: 0.141070373086 before, 0.34519972627 after. Measured-label cross entropy: 1.95849641321 before, 1.06363211273 after.

Life generations: 8448 to 8704; genuine contacts: 256; model updates: 3; completed tasks: 3. Three independently measured TRAIN-family receipts are enqueued; only Life contact participants can update.

Review `winner.c`, `input.bin`, `evaluator.c`, each `candidate-N/candidate.c`, `compile.log`, `train.log`, `audit.log`, and `memory.centroid`. No candidate is applied to the codebase.

Attempted candidates: 4 / 4; verified native evaluations: 4 / 4.

Frozen publication gate: strictly fewer TRAIN failures than baseline and zero AUDIT failures. Gate passed: yes. Rejected candidates remain reviewable; only a passed gate gets `accepted.sha256`.

Elapsed experiment time: 1171 ms. Frozen input bytes: 28647. Checkpoint bytes: 770927; SHA256: `08355fb90456f492c821538579cac832b7ccc41353312ab8ca53a359cd3e10c0`.

| Action | Compile ms | TRAIN ms | AUDIT ms | Raw log bytes (compile/TRAIN/AUDIT) | Executable bytes |
| --- | --- | --- | --- | --- | --- |
| 0 | 172 | 62 | 31 | 0 / 568 / 926 | 139264 |
| 1 | 188 | 47 | 31 | 0 / 568 / 926 | 139264 |
| 2 | 172 | 47 | 31 | 0 / 568 / 926 | 139264 |
| 3 | 172 | 63 | 31 | 0 / 568 / 926 | 139264 |

Action 0 executable SHA256: `26694ab10adcbe80c5c8edc92b75e5cd7a63177dadec3d6015c3ea77c20d3042`.


Action 1 executable SHA256: `d327b76999277dbc279f94d1dee7bd54a1574dbfb72218eac8330aca4f9e0828`.


Action 2 executable SHA256: `4c4a56e470f4b752fb2042f3ab550ef13757a5bf631c8a54748ee746bda28553`.


Action 3 executable SHA256: `e566f823cb736da3422b75b37f390edefd6e14d73bf31d0585ff22dbcea18dea`.

