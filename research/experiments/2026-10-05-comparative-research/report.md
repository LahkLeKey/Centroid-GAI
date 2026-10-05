# Matched native research results

The protocol and fixtures were frozen before predictions. `predictions.tsv` retains complete distributions for every selected prefix, contiguous causal tail, autoregressive rollout step and retention probe. `inputs.tsv` retains every TRAIN target and exact feature vector; `sequences.tsv` records all attempted17-symbol rollouts. `metrics.tsv` contains every seed, denominator, failure, update/owner budget, coverage and cost. No DEV/AUDIT record was admitted.

The complete prediction bytes are retained locally in `predictions.tsv` and the
lossless `predictions.tsv.gz` archive. Both are excluded from source control under
the [research artifact policy](../../ARTIFACTS.md). The [archive receipt](ARCHIVE.md)
records both byte counts and SHA256 identities, reconstruction verification and
the optional external decompression tool. Compression changes no prediction,
denominator or reported result; these local files are not part of a Git clone.

| Condition | Updates per seed | Mean TRAIN loss before → after | Mean DEV loss before → after | Mean AUDIT loss before → after | Mean AUDIT accuracy after | Retention loss delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| LIFE | 96 | 5.5510612 → 3.2914811 | 5.5624865 → 4.3239296 | 5.5462235 → 5.7900222 | 0.0229885 | -0.0024670229 |
| FROZEN_LIFE | 96 | 5.5510612 → 3.2914811 | 5.5624865 → 4.3239296 | 5.5462235 → 5.7900222 | 0.0229885 | -0.0024670229 |
| ROUND_ROBIN | 96 | 5.5510612 → 3.2914811 | 5.5624865 → 4.3239296 | 5.5462235 → 5.7900222 | 0.0229885 | -0.0024670229 |
| SINGLE_EXPERT | 96 | 5.5500622 → 3.3026265 | 5.5668311 → 4.3281817 | 5.5469814 → 5.7916898 | 0.0172414 | 0 |
| FOUR_SPECIALISTS | 96 | 5.5552244 → 3.2766501 | 5.5584445 → 4.3407762 | 5.5478553 → 5.7142626 | 0.0344828 | 0.0024275762 |
| PAIR_SUBWORDS | 96 | 5.5617247 → 3.2644171 | 5.5551908 → 4.3615058 | 5.5525817 → 4.616212 | 0.126437 | 0.0068354185 |
| SOURCE_ONLY | 96 | 5.5541339 → 3.5190774 | 5.5510249 → 4.2033738 | 5.5546327 → 5.2006749 | 0.137931 | 0.0037255505 |
| SOURCE_LLM | 96 | 5.550536 → 3.5008104 | 5.5469611 → 4.2936362 | 5.543038 → 5.1267649 | 0.137931 | 0.0037457557 |
| SOURCE_ACTIVITY | 96 | 5.5556199 → 3.5376549 | 5.5540548 → 4.2505455 | 5.5575244 → 5.3048633 | 0.137931 | 0.0037464759 |
| SOURCE_LLM_ACTIVITY | 96 | 5.5550917 → 3.5485013 | 5.5474386 → 4.3038842 | 5.5504527 → 5.4997453 | 0.137931 | 0.013887313 |
| FROZEN_NO_LEARNING | 0 | 5.5510612 → 5.5510612 | 5.5624865 → 5.5624865 | 5.5462235 → 5.5462235 | 0 | 0 |

Preregistered gates (an improvement is positive comparator loss minus candidate loss; no seed may regress by more than0.1):

- LIFE versus FROZEN_NO_LEARNING: mean AUDIT loss gain=-0.243798657, worst seed gain=-0.253366786, retention delta=-0.00246702287, margin=0, gate=FAIL_OR_INCONCLUSIVE.
- FROZEN_LIFE versus LIFE: mean AUDIT loss gain=0, worst seed gain=0, retention delta=-0.00246702287, margin=0.02, gate=FAIL_OR_INCONCLUSIVE.
- ROUND_ROBIN versus LIFE: mean AUDIT loss gain=0, worst seed gain=0, retention delta=-0.00246702287, margin=0.02, gate=FAIL_OR_INCONCLUSIVE.
- SINGLE_EXPERT versus LIFE: mean AUDIT loss gain=-0.00166759114, worst seed gain=-0.0171609605, retention delta=0, margin=0.02, gate=FAIL_OR_INCONCLUSIVE.
- FOUR_SPECIALISTS versus LIFE: mean AUDIT loss gain=0.0757595688, worst seed gain=-0.00557032298, retention delta=0.00242757619, margin=0.02, gate=PASS.
- PAIR_SUBWORDS versus LIFE: mean AUDIT loss gain=1.17381014, worst seed gain=1.14532697, retention delta=0.00683541848, margin=0.02, gate=PASS.
- SOURCE_LLM versus SOURCE_ONLY: mean AUDIT loss gain=0.0739100569, worst seed gain=0.0267032149, retention delta=0.0037457557, margin=0.02, gate=PASS.
- SOURCE_ACTIVITY versus SOURCE_ONLY: mean AUDIT loss gain=-0.104188308, worst seed gain=-0.180508532, retention delta=0.00374647587, margin=0.02, gate=FAIL_OR_INCONCLUSIVE.
- SOURCE_LLM_ACTIVITY versus SOURCE_ONLY: mean AUDIT loss gain=-0.299070367, worst seed gain=-0.450368198, retention delta=0.0138873127, margin=0.02, gate=FAIL_OR_INCONCLUSIVE.
- Inference UNIFORM_ROUTING versus trained LIFE: mean AUDIT loss gain=0.137307543; no separate fitting budget or production promotion.
- Inference PREFIX_ELIGIBILITY versus trained FOUR_SPECIALISTS: mean AUDIT loss gain=0.0241186948; no separate fitting budget or production promotion.

Interpretation limits: multiple seeds assess this fixed authored experiment, not broad transfer. Task-update budgets match among learned conditions, but single/four owners and token/context inputs have different parameter, scalar-update, encoding and inference costs. Millisecond measurements may quantize schedule-only time to zero. Peak memory is the cumulative native process high-water mark and includes report buffers and previous conditions. No held-out result selects a recipe, authorizes a merge or proves general coding ability. Exact ties or failed margins resolve a research question as inconclusive; Life optimality and causal LLM utility must not be inferred from infrastructure passes. Research owners are deliberately not serializable as production checkpoints; retained fixture, recipe, seeds and exact TRAIN features allow independent deterministic reruns. Production continuation is tested separately.
