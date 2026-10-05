# Immutable extension evidence

After the final instrumented extension run, the native CLI was invoked again
with the exact same output directory:

```text
build/Release/centroid.exe extensions research/experiments/2026-10-05-gated-extensions-v2
```

It returned exit1, `centroid: invalid argument`. SHA256 lists of every file in
that directory, ordered by filename, were identical before and after the failed
invocation. The exclusive directory guard runs before opening any artifact.
The original revision1 and revision2 evidence directories remain distinct.
