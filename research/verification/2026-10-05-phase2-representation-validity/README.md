# Frozen representation source validity

Raw artifacts named below are retained locally under the [artifact policy](../../ARTIFACTS.md); their identities are in the [artifact manifest](../../local-artifacts.txt). They are excluded from Git.

October 5, 2026. All six frozen comparative-research source fixtures pass
`-std=c11 -pedantic-errors -Wall -Wextra -Werror -fsyntax-only` with both Windows
clang 21.1.0 and Linux GCC 13.3.0: **12/12 checks pass**. No source bytes changed
during compilation; both platforms recorded identical fixture SHA256 identities.
Every successful compiler invocation produced zero diagnostic bytes.

The fixtures are the actual retained TRAIN binary-search/byte-reversal, DEV
byte-count/insertion-sort and AUDIT ring-buffer/unsigned-gcd files from
[the frozen comparison](../../experiments/2026-10-05-comparative-research/report.md).
The native verifier (`verify.c`; local artifact) owns no context, trainer or model and admits no
records. AUDIT bytes and results remain quarantine evaluation artifacts.

Windows results (`windows-clang/results.tsv`; local artifact) and Linux results (`linux-gcc/results.tsv`; local artifact)
retain input byte counts, hashes before and after, status, exit code, deadline,
raw-output byte count/SHA and elapsed time. Each platform directory retains full
`compiler-version.log`, `fixture-N.log` and the exact direct argv in adjacent
`.argv.txt` files. The empty fixture logs are complete observed raw output.

The native token roundtrip test in `tests/test_research.c:test_tokenizer` verifies
exact reconstruction of all six byte sequences from the frozen pair dictionary
and token wires. Given that byte equality, syntax-checking these exact originals
also establishes syntax validity for their exact reconstructions. This is an
inference from exact equality, not a test of model-generated source validity or
runtime behavior. Predicted sequence correctness and broad coding competence
remain separate measurements.

Tool identities:

| Artifact | SHA256 |
| --- | --- |
| Windows clang executable | `07e62068e7cbce378c053cfd0633b1a235fa8b38ebd57b27998dda17fe375a8c` |
| Linux GCC 13 executable | `1b99826121ae6682a634e5efe09bd3e3df58ce58e0b28f849114ab5b89139c26` |
| Native verifier source | `d8e24326e3bcc4d604afb3a38496c2aa71205ba0a6969eb43a8050a6a6ec8036` |
| Windows verifier executable | `65e3177ff8cd1974897ee9c120993dec0409882cc39c7a162b515852fefd76df` |
| Linux verifier executable | `a363a3fe2bd6270e0a5b6d8b5aba77a70f631d1eeb568af3b07ff7f0474b72e0` |
| Both empty build logs | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| Windows verifier console output | `eb5d4a6d143e0d70b5ccabd00dc5952a78e2b4df119778e9abbd5d0d6c1c488e` |
| Linux verifier console output | `5a500b3014aee55b4ce734e83327afc410141df3e71829321bad7ef279bf3b5f` |

The verifier was compiled directly from `verify.c`, `src/core/core.c`,
`src/platform/filesystem.c` and `src/platform/process.c`; shared build directories
were not changed. Build argv (`build-argv.txt`; local artifact) records both exact compiler argument
vectors. `build-windows.log` and `build-linux.log` copy the complete empty output
observed by the execution tool; both builds exited 0. Generated verifier binaries
are ignored by Git and can be rebuilt locally. The Linux output was renamed from
`verify-linux` to `verify-linux.exe` without changing its bytes so the ordinary
binary exclusion applies; it remains a Linux ELF executable.
`verifier-windows.log` and `verifier-linux.log` copy the complete observed
verifier console output, preserving each platform's line endings; both exited 0.
