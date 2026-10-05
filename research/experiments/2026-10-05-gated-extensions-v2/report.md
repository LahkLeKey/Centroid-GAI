# Gated native extension results, instrumentation revision2

Same previously opened seeds/families; see protocol.md. No global recipe is promoted from these AUDIT results.

| Seed | Candidate | TRAIN loss; correct/16 reference -> candidate | DEV loss; correct/16 | AUDIT loss; correct/16 | Retention loss (tasks) | Gate | Model wire bytes | Checkpoint bytes | Contacts changed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 41 | shared-scale | 1.78750807; 7/16 -> 1.72499384; 7/16 | 4.16097467; 3/16 -> 4.14204228; 3/16 | 3.6573684; 4/16 -> 3.60831843; 4/16 | 1.78750807 -> 1.72499384 (64) | passed | 805720 -> 805720 | 851489 -> 851489 | 0 |
| 41 | world-policy | 1.78750807; 7/16 -> 1.78751685; 7/16 | 4.16097467; 3/16 -> 4.16098442; 3/16 | 3.6573684; 4/16 -> 3.6573795; 4/16 | 1.78750807 -> 1.78751685 (64) | passed | 805720 -> 805720 | 851489 -> 851489 | 3 |
| 41 | merge-0-1 | 2.88379993; 4/16 -> 3.29314881; 6/16 | 4.58054177; 2/16 -> 4.75973961; 3/16 | 4.24715261; 3/16 -> 4.48654549; 4/16 | 2.88379993 -> 3.29314881 (32) | rejected | 805720 -> 604488 | 831137 -> 631185 | 0 |
| 73 | shared-scale | 1.7868786; 8/16 -> 1.72052821; 8/16 | 4.18682089; 3/16 -> 4.16693061; 3/16 | 3.70936051; 4/16 -> 3.66039112; 4/16 | 1.7868786 -> 1.72052821 (64) | passed | 805720 -> 805720 | 851489 -> 851489 | 0 |
| 73 | world-policy | 1.7868786; 8/16 -> 1.84882004; 8/16 | 4.18682089; 3/16 -> 4.2230416; 3/16 | 3.70936051; 4/16 -> 3.75557662; 4/16 | 1.7868786 -> 1.84882004 (64) | rejected | 805720 -> 805720 | 851489 -> 851489 | 2 |
| 73 | merge-0-1 | 2.88903007; 6/16 -> 2.89393786; 6/16 | 4.60463871; 3/16 -> 4.61039933; 3/16 | 4.29777814; 3/16 -> 4.3016119; 3/16 | 2.88903007 -> 2.89393786 (32) | passed | 805720 -> 604488 | 831137 -> 631185 | 0 |
| 109 | shared-scale | 1.71467291; 8/16 -> 1.65380455; 8/16 | 4.13263454; 4/16 -> 4.11569905; 3/16 | 3.61095188; 5/16 -> 3.5651899; 5/16 | 1.71467291 -> 1.65380455 (64) | rejected | 805720 -> 805720 | 851489 -> 851489 | 0 |
| 109 | world-policy | 1.71467291; 8/16 -> 1.71466324; 8/16 | 4.13263454; 4/16 -> 4.1326; 4/16 | 3.61095188; 5/16 -> 3.6109523; 5/16 | 1.71467291 -> 1.71466324 (64) | passed | 805720 -> 805720 | 851489 -> 851489 | 3 |
| 109 | merge-0-1 | 2.96691443; 5/16 -> 3.18463353; 5/16 | 4.61187415; 4/16 -> 4.71658518; 3/16 | 4.2956525; 3/16 -> 4.47255394; 3/16 | 2.96691443 -> 3.18463353 (32) | rejected | 805720 -> 604488 | 831137 -> 631185 | 0 |

Elapsed: 2543403700 ns. Process peak resident/working-set: 16826368 bytes. Model allocation capacity is 805728 bytes per snapshot, irrespective of live owners. Raw probabilities and exact features are in predictions.tsv; clocks, update deltas, physical contacts/deferrals/reseeds, wire sizes and phase costs are in costs.tsv. Policy trace includes unmodified observations and proposed/published world identities. Parent and matched-reference checkpoints are retained for replay and rollback. Accepted gates do not establish fluent generation or general coding quality.
