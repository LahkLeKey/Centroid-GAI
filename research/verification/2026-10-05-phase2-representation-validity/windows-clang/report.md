# Exact frozen representation syntax check

Compiler executable: `C:/Program Files/LLVM/bin/clang.exe`. Compiler version output SHA256: `0fbb23c0529e4ac5be23c4ded4ac8e8ee87c5f4e5d959c540b918cd734f37f8f`.

Passed: 6/6. Each command uses strict C11, pedantic errors and warnings as errors, with syntax-only evaluation. `results.tsv` records exact input bytes/SHA before and after, actual status, exit/deadline, full raw output identity and time. Every direct argv and full raw log is retained. There is no model/context owner and no TRAIN admission.

The separately tested token codec reconstructs exact source bytes. Compiling these exact originals therefore establishes syntax validity of exact reconstructed originals. This does not establish syntax validity of predicted/generated sequences, runtime contract correctness or general coding competence.
