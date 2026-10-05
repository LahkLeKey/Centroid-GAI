# Frozen source-quality protocol

Recipe: `centroid-life/1:byte-pos32:role-v1:mix-softmax:adam-owned:b3s23-torus16`. Seed: 20031; groups: 2; eligibility: 3.

This protocol is published before any AUDIT prediction. Families and byte offsets below are authored constants, selected before observing model outputs. Each target is the immutable next byte at its offset, or EOS=256 at file length. Input encoding consumes only the causal prefix bytes[0:offset].

TRAIN: range and clamp, 8 offsets each, 16 cases. AUDIT: counted-loop and array-lookup, 8 offsets each, 16 separate cases. AUDIT bytes remain in local frozen fixtures and audit/ artifacts; they are never admitted to context, enqueued, retrieved, or fitted.

Budget: four epochs of all 16 TRAIN offsets, exactly 64 source task updates; at most 1024 generations total. Production Life contacts are the only update authority. No code-head training occurs. If the encounter budget is exhausted, preserve observed results and report deferral.

Conditions: INITIAL before learning; LIFE_TRAINED after the fixed budget; FROZEN after the run with the same initial seed/model and no updates. FROZEN is a no-learning comparator, not an alternative scheduler. All cases contribute to denominators, including failures and EOS. Report mean next-byte negative-log-probability and top-1 accuracy; do not select on AUDIT results.

Code retention: two fixed inference-only anchors, range target action=3 and clamp target action=1, use the complete anchor bytes as input. They are arbitrary predeclared probability-retention probes, not verified code-utility supervision. Track mean loss before/after and exact code readout preservation.

- TRAIN range: SHA256 `fe4ec810acf87c9af8f956005016cc46a9b3612114f218fa7684b0af5e531850`, bytes=67, offsets=0,4,8,16,24,40,53,67.
- TRAIN clamp: SHA256 `9e1679edac61af3ad7940b188b7bf26b8d72264390cb781c3e940845f95629f5`, bytes=97, offsets=0,4,16,31,47,62,79,97.
- AUDIT counted-loop: SHA256 `b45f757021f32a8d51e19f8c866fd6457dbf523022814460f68207f07844a5d7`, bytes=93, offsets=0,4,16,28,43,58,76,93.
- AUDIT array-lookup: SHA256 `4669e7d4a692f6e9442beb049b6158c0521c719cc3d573719019aed7ab2e3336`, bytes=73, offsets=0,4,16,25,36,48,60,73.

Report loss and accuracy even if AUDIT quality deteriorates. Gains on these small authored source families do not establish general coding ability, transfer, or Life optimality. A deterministic alternative scheduler, matched compute comparisons, multiple seeds, longer sequences, and broader independent families remain pending.
