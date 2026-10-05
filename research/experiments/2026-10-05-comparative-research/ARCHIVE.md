# Complete comparative prediction archive

Archived October 5, 2026 for local evidence retention. This changes artifact storage
only; the experiment protocol, source fixtures, measurements and result claims
are unchanged. Every prediction distribution and original byte is retained locally
in `predictions.tsv.gz`, excluded from Git under the
[research artifact policy](../../ARTIFACTS.md).

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| Original `predictions.tsv` | 111,764,177 | `e00b3a049e9e5127ce8f957dd3893c9cc008d58201d486401d67d65ab6f382d8` |
| Compressed `predictions.tsv.gz` | 43,531,878 | `07e69ad4b594dad619abf2838a402f01c1d5ea7fb62712a872f618f443979ec8` |

The archive was fully decompressed and its reconstructed SHA256 verified against
the unchanged original. Both files remain locally available and ignored. Their
identities are committed in the [local artifact manifest](../../local-artifacts.txt).
No external backup or download location has been provisioned, so a Git clone does
not contain either file.

To reconstruct the original from this directory, use the optional external gzip
utility:

```sh
gzip -dc predictions.tsv.gz > predictions.tsv
sha256sum predictions.tsv
```

Any standard gzip reader can reconstruct it. These are archive-inspection tools,
not dependencies of the native library, CLI, training loop or test suite. Native
research commands still create ordinary `predictions.tsv` files in fresh output
directories. Historical output and frozen outcomes remain excluded from TRAIN
retrieval and automatic source scanning.
