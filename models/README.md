# Reference model assets

Model bytes are separate local/release assets, rather than source-checkout
inputs. `reference-manifest.json` records exact profile IDs, model identity,
file sizes and SHA256 checksums for the first accepted native search feature.
The source checkout includes its native qualification recipe and tests.

Local prepared assets are under `reference-v1/`: `qualified.crmodel`, its adjacent
`qualification.md`, the raw-evidence identity manifest, frozen protocol/report
and `1409-parent.crmodel`. The helper checks the adjacent qualification record's
SHA256 before task-specific use. The host also reviews its evidence and trusts
the publisher; a checksum alone does not establish quality.

The parent is an explicitly experimental zero-update model. It is preserved as
a numerical rollback asset. On rollback, `cr_code_recommend` returns
`CR_DEFERRED`; the host uses its conventional binary-search fallback. A previous
qualified asset may instead remain deployed for a host's normal rollback policy.
Replacement and destruction occur only when the host has synchronized readers.

The reference model supplies bounded lower-bound strategy selection. Code
evidence retrieval uses fixed encoding and a deterministic baseline. Neither
asset establishes fluent conversation, gameplay policy or general coding gain.
The curated evidence is in `research/sdk-release/M5_REPORT.md`; complete attempt
bytes remain retained locally under the research artifact policy.

The owner authorized MIT for SDK code and original model assets on October 6,
2026. The source-root `LICENSE` contains the notice; reference-model packages
include it at `reference-v1/LICENSE`. Include the copyright and permission notice
with redistributed model packages. Model digests and immutable qualification
records remain unchanged. Generating a compatible candidate locally uses the
optional installed tool:

```text
centroid-sdk-qualify PROJECT_ROOT NEW_OUTPUT_DIRECTORY
```

Supply a source checkout containing the registered protocol and actual
`src/domain/algorithms.c`. The tool retains every outcome and refuses existing
output directories. Reexecuting the frozen recipe tests reproducibility; it
does not authorize tuning on opened AUDIT data or a new independent quality claim.
