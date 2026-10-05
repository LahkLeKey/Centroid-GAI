# Finite native C range-predicate experiment

This improvement is scoped to an authored integer-range fixture and four declared C candidates. It does not establish broad coding improvement or edit project source.

Parent SHA256: `e0a751b3accfd409bd29d7ead1b5dee298f73a74119a38116ae757c3f4d9a94d`

Independent evaluator SHA256: `9e0787c60bdb5de17226c4cd8b41f10a00431aa9906311e5d1dbcac451c710f4`

Frozen input SHA256: `78e67d4b71ddff0ef5a09fa9733042de13902ce3636f0b3e86d68ec7190826ca`

Frozen pretrial model-parent checkpoint SHA256: `cb12383ab9e6e37f7853960da54c8c48e32cdc8227751a1d7efdfb554cfdebf5`. Physical contact proof SHA256: `b2cedd1daf78834f66af2b12a0be7f24262fe156e597bc39240f03664c6197f8`; generation=15360 eligible=3 participants=3 contact-graph=2. Both immutable artifacts precede every tool trial and are bound into every TRAIN receipt.

Compiler: `clang`

TRAIN families: symmetric, singleton, reversed (13 authored cases). AUDIT families: negative, wide, extremes (17 separately authored cases). Every raw case, expected value, actual value and failure is retained in each candidate's logs.

| Action | Candidate | TRAIN failures / 13 | AUDIT failures / 17 |
| --- | --- | --- | --- |
| 0 | OR baseline | 7 | 8 |
| 1 | Lower only | 4 | 3 |
| 2 | Upper only | 4 | 3 |
| 3 | Both inclusive | 0 | 0 |

Winner action: 3. Selection uses TRAIN failures only; ties use the lowest action ID. AUDIT targets and outcomes stay in HOLDOUT records and never select a training target.

Frozen model choice before targets were revealed: 2. Measured TRAIN winner probability: 0.160096393177 before, 0.334138461079 after. Measured-label cross entropy: 1.8319791878 before, 1.09619981778 after.

Life generations: 15360 to 15616; genuine contacts: 256; model updates: 3; completed tasks: 3. Three independently measured TRAIN-family receipts are enqueued; only Life contact participants can update.

Review `winner.c`, `input.bin`, `evaluator.c`, each `candidate-N/candidate.c`, `compile.log`, `train.log`, `audit.log`, and `memory.centroid`. No candidate is applied to the codebase.

Attempted candidates: 4 / 4; verified native evaluations: 4 / 4.

Frozen publication gate: strictly fewer TRAIN failures than baseline and zero AUDIT failures. Gate passed: yes. Rejected candidates remain reviewable; only a passed gate gets `accepted.sha256`.

Elapsed experiment time: 5719 ms. Frozen input bytes: 35396. Checkpoint bytes: 1972877; SHA256: `96ea574b2160efea14438d1292450e0b6ca99eb27abfcc54a38a016a1348e5f1`.

| Action | Compile ms | TRAIN ms | AUDIT ms | Raw log bytes (compile/TRAIN/AUDIT) | Executable bytes |
| --- | --- | --- | --- | --- | --- |
| 0 | 234 | 2078 | 484 | 0 / 568 / 926 | 139264 |
| 1 | 250 | 312 | 281 | 0 / 568 / 926 | 139264 |
| 2 | 219 | 297 | 281 | 0 / 568 / 926 | 139264 |
| 3 | 235 | 328 | 391 | 0 / 568 / 926 | 139264 |

Action 0 executable SHA256: `09f284b9c79cf4e9a4db1a4b6d926a86762e777cd60fbd6bc967344e9d5f565d`.


Action 1 executable SHA256: `1e040cc7da97305cae2ed4081cd401671f87002a0e9e9f9f0c29b2d14c0af5c5`.


Action 2 executable SHA256: `ff540dd6d42721201f8eca4350ef64e1abc78d341b9ead78de681eea3e475968`.


Action 3 executable SHA256: `a392d368d603bd6343f8299acfae1ca830364e518088ed4050bc2273b8a19b9c`.

