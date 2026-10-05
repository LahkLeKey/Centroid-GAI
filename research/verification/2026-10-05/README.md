# Native release verification

Raw artifacts named below are retained locally under the [artifact policy](../../ARTIFACTS.md); their identities are in the [artifact manifest](../../local-artifacts.txt). They are excluded from Git.

All ten CTest tests passed on October 5, 2026, after the final context quarantine
and per-kind retrieval changes. Each complete output was captured by that
platform's native `centroid run`, with observed child exit 0, no timeout and no
omitted bytes. These are infrastructure results, not model-utility labels.

| Build | Native invocation captured | Result | Raw output |
| --- | --- | --- | --- |
| Windows MSVC 19.38 | `ctest --test-dir build -C Release --output-on-failure` | 10/10 passed; 15.07 s | `msvc-tests.log` |
| Windows clang 21.1 | `ctest --test-dir build-clang --output-on-failure` | 10/10 passed; 14.01 s | `clang-tests.log` |
| Linux GCC 13.3, Ubuntu under WSL | `ctest --test-dir build-linux --output-on-failure` | 10/10 passed; 12.70 s | `linux-tests.log` |

The final CLI-only change added record version/current/attribution display. All
three CLI builds were rebuilt afterward. Library behavior and the tested native
contracts were unchanged. Full source snapshots remain in the working model's
versioned context; toolchain-compatible checkpoint continuation is tested by the
native checkpoint executable.

A later scanner correction admits native `src/model/model.c` while continuing
to exclude generated models, checkpoints and weight files. The context regression
was rebuilt and captured again on all three platforms: MSVC 0.26 s, clang 0.26 s,
Linux GCC 1.75 s; all passed with observed child exit 0 and complete output.
The raw files are `runs/final-scan-*-context.log` and copied beside this report.

SHA256 identities of the copied raw outputs:

```text
7f64d363a86ef7f96c4c54699b11abb666a749d8eec86d89bc8c8684d285d9ca  msvc-tests.log
45eea7e5e4b87df50d8b57912b7c86b1bba4b33abe59d34b2ed464532a01d4bd  clang-tests.log
8d795071c42c4ec1fcedd3498d3c8c94085b0f4f728b4371d8a9271b74508525  linux-tests.log
```

`clang-format --dry-run --Werror` passed on authored C files and headers. Clang
static analysis passed for the native library, with the last context, experiment,
checkpoint and process changes checked again. These development checks were
observed through tool execution; the ledger's manual activity note identifies
capture coverage rather than claiming automatic access to every editor action.
