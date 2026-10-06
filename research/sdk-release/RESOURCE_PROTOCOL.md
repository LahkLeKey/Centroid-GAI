# Prospective deployment resource conformance protocol

Registered October 5, 2026, before compiling or running the new
`tests/sdk_resource_check.c` deployment benchmark. This is a prospective
deployment-resource check of a frozen accepted value model, not new AUDIT,
capability evaluation, model selection or permission to tune the learning recipe.

The original [M5 protocol](M5_PROTOCOL.md) froze resource thresholds before its
fresh quality trial, but the first historical timing host was identified only
retrospectively. That limitation remains part of the historical record. Neither
the original protocol nor its independent quality result is rewritten or
relabeled by this registration. All original TRAIN/DEV/AUDIT families, seeds,
targets, quality gates, update order, recipe and tied schedule results remain
unchanged. Later installed-qualifier runs repeat that exact recipe as
conformance, not independent new quality evidence; their whole-run gate remains
120 seconds with a 128 MiB measured native process peak.

The only deployment candidate permitted by this protocol has immutable
value-model SHA256
`eb34dee4bb00ea02d5cef7a8eaa1214869d7fffe15c5356cabbdb96ef7c20e27`,
the registered `lower-bound-strategy/v1` profile,
`lower-bound-band2-byte-pos32/v1` observation schema and
`lower-bound-binary-forward-reverse-gallop4/v1` action catalog. Its qualification
sidecar must pass the public native SHA256 identity check before timing. The
qualified flag and evidence digest identify the supplied record; they do not
replace host review of that record. No model is trained, modified or selected.

The registered physical host is a 12th Gen Intel Core i5-12400F, Intel64 family
6/model 151/stepping 5, with 12 logical processors, running Windows x86-64.
The observed Windows kernel version is 10.0.26200; the installed build revision
is 9457 and the display version is 25H2. The registry's legacy product label is
Windows 10 Home; this record states the observed numeric OS identity without
using that label to infer a different kernel. Windows builds use clang 21.1.0
or MSVC 19.38.33145.0, strict C11 and Release optimization (`-O2` or `/O2`).
The Linux support check uses Ubuntu 24.04 under WSL on this same physical host,
GCC 13.3 and strict C11 Release optimization. The actual process clock source,
compiler, OS/architecture and observable CPU identity are printed for each run.
Results apply to this declared host; virtualization or toolchain changes remain
explicit metadata and never silently stand in for another machine.

The native benchmark accepts only a qualified bundle path and its sidecar path.
It loads and validates the complete model and sidecar outside steady-state
timing. It then prepares eight fixed observations: the exact two bytes `{1,
band}` for each band 0 through 7, passed through the public `cr_encode` to 32
features. These are target-free encoder inputs, not workload arrays, teacher
labels, previous AUDIT outcomes or new supervision. Both CODE and TEXT are
measured using the caller-scratch public inference API.

The registered work is 16 cases in fixed order: eight bands, each CODE followed
by TEXT. Two complete warmup cycles give 32 unmeasured calls; 100 measured cycles
give exactly 1,600 calls, 800 per head. Both owners are eligible, with mass 1 for
each owner and zero for inactive owners. Each head uses its full finite output
catalog (all four CODE actions or 257 TEXT byte/EOS outputs); there is no
teacher-derived legal mask. Every successful result must be finite, nonnegative,
have a positive maximum and sum to one within 1e-12. No correctness or downstream
coding improvement is inferred from probability normalization.

Each call uses a caller-owned workspace of exactly 554 doubles (4,432 bytes)
as declared by `CR_PREDICT_SCRATCH_DOUBLES`. Input, owner mass, workspace and
output buffers have separate storage. Runtime inference is allocation-free;
loading, sidecar reading and initial optional serialization buffers occur before
the timed loop. The benchmark allocates nothing and invokes no callbacks in its
steady-state loop. It records the exact caller buffer sizes separately from
model/bundle bytes and the entire process high-water measurement. It never
subtracts a parent process's memory or presents estimated allocations as a
measured per-process peak.

Each timed interval surrounds only `cr_model_predict_with_scratch`; encoding,
loading, qualification validation, identity checks, result validation and log
printing are outside it and reported separately. There is no framing/retrieval,
tokenizer or host callback in this profile; those costs are marked absent rather
than folded into raw prediction latency. Raw native start/end ticks, their
frequency or nanosecond interpretation, converted elapsed nanoseconds, head,
band and call index are retained for all 1,600 measured calls. The benchmark
prints mean and maximum latency per head and a complete run summary. It checks
model metadata/digest and canonical serialized bytes before and after all calls
to establish immutable deployment conformance.

Acceptance is fixed now: each head's mean prediction latency must be at most
10 ms and the benchmark's measured process peak must be nonzero and at most
128 MiB. Windows uses its own process peak working set through
`K32GetProcessMemoryInfo` and `QueryPerformanceCounter` timestamps. Linux uses
its own process peak RSS from `getrusage` and `CLOCK_MONOTONIC` timestamps.
Failure to read a required clock or native peak counter fails explicitly.
Maximum latency, loading, preprocessing, result-validation, log and whole-run
times remain reported informational costs; neither head's mean may hide failure
of the other head. No threshold or recipe changes follow a failing result.

Every run, including failures, keeps its raw log under an ignored build
directory. Repetitions across declared toolchains establish resource and
deployment conformance only. They do not reset the original AUDIT, establish
Life superiority, change the fixed publication seed or add a general coding
claim. No script runtime, hosted service, process launcher or trainer dependency
is introduced by the standalone public-Runtime benchmark.
