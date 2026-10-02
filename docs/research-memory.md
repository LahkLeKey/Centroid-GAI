# Research and memory

Status: the initial source-result implementation is connected to the
[chat service](chat-service.md): local retrieval, scoped persistent memory,
explicit support routing, Wikipedia/SearXNG providers and a bounded public fetcher.
This document retains the broader design and release gates; not every policy
below is implemented. Current restrictions include one configured owner, IPv4-only
fetching, exact-query/applicability cache reuse and heuristic source TTLs. Unsupported
messages automatically research their current text; `autoSearch: false` opts out,
and `publicQuery` supplies a separate public query. No trained model is needed.
Generated factual synthesis, automatic claim
verification, fact-specific TTL calibration and reviewed training-candidate queues
remain future work. Internet research runs in the backend without a browser.

The reimplemented chat path now admits current evidence to protocol-two models
and permits only complete, source-matched quotations, falling back to excerpts on
unknown words, omitted evidence or unsupported output. New source records are
`sourced` rather than semantically verified. Versioned cache policies use five
minutes for volatile queries and one day otherwise, measured from fetch time.
Offline training requires independent development checks before promotion;
per-job reports preserve rejected candidates. Free-form semantic verification,
human-reviewed corpora and calibrated domain-specific freshness remain open.

## Answer support comes first

The chatbot should consult authorized conversation context, user memory and local
source passages before deciding whether public research is useful. It should
preserve uncertainty when support is missing and retain useful sourced knowledge
for later conversations.

```text
question + authorized history
           |
retrieve local sources and eligible memory
           |
check relevance, freshness, applicability and contradictions
           |
           +-> sufficient support -> answer with source references
           |
           +-> missing public evidence -> bounded search and fetch
           |                                  |
           |                        verify passages -> cited source result
           |
           +-> ambiguous or unavailable evidence -> clarify or disclose limits
                                              |
                            eligible claims -> memory admission -> later retrieval
```

Call this an answer-support decision. Token probability, output entropy, loss,
nearest-centroid distance and retrieval similarity do not measure factual
correctness. Do not expose an invented confidence percentage or use an arbitrary
`confidence < 0.7` threshold as if it were calibrated.

Start with explicit rules and a recorded reason: refresh volatile or expired
facts, search when relevant public evidence is absent, investigate conflicting
sources, and clarify ambiguity. An explicit request to research should use the
configured provider when eligible. If access is disabled, exhausted or fails,
report that state without implying a successful search.

Later, evaluate a router on held-out questions labeled for correctness, evidence
support and usefulness of research. Choose thresholds from measured results and
recalibrate when the model, tokenizer or domain changes.

## Bounded public research

Add a provider adapter returning URLs, titles, snippets and available dates.
Provider selection, credentials and spending limits are deployment configuration;
credentials stay on the server. A starting budget is two queries and five fetched
pages per turn, with byte limits, a total deadline, cancellation and daily quotas.
Tune these limits using measured latency, cost and answer quality.

The implemented default is Wikipedia, without an API key; a configured SearXNG
endpoint extends search beyond the encyclopedia. Set `CGAI_SEARCH_PROVIDER=disabled`
to prohibit network research. Successful excerpts are remembered only when
`rememberSources: true` and memory is enabled.

Send a short public query containing only the information needed for the search.
Do not send whole transcripts, private repository contents, private memory,
credentials or internal identifiers. If essential private information cannot be
removed, obtain permission for that specific disclosure before sending it.

The fetcher accepts public HTTP(S) destinations, validates resolved addresses and
every redirect, rejects private/local/link-local targets, and bounds response
size, decompression and parsing time. It sends no application cookies or secrets.
It extracts readable text without executing downloaded scripts. Access-restricted
or unavailable pages remain unavailable evidence.

Treat all downloaded text and stored excerpts as untrusted data. A passage cannot
change tool permissions, issue tool calls, alter ownership, or authorize memory
publication. Keep acquisition and extraction separate from service-controlled
memory writes. Deduplicate copied pages before assessing independent agreement.

