# Native source-quality measurement

The protocol and immutable fixtures were published before AUDIT prediction. Raw `predictions.tsv` retains every target, top-1 prediction, loss and complete action distribution for all 102 inference attempts. `metrics.tsv` retains exact denominators and costs. No held-out bytes entered the two-record training context.

| Set | Cases per condition | Initial loss | Life-trained loss | Frozen loss | Initial accuracy | Life-trained accuracy | Frozen accuracy |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| TRAIN range + clamp | 16 | 5.5494339 | 2.02345498 | 5.5494339 | 0 | 0.75 | 0 |
| AUDIT counted-loop + array-lookup | 16 | 5.54813371 | 4.32597114 | 5.54813371 | 0 | 0.25 | 0 |
| CODE retention probes | 2 | 1.36635665 | 1.35321885 | 1.36635665 | 0.5 | 0.5 | 0.5 |

Completed 64 / 64 planned source updates through 64 contact generations, with 64 total generations, 0 deferrals and 8 reseeds. Both owned group clocks: 64, 64. Changed TRAIN probability distributions: 16 / 16. Code readout values and optimizer moments stayed exact: yes; shared routing changes can still move code probabilities.

Source loss gain: 3.52597892. AUDIT loss gain: 1.22216257 (negative means deterioration). Code retention loss delta: -0.013137793 (positive means deterioration). These results are retained regardless of sign.

Measured training/scheduler wall time: 31 ms; complete measured run before report publication: 79 ms. Trained checkpoint: 444263 bytes. Fixed allocated model bound: 804944 bytes per model, 1609888 bytes for trained plus frozen comparator. Model bytes are sizeof-based allocation bounds, not process RSS or peak-memory measurements.

This is a fixed-seed, tiny, causal next-byte benchmark on authored C fixtures. TRAIN gains establish scoped learning on selected prefixes; separate AUDIT families measure only these prefixes and may improve or deteriorate. CODE anchors measure probability retention rather than independently verified code utility. The frozen comparator has identical initial parameters and zero updates; it is not compute-matched and does not isolate scheduler value. Life optimality, a deterministic alternative scheduler, matched compute comparisons, multiple seeds, broader independent families, and longer-sequence quality remain pending.
