# Gated native extension results

| Seed | Candidate | TRAIN loss parent/candidate | DEV loss parent/candidate | AUDIT loss parent/candidate | Retained tasks | Gate | Updates | Shared clock | Policy updates/edits | Groups |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 41 | shared-scale | 1.78750807 / 1.72499384 | 4.16097467 / 4.14204228 | 3.6573684 / 3.60831843 | 64 | passed | 64 | 32 | 0 / 0 | 4 |
| 41 | world-policy | 1.78750807 / 1.78751685 | 4.16097467 / 4.16098442 | 3.6573684 / 3.6573795 | 64 | passed | 64 | 0 | 179 / 34 | 4 |
| 41 | merge-0-1 | 2.88379993 / 3.29314881 | 4.58054177 / 4.75973961 | 4.24715261 / 4.48654549 | 32 | rejected | 32 | 0 | 0 / 0 | 3 |
| 73 | shared-scale | 1.7868786 / 1.72052821 | 4.18682089 / 4.16693061 | 3.70936051 / 3.66039112 | 64 | passed | 64 | 32 | 0 / 0 | 4 |
| 73 | world-policy | 1.7868786 / 1.84882004 | 4.18682089 / 4.2230416 | 3.70936051 / 3.75557662 | 64 | rejected | 64 | 0 | 172 / 30 | 4 |
| 73 | merge-0-1 | 2.88903007 / 2.89393786 | 4.60463871 / 4.61039933 | 4.29777814 / 4.3016119 | 32 | passed | 32 | 0 | 0 / 0 | 3 |
| 109 | shared-scale | 1.71467291 / 1.65380455 | 4.13263454 / 4.11569905 | 3.61095188 / 3.5651899 | 64 | rejected | 64 | 32 | 0 / 0 | 4 |
| 109 | world-policy | 1.71467291 / 1.71466324 | 4.13263454 / 4.1326 | 3.61095188 / 3.6109523 | 64 | passed | 64 | 0 | 177 / 56 | 4 |
| 109 | merge-0-1 | 2.96691443 / 3.18463353 | 4.61187415 / 4.71658518 | 4.2956525 / 4.47255394 | 32 | rejected | 32 | 0 | 0 / 0 | 3 |

Elapsed: 594 ms. Process peak resident/working-set bytes: 13852672. All candidates retained. The gate evaluates these fixed source prefixes only; it does not establish general reasoning, world optimality or safe arbitrary merges. Shared, policy and merge phases have different permissions/objectives and must be reported separately.
