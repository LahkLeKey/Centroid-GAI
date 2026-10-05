# Exact frozen representation syntax check

Compiler executable: `/usr/bin/gcc`. Compiler version output SHA256: `8497f84b2b8334c413d9c6166bd39d009ba55d2ff60a8f23e66dfa5819c2b048`.

Passed: 6/6. Each command uses strict C11, pedantic errors and warnings as errors, with syntax-only evaluation. `results.tsv` records exact input bytes/SHA before and after, actual status, exit/deadline, full raw output identity and time. Every direct argv and full raw log is retained. There is no model/context owner and no TRAIN admission.

The separately tested token codec reconstructs exact source bytes. Compiling these exact originals therefore establishes syntax validity of exact reconstructed originals. This does not establish syntax validity of predicted/generated sequences, runtime contract correctness or general coding competence.