## Sources and answer formation

For each candidate claim, retain its supporting passage, original and canonical
URL, title, publication/update date when known, fetch time and content hash.
Repository evidence uses path, commit and line coordinates. A fetch timestamp is
not a publication date. Preserve entity, version, jurisdiction and time qualifiers.
Search-result snippets alone are insufficient evidence for a saved factual claim.

Citation IDs must refer to fetched or authorized stored sources. Resolve URLs from
the service's source records so the generator cannot invent them. A valid URL does
not establish claim support: evaluate the passage and the answer together.

The small centroid model has not demonstrated reliable evidence synthesis.
Initially return relevant source excerpts with citations as source results.
Promote generated evidence-based answers only after they pass held-out support
tests, using the same structured evidence representation in training and inference.

## Memory admission and lifecycle

| Record | Scope and admission |
| --- | --- |
| Conversation | Owner's ordered messages and completion states; bounded context selection |
| Preference or project fact | Owner/workspace scoped, attributed to the user; user can correct it |
| Sourced public claim | Supported passage, applicability and freshness rules; public cache only after explicit nonprivate admission |
| Unverified claim | Owner-scoped candidate; never silently promoted to general knowledge |
| Training candidate | Separate reviewed dataset queue with source and eligibility lineage |

Use an explicit owner identity. A conversation ID is not authorization. A local
pilot can use one configured owner; shared deployment requires authenticated
ownership checks before private cross-session memory is enabled. Private queries
and claims must not enter a public cache.

Store content and provenance as authoritative records; lexical/vector indexes
are rebuildable. Version learned indexes by encoder/tokenizer identity and reindex
after changes; separately trained embedding spaces are not interchangeable.
Record creation and verification times, expiry, applicability,
supporting sources, originating conversation and supersession relationships.
Use states such as candidate, supported, disputed, stale, superseded and deleted.
Supported means backed by the recorded evidence, not universally guaranteed true.

Choose TTL by fact type and version. Reuse a fresh claim only when its entities,
scope and applicability match; similarity alone is insufficient. Reuse cites the
original source and identifies it as cached evidence. Expired or mismatched claims
need revalidation. Conflicting claims remain visible until resolved; a user
correction to public knowledge triggers checking rather than a silent global edit.

Expose view, correct, forget, disable-memory and retention controls through the
API. Conversation deletion should offer removal of memories derived solely from
it. Forgetting removes retrieval/index entries and pending training copies.
Apply the configured retention period to transcripts, sources and research logs,
and avoid automatically retaining secrets or sensitive identifiers.

## Learning without changing live weights

Immediate learning means admitting a retrievable memory for the next turn.
Weight updates happen offline on reviewed, deduplicated examples with a frozen
evaluation set, provenance and checkpoint lineage. Never apply automatic gradient
updates from arbitrary chats, fetched pages or the bot's unsupported outputs.
New vocabulary requires a versioned vocabulary/model change and reevaluation.

Deleting memory cannot erase information already trained into weights. Keep
training eligibility and checkpoint lineage separate and define a removal or
retraining process before promising that capability. Keep previous checkpoints
available for rollback after evaluation regressions.

## Implementation and release gates

Implement owner-scoped storage and memory controls, then the explicit support
router, then one provider and bounded fetcher. Add cited source results and memory
reuse before experimenting with neural synthesis or router calibration. Extend
the planned chat request lifecycle with research status, sources and memory IDs;
exercise it through curl and isolated Compose integration tests.

Required deterministic fixtures cover unsupported questions, fresh-memory reuse,
TTL expiry, version mismatch, contradictory sources, private-owner isolation,
forget/retention, hostile page instructions, redirects to local services, provider
failures, quotas and cancellation. Preserve the current question when budgeting
history and evidence. Measure claim support, answer correctness, appropriate
abstention, unnecessary search, memory reuse accuracy, latency and cost separately.
