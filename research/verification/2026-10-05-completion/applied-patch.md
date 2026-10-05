# Verified local publication

Raw artifacts named below are retained locally under the [artifact policy](../../ARTIFACTS.md); their identities are in the [artifact manifest](../../local-artifacts.txt). They are excluded from Git.

The native `centroid apply` operation published the retained three-domain suite's
winner to `src/domain/algorithms.c`. Only the lower-bound implementation changes:
it uses overflow-safe midpoint binary search while preserving the declared
sorted-array, invalid-input and comparison-counter contracts. Checked affine
sizes and saturating unsigned addition retain their existing behavior.

Publication command:

```text
build/Release/centroid.exe apply research/experiments/2026-10-05-project-code-suite . a25f20aead557c992d24feadcaeade99975c26bfd97e084686b0fe01fc30c0dc
```

The operation returned exit0 and the immediately published source SHA256 matched
the accepted winner exactly. Subsequent `clang-format` changed whitespace only.

| Identity | SHA256 |
| --- | --- |
| Exact pre-publication parent | `a25f20aead557c992d24feadcaeade99975c26bfd97e084686b0fe01fc30c0dc` |
| Exact accepted/published winner | `6c25525a86b0501149e0e4a47b4c525ac504861b2d40440b0e8d2751350133fe` |
| Formatted working source | `e5a3b7ef0f9c72712485791e518d26d397b6b3c2823b1f07b3637d8902f6f66f` |
| Live `include/centroid.h` | `441b2d615fd7975e1b1bc1e4372f46582ab8a7e046a01e1c461d4c799a0dfee9` |
| Live `include/centroid_algorithms.h` | `50c6d1beebb53ad8ea05806557ad1439227db053e7ba4706c9d983009e75a93b` |

Both live headers matched the frozen manifest dependencies at publication. The
strengthened final verifier checks those identities immediately before atomic
replacement; changed-header tests preserve the incumbent source.

Parent/rollback source (`../../experiments/2026-10-05-project-code-suite/parent.c`; local artifact),
winner (`../../experiments/2026-10-05-project-code-suite/winner.c`; local artifact),
manifest (`../../experiments/2026-10-05-project-code-suite/patch.manifest`; local artifact) and
[all candidate results](../../experiments/2026-10-05-project-code-suite/report.md)
remain retained. The measured comparison-cost change is49→27 across eight
TRAIN cases, with exact correctness gates on TRAIN, DEV and AUDIT. It is not a
measured wall-clock speedup or evidence that the learned chooser transfers.
Future suite trials require the declared linear baseline in a fresh sandbox;
the optimized working source intentionally no longer satisfies that precondition.
