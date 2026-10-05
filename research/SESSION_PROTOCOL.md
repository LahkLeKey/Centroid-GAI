# Bounded frozen conversation protocol

Registered October 5, 2026, before opening the session evaluation results.

The new local session is an evidence interface, with experimental byte generation
available separately. Supported responses quote an exact current TRAIN SOURCE
span and carry record ID, version, full source SHA256, path, attribution and byte
span. Lexical matches are evidence availability, not semantic comprehension or
confidence. Missing source evidence produces an explicit abstention. Proposals,
activity, development and audit records never substitute for supported source.

Acceptance: every independently authored positive identifier query returns its
expected current source/version with exact span bytes and provenance; every
negative query abstains. Test families cover a ring buffer, alignment and decimal
parsing, with queries frozen before result inspection. Binary source and request
bytes, changed versions and frozen-snapshot retention are included. Results are
reported with complete per-query attempts and denominators. This gate establishes
bounded evidence behavior, not a learned conversation or context-benefit claim.

Ownership and continuation gates: inference leaves original model fingerprints,
optimizer clocks, Life state and trainer checkpoint bytes unchanged; a session
survives original-owner destruction. Full requests fail explicitly above 64 KiB;
at most 16 turns and 256 KiB complete user/assistant history are retained. Exact
source excerpts are at most 4096 bytes and greedy generation at most 1024 bytes.
EOS is a distinct model token. Unknown bytes, NUL and literal role names remain
bytes within explicit user/assistant roles. No implicit request truncation,
training, tools, sampling RNG or hosted service is permitted.

Session files use canonical widths, little endian, whole-envelope integrity,
model and formatter recipe/build identities, complete frozen context and history.
Loading replays every deterministic answer against the frozen model/context and
prior role-separated turns, rejects invalid lengths, nonfinite values, modified
claims, corruption, truncation and trailing bytes, and preserves the incumbent.
Same-build save/restart predictions and history must be exact. Generation budgets
bound calls; output is labeled unverified, with explicit EOS versus byte limit.

Sequence quality is not promoted by this feature. The existing independent
next-byte measurements do not establish conversational fluency. Wider language
generation requires a separate sequence-quality and retention protocol.
