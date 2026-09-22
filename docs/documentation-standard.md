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

Each C source and private header starts with `@file` and `@brief`. Document
private functions when their invariants, ownership rules, algorithms, or units
are not evident from their signature. Prefer domain types over comments when the
compiler can express a constraint.

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

Doxygen warnings are errors, so undocumented additions fail the documentation
build.

