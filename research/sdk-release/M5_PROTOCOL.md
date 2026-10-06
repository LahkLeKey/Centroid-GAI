# Registered M5 finite code-helper qualification

Registered October 5, 2026, before creating or evaluating this trial's native
TRAIN, DEV or AUDIT workloads. This replaces no earlier opened research trial.

The feature recommends one of four correct implementations of the repository's
`c_lower_bound` contract for a bounded sorted-array workload. Action 0 is the
current production binary search; 1 is a forward scan; 2 a reverse scan; 3 a
galloping prefix followed by binary search. All candidates must return the first
index whose value is at least the key, including empty arrays and duplicates.
An independent complete linear oracle checks every candidate. Measured value
comparisons, not labels written by an LLM, select the cheapest correct TRAIN
action; ties select the lowest action ID. Every candidate result is retained.

Profile: `lower-bound-strategy/v1`. Observation: two exact bytes, version 1 and
a target-free workload band. The band uses only array count, first/last value
and key: empty, before-first, after-last, estimated first three positions,
estimated first 24 positions, interior, estimated last eight positions, or
estimated positions 64 through 256. The estimate is bounded integer arithmetic.
The encoder remains the registered native `byte-pos32/v1`; original observation
bytes and complete workload arrays are retained. No teacher result is an input.
This is bounded workload strategy selection, not generated source, arbitrary
code understanding, or an LLM coding improvement claim.

Fresh workload families use different native sorted-value constructions:
TRAIN uses affine increasing values and paired duplicate values; DEV uses
periodic extra gaps and triples of equal values; AUDIT uses cyclic unequal gaps
and periodic repeated values. Each split contains 40 independently verified
workloads: five cases in each of eight requested regions. Nonempty arrays have
between 2,048 and 4,096 entries. Arrays, keys and all candidate outcomes are
reported even where different requested regions yield the same observed band.
AUDIT is generated and evaluated only after the fixed recipe passes DEV.
No DEV/AUDIT records, arrays, result summaries or labels enter the trainer store.
These are fresh case families, not reused range, affine-sizing, saturation or
previous binary/linear correctness fixtures. Observation bands may repeat across
splits; the claim is transfer to new workloads within this declared schema.

Seeds are 1409, 3251 and 7907. Two owned experts use the existing frozen shared
representation and CODE head, with 12 epochs of 40 independently measured TRAIN
receipts: exactly 480 joint updates and 960 owner updates per learned condition.
Every epoch permits at most 2,048 scheduling generations. Conditions are native
production Life, frozen physical Life, and deterministic research scheduling.
They start with the same model, task order, inputs, optimizer and update budget.
The deterministic comparator is isolated research and cannot be exported as a
production checkpoint. A zero-update frozen model is reported separately.
Default production binary search and an endpoint/first-two/last-two heuristic
are deterministic task baselines. The exhaustive measured minimum is also
reported as an oracle ceiling, including its additional verification work.

Acceptance is fixed before evaluation. Each Life seed must have zero semantic
failures and reduce aggregate DEV and AUDIT comparisons by at least 20% versus
production binary, at least 10% versus the deterministic endpoint heuristic,
and at least 10% versus its frozen-model choice. All 480 updates must occur at
physical contacts. Eight fixed source-bound TEXT retention probes must increase
mean cross entropy by no more than 0.05; TEXT readouts and optimizer moments must
remain bitwise unchanged. Default shared/policy clocks remain zero. No Life
superiority claim follows from a tie or a failed comparative margin.

The release candidate is the first registered seed, 1409, if all three Life seeds
pass; selection cannot inspect AUDIT to choose a seed. No learning recipe changes
after opening AUDIT. A failed DEV gate leaves AUDIT unopened and every failed
candidate retained. Any subsequent changed recipe requires a new registration;
already opened AUDIT families cannot become a new independent quality trial.

Resource envelope: at most 4,096 array entries per case, 40 cases per split,
1,024 measurement receipts per trainer, 16 MiB observation/measurement artifact
reads, 128 MiB measured process peak memory, 120 seconds per complete native
qualification and at most 10 ms mean bounded inference per observation. Report
encoding, selection, scheduling, fitting, evaluation and whole-run wall times;
parameters, serialized model/checkpoint bytes, actual updates and per-owner
clocks; and native process high-water memory. Matching updates does not match
total compute. Comparison savings are algorithm work, not a wall-clock speedup:
the host requests a recommendation outside its search hot path and owns use of
the verified implementation.

Qualification binds the exact immutable runtime value-model digest, registered
profile/observation/action semantics, current production-source identity,
independent verifier/tool executable identity, training checkpoint and selected
parent. A qualified bundle is exported only after every fixed gate passes.
It contains inference values and metadata, never private source/work context,
optimizer, Life state, receipt ledger or research workloads. The parent remains
available for rollback. Reproduction uses this native recipe and keeps all raw
attempts; repeated runs are conformance/reproduction evidence, not new AUDIT.
