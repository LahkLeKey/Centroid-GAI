# Documentation standard

Documentation is part of the interface and is reviewed with the code it
describes.

## Public API

Every installed header must have a Doxygen `@file` and `@brief` description.
Every public type, enum value, macro group, and function must be documented.
Function documentation must cover:

- purpose and observable behavior;
- every parameter, including ownership and nullability where relevant;
- return semantics and how errors are reported;
- mutation, lifetime, thread-safety, format, or trust constraints when relevant.

Use `@note` for behavior callers need to plan around and `@warning` for hazards.
Describe the contract rather than restating the function name.

## Implementation

Each C source and private header starts with `@file` and `@brief`. Every C
function, including static helpers, inline helpers, entry points, and tests,
has a multiline Doxygen contract immediately above its definition. Include a
plain-language purpose, an explanatory paragraph, every parameter, return
semantics for non-void functions, ownership, and relevant failure behavior.
Declaration comments must agree with the implementation contract.

Assume readers may be new to C. Explain pointer-to-pointer output arguments,
borrowed versus owned storage, element counts versus byte counts, terminators,
flat-array indexing, and short-circuit control flow where those details matter.
Document aggregate fields with their units and lifetimes. Keep the explanation
specific to the function rather than repeating a generic C tutorial everywhere.

Inside every function, use numbered `Step 1`, `Step 2`, and subsequent comments
at meaningful execution phases. Explain validation, allocation, computation,
publication of outputs, and cleanup in their actual order. A small accessor may
need only one step; loops need comments explaining the calculation or invariant,
not a comment for every punctuation mark. Early exits can skip later steps.

Comments describe what the implementation actually guarantees. Do not imply
that training rolls back, file replacement is atomic, native artifacts are
portable, or sentinel checks establish complete bounds unless code enforces it.
Use domain types and checks alongside comments when the compiler can express a
constraint. The [C reading guide](c-reading-guide.md) gives newcomers an entry
point into these conventions and the application's call paths.

Names use the `cgai_` prefix. Public types are named typedefs. Boolean-style
public results use `cgai_status`; identifiers for different domains must not
share a raw integer type inside the implementation.

## Keeping documentation valid

API changes update the public header, architecture notes, examples, and version
in the same change. Build strict documentation locally with:

```sh
cmake -S . -B build-docs -DCGAI_BUILD_DOCS=ON
cmake --build build-docs --target docs
```

Doxygen includes installed headers, core/ABI/CLI sources, private helpers, the
Node bridge, and tests. Generated pages include source, references, and caller
links. Warnings and missing parameter documentation are errors; Graphviz is not
required. The reading guide serves as the generated documentation's main page.

## C11 and native checks

Build every native target as ISO C11. Keep related ownership and algorithms in
focused implementation files and expose only the private declarations needed by
other modules. Document buffer ownership, partial initialization, and cleanup at
these boundaries. Keep exported API signatures and artifact encoding stable when
moving implementations.

Run `cgai_lint` and `cgai_format_check` from the configured CMake build. Lint
covers all owned `.c` files, including tests and the Node addon, and checks private
headers through their callers. Functions allow at most 20 statements, eight
branches, and seven parameters; the line budget allows explanatory comments.
The analyzer's optional Annex K replacement check is disabled because those
`*_s` functions are not available across the supported platforms.

Test assertions evaluate their condition once and terminate the test executable
on failure, including inside fixture helpers. Keep scenario setup, assertions,
and cleanup together in focused tests; never hide production diagnostics with
test-only lint exclusions.
