# Research artifact retention

Updated October 5, 2026. Source control retains the native implementation and the
documents needed to review its contracts and measured conclusions. Historical
research run outputs remain local and are excluded by the root `.gitignore`.
Local retention and inclusion in Git are separate guarantees.

| Versioned in Git | Retained locally outside Git |
| --- | --- |
| Native source, public/internal headers and tests | Historical copied fixtures, candidate/evaluator sources and auxiliary verification drivers |
| Required build/test fixtures in the five `data/audit` headers | Prediction distributions, feature/target matrices, metrics, costs, sequences and traces |
| Research Markdown protocols, reports, references and the archived design | Measurement receipts, argv/status records and raw verification logs |
| `local-artifacts.txt` path, size and hash manifest | Frozen causal inputs, physical-contact proof files and runtime context/model/session snapshots |
| Optional small `data/train` request/context examples | Build executables, objects, local training runs and temporary files |

Only Markdown documents and the artifact identity manifest are versioned under
`research/`. These rules do not exclude runtime source, native tests or required
`data/audit` headers. The research tree remains quarantined from automatic TRAIN
ingestion regardless of whether a file is versioned. Documentation refers to
excluded raw artifacts by local filenames rather than links that would break in a
fresh clone.

[local-artifacts.txt](local-artifacts.txt) records the repository-relative path,
complete byte count and SHA256 of each excluded research run artifact present at
this commit review, including original and compressed comparative predictions,
small logs and frozen inputs. All original files remain on disk; nothing was
deleted. The manifest is a snapshot, not an automatic inventory of future runs.
Hashes verify identities but do not replace the full bytes or prove a reported
quality result.

A fresh clone includes code, native tests, required fixtures and the research
documents, but not historical run outputs or resumable model/session states.
Preserve those bytes in a separate backup before cleaning local research
directories. No remote backup or artifact download location has been configured.
The existing native evaluators can produce new runs in fresh directories; reruns
do not reproduce historical timing or guarantee cross-build floating-point identity.

The October 5 fresh-install check copied only `CMakeLists.txt`, `src`, `include`,
`cli`, `tests` and the five `data/audit` headers into an isolated 55-file source
tree. Windows clang 21.1 configured and built it, and all fifteen native tests
passed in 40.02 seconds. The copied tree contained neither `research` nor
`data/train`. This validates build/test independence from the historical archive;
it does not reproduce the historical quality experiments or trained checkpoint.

New experiments should version their protocol and required authored build/test
fixtures before evaluation, retain every attempted outcome locally, and commit
their report and artifact identities. Generated run outputs should follow these
ignore rules rather than being added to Git by default.
