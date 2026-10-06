# First local code-assistance trial

Use installed `centroid_code_helper` to inspect current C source and prepare an
evidence-backed request for your LLM. Retrieval runs locally; the host owns its
LLM connection. See the [SDK contract](SDK.md) for installation and bounds.

## Inspect the loader

In PowerShell, set paths to your installed Windows helper and Centroid checkout:

```powershell
$CentroidHelper = 'C:\YOUR_SDK\bin\centroid_code_helper.exe'
$CentroidSourceRoot = 'C:\YOUR_CENTROID_CHECKOUT'
& $CentroidHelper $CentroidSourceRoot 'cr_model_load_bytes'
& $CentroidHelper $CentroidSourceRoot 'cr_model_load_file'
```

The first query finds the interface and byte-loader implementation. The second
supplies neighboring context around the file loader, including the preceding
validated-model replacement step in this checkout. Excerpts can end inside
functions. Check the complete implementation before a code or deployment decision.

Each invocation scans current working bytes into a fresh context. Evidence gives
path, ID, version, attribution, score and exact byte offset/length. SHA256 identifies
the complete original record, including bytes outside the excerpt. IDs and versions
belong to that context; retain source snapshots and digests across requests.

Defaults allow four 2,048-byte excerpts and 8,192 total raw excerpt bytes.
Metadata and formatting add bytes. Reports distinguish matches, returns and
omissions. Ranking uses fixed lexical/centroid similarity; this trial establishes
no learned-ranking or downstream coding benefit. Exercise refresh and abstention:

```powershell
& $CentroidHelper --context-lifecycle
$excludedQuery = 'nano' + 'seconds'
& $CentroidHelper $CentroidSourceRoot $excludedQuery
```

Lifecycle mode demonstrates unsaved-source admission, refresh, preserved history
and independent contexts at the same path. The constructed query identifies a
helper in this checkout's excluded benchmark tests and should abstain: the admitted
context supplies no supported evidence. After corpus edits, choose an identifier
that still appears only in excluded tests for this control.

## Prepare a host-owned LLM request

Keep the complete user question separate from the lookup identifiers. Retain
the exact formatted evidence and its metadata; preserve explicit byte lengths
if a source contains embedded NULs. A request template is:

```text
Explain how this implementation preserves the previously loaded model when
a candidate bundle fails validation. Identify the successful replacement step.

Source evidence:
[Insert formatted evidence for both queries, including paths, versions,
byte spans, attribution and complete-record SHA256s.]

Use the supplied excerpts as evidence and cite their paths and byte spans.
State which conclusions need additional source because an excerpt is incomplete.
```

Your host chooses the LLM, submits the request and retains its actual response.
Count the entire submitted prompt with that provider/model's tokenizer,
including instructions, question, metadata, excerpts, history and reserved
response space. The helper's byte budget does not determine an LLM token budget.

Admit LLM responses as attributed proposals. Proposals and activity remain inputs;
independent tests or measurements supply verified teachers. Updates belong to
separate Life training and require physical contact. Review complete source and
verify outcomes before accepting proposed code or supervision.

## Try the qualified finite-action example

With a qualified lower-bound bundle and adjacent `qualification.md`, optionally run:

```powershell
$CentroidBundle = 'C:\YOUR_MODEL_PACKAGE\reference-v1\qualified.crmodel'
& $CentroidHelper $CentroidSourceRoot 'c_lower_bound' $CentroidBundle
```

The helper checks sidecar identity and demonstrates native search-strategy
selection alongside retrieval. Review the record and publisher. Its learned
feature selects among four verified lower-bound strategies. Broader code, chat
and gameplay capabilities require their own registered evaluations.
