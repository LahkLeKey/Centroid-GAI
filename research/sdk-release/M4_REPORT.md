# M4 deterministic context-helper measurements

The native runtime-context fixture passed on October 5, 2026 with reproducible
retrieval, abstention, byte-budget, refresh and ownership checks. These results
measure the deterministic `evidence-lexical-cosine/v1` profile: lexical overlap
gates eligibility and the fixed native encoder supplies cosine ranking. No model
parameters are learned by this interface. The separately qualified finite
strategy feature is described in [M5_REPORT.md](M5_REPORT.md).

`tests/test_runtime_context.c` constructs 64 exact native source records. Each
has one unique function identifier and a shared identifier used by omission
controls. Eight queries specify unique functions; six negatives cover an absent
function, empty input, a numeric-only query, punctuation-only input and symbols
present only in quarantined DEV/AUDIT control records. The test never admits
those control records as TRAIN or uses them as training targets. Two additional
queries match all 64 sources and exercise bounded output. Expectations and
measurement code reside in the excluded test tree.

| Contract measurement | Result |
| --- | ---: |
| Unique positive query returns the expected source path | 8 / 8 |
| Negative query abstains with no evidence | 6 / 6 |
| Evidence byte budget and explicit omissions | 2 / 2 |
| Old symbol abstains after refresh; new symbol returns version 2 | 2 / 2 |
| Private source is absent from the other context and retained in its own | 2 / 2 |

These are exact lexical fixture checks, not an estimate of relevance on arbitrary
code queries or an ambiguous-ranking benchmark. The returned records are current
TRAIN sources with attribution, version and a complete-byte SHA256 identity.
All returned spans stay within the preserved original records. With 12-byte
excerpts and a 19-byte total budget, the shared query returns two excerpts,
reports 64 matches and explicitly omits 62 records. With a maximum of two hits,
32-byte excerpts and an 8-byte total budget, it returns one excerpt and omits 63.
Formatting has its own caller-buffer capacity check; these byte budgets do not
claim to count an external LLM's tokens or its complete prompt overhead.

Each of the 16 fixed queries was executed 100 times after two complete warmup
cycles. Every repetition also checked the same correctness and budget contracts.
Timing surrounds the allocation-free query call; assertions and fixture
construction are outside the interval. Refresh then replaces the first source,
preserves its previous complete bytes, excludes its old identifier and returns
the new identifier at version 2. A second context holds a different source at
the same path and remains independent after the first context's refresh.

The targeted MSVC Release run measured:

| Resource measurement | Observed value |
| --- | ---: |
| Timed query calls | 1,600 |
| Warmup query calls | 32 |
| Mean latency | 7,030.250 ns |
| Median latency | 7,100 ns |
| Retained complete raw source/control bytes, including history | 4,643 bytes |
| Retained path/attribution strings, including terminating NULs | 2,568 bytes |
| Principal caller-owned fixture/query/result/timing buffers | 29,224 bytes |
| Process peak working set | 21,434,368 bytes |

The measured contexts contained 67 and 1 retained records after refresh. The
first context's configured capacity was 128 records, 1,024 bytes per record and
128 KiB total raw bytes. The memory totals distinguish admitted raw bytes,
provenance strings and explicitly sized caller buffers from the process high
water mark. SDK record bookkeeping, allocation metadata, stack and loaded
modules are included in the process measurement; the report does not pretend
that raw-byte accounting equals total heap consumption. Windows uses the native
peak working-set counter; Linux repetitions report native process peak RSS.

This result was measured on Windows x86-64 using MSVC 19.38.33145.0, strict C11,
the Release configuration and the runtime-only test dependency. The host's
reported CPU identity was Intel64 Family 6 Model 151 Stepping 5, GenuineIntel,
with 12 logical processors. The native performance counter supplies monotonic
latency timestamps. These are informational measurements for this small corpus
and machine rather than a universal latency guarantee. No LLM/provider or
training library participates in the test, and no downstream coding improvement
is inferred.

Reproduction from a configured native build is:

```text
cmake --build build-sdk-full-msvc --config Release --target test_runtime_context
ctest --test-dir build-sdk-full-msvc -C Release -R "^runtime_context$" -V
```

The native test prints a single metrics record with denominators, repetitions,
mean, median and memory accounting. The initial output is retained in
`build-sdk-full-msvc/Testing/Temporary/LastTest.log` until the next CTest run;
the exact measured values above preserve the first targeted result. The existing
filesystem fixture also verified deterministic scanning, dirty-source version
refresh, reserved-tree exclusion and complete cleanup. Installed-host and
cross-toolchain consumer checks are separate product release evidence.

The installed example now also exposes `centroid_code_helper --context-lifecycle`.
This native, provider-free mode demonstrates unsaved in-memory source refresh,
preserved version-1 bytes, version-2 retrieval and a second private context at
the same path. Its CTest coverage runs both the build-tree executable and the
installed Examples component. These checks extend host integration coverage;
they do not replace or relabel the initial fixture measurements above.

The exact initial native measurement record was:

```text
M4 measurement profile=evidence-lexical-cosine/v1 records=67 independent_records=1 positive=8/8 negative=6/6 bounded=2/2 refresh=2/2 independence=2/2 calls=1600 warmup_calls=32 mean_ns=7030.250 median_ns=7100 raw_bytes=4643 provenance_bytes=2568 caller_buffers=29224 peak_process_bytes=21434368
```
