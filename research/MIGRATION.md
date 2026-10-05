# Required runtime migration inventory

The fresh repository began with only the
[fresh-start blueprint, now archived](archive/FRESH_START_BLUEPRINT.md). There are no
required executable non-C components to port. No historical behavior parity is
claimed. CMake, Git and the native compiler are declared external tools.

Foreign-language source files may be ingested as attributed bytes. That is not
an implementation migration. Add any future required component here with its
observable input/output/error/persistence contract, licensing, reference fixtures,
native replacement and parity evidence before retiring the reference runtime.
