# Portable inference bundle v1

The normative writer/reader is the source checkout's `src/runtime/bundle.c`;
the installed public contract is `centroid_runtime.h`. This format is
separate from canonical trainer checkpoints and frozen legacy sessions. No
optimizer, world, RNG, queue, receipt or private context bytes are serialized.

Integers use little-endian `uint32_t`/`uint64_t` fields and parameters use
little-endian IEEE binary64. Supported builds must have radix 2, 53-bit double
precision and the binary64 exponent range. No native structure padding enters
the format. Fixed strings contain one NUL followed by zero padding.

| Field, in order | Bytes |
| --- | --- |
| Magic `CRMODEL1` | 8 |
| Bundle version, minimum ABI version | 4 each |
| Complete file length | 8 |
| Groups, shared-enabled, features, CODE actions, TEXT actions, total parameters | 4 each |
| Recipe, encoder, framing, generic action-schema IDs | 96 each |
| Qualification label, reserved zero | 4 each |
| Model, parent, checkpoint, qualification digests | 65 each |
| Task profile, observation schema, action catalog IDs | 96 each |
| Provenance and qualification references | 256 each |
| Shared scales | 32 binary64 values |
| Each live owner: UID, centroid, CODE readouts, TEXT readouts | 8 + (32 + 4×32 + 257×32)×8 |
| Integrity SHA256 over all preceding bytes | 64 lowercase ASCII hex bytes |

The fixed header is 1500 bytes. Owner readouts are action-major and then
feature-major; owners retain their serialized order. Model identity hashes the
canonical dimensions, global semantic IDs, task semantic IDs, owner UIDs and
parameter values. Qualification/provenance references do not change the
numerical model identity, but are covered by complete-bundle integrity.

Version 1 supports the declared legacy recipe, byte-pos32 encoder, role-v1
framing, CODE4/TEXT257 generic schema and 1–4 owners. Task-specific consumers
validate their additional profile/schema/catalog IDs. A qualified label requires
explicit nonempty qualification digest and reference; it is an attestation,
not a quality test performed by the loader. Hosts verify the referenced evidence.

Load validates size, integrity, versions, dimensions, canonical strings, finite
parameters, distinct nonzero UIDs, permitted shared scales, metadata and model
identity before publishing a handle. Rejection preserves an incumbent model.
Successful replacement is synchronized by the host with readers. Unsupported
semantic versions are never silently reinterpreted. Save refuses an existing
asset path; retain named parents and create a new asset for each publication.

Cross-toolchain tests compare inference under declared numerical tolerances and
selected-action agreement. Canonical byte serialization is deterministic for the
same values and metadata. These promises do not relax strict same-build trainer
continuation or imply identical learning across different numerical environments.
