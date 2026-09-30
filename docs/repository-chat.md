# Repository conversations

The API can answer codebase questions without training a neural model or contacting
a public search provider. It returns exact source excerpts from a verified Git
snapshot and uses `docs/codebase-tasks.json` for proposed next actions. The reviewed
[codebase brief](codebase-brief.md) is the starting inventory of implemented behavior
and known limits. Answers use deterministic retrieval; they do not establish neural
generalization.

## Prepare committed evidence and start Compose

Commit the source and documentation you want the chatbot to see before preparing
a snapshot. Preparation fails when required files are absent from that commit.
It reports dirty working-tree state, but never includes those uncommitted changes.
An existing output directory must match the requested commit, inclusion policy,
Git inventory and normalized contents exactly. Use a new directory after changing
the policy or commit.

PowerShell, from the repository root:

```powershell
$revision = git rev-parse HEAD
python -m tools.knowledge bootstrap --config examples/knowledge/codebase.json --repo . --ref $revision --output "build/knowledge/repository/$revision"
if ($LASTEXITCODE -ne 0) { throw 'Snapshot preparation failed' }
$env:CGAI_REPOSITORY_ROOT_HOST = (Resolve-Path build/knowledge/repository).Path
$env:CGAI_REPOSITORY_REF = $revision
$env:CGAI_SEARCH_PROVIDER = 'disabled'
$env:CGAI_CHAT_API_TOKEN = 'replace-with-a-private-token'
docker compose -f compose.yaml -f compose.repository.yaml up --build --detach --wait
$chatHeaders = @('-H', "Authorization: Bearer $env:CGAI_CHAT_API_TOKEN", '-H', 'Content-Type: application/json')
curl.exe --fail-with-body @chatHeaders http://localhost:3000/api/v1/knowledge/repository
```

Shell equivalent:

```sh
revision=$(git rev-parse HEAD)
python3 -m tools.knowledge bootstrap --config examples/knowledge/codebase.json --repo . --ref "$revision" --output "build/knowledge/repository/$revision" || exit 1
export CGAI_REPOSITORY_ROOT_HOST="$PWD/build/knowledge/repository"
export CGAI_REPOSITORY_REF="$revision"
export CGAI_SEARCH_PROVIDER=disabled
export CGAI_CHAT_API_TOKEN='replace-with-a-private-token'
docker compose -f compose.yaml -f compose.repository.yaml up --build --detach --wait
curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" http://localhost:3000/api/v1/knowledge/repository
```

The readiness response identifies `repository`, `commit` and `manifestSha256`,
plus loaded snapshots and source coverage. A missing or invalid primary snapshot
reports `ready: false`; creating a repository conversation returns 503. An invalid
additional snapshot is reported rather than substituted for the primary. Public
conversations remain available. The API loads snapshots at startup; restart it
after adding a snapshot or an operator report.

## Ask questions and continue the same conversation

```powershell
$conversation = curl.exe --fail-with-body @chatHeaders --data-binary '@examples/api/repository-create.json' http://localhost:3000/api/v1/conversations | ConvertFrom-Json
$response = curl.exe --fail-with-body @chatHeaders --data-binary '@examples/api/repository-message.json' "http://localhost:3000/api/v1/conversations/$($conversation.id)/messages" | ConvertFrom-Json
$conversation = $response.conversation
$conversation.messages[-1].content

function Ask-Repository([string]$question) {
    $body = @{ requestId = [guid]::NewGuid().ToString(); revision = $script:conversation.revision; content = $question; autoSearch = $false } | ConvertTo-Json -Compress
    $result = $body | curl.exe --fail-with-body @chatHeaders --data-binary '@-' "http://localhost:3000/api/v1/conversations/$($script:conversation.id)/messages" | ConvertFrom-Json
    $script:conversation = $result.conversation
    $script:conversation.messages[-1].content
}
Ask-Repository 'What is the next unfinished step for the repository chatbot?'
Ask-Repository 'Which files should I change first?'
docker compose -f compose.yaml -f compose.repository.yaml restart api
# Wait for /api/v1/health, then reload the persisted conversation after restart.
$conversation = curl.exe --fail-with-body @chatHeaders "http://localhost:3000/api/v1/conversations/$($conversation.id)" | ConvertFrom-Json
Ask-Repository 'How should I verify that change?'
Ask-Repository 'I meant source-mode follow-ups, not neural training.'
Ask-Repository 'What is the smallest next action now?'
```

With shell and `jq`, the same requests use JSON fixtures and the returned revision:

```sh
base=http://localhost:3000/api/v1
conversation=$(curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" -H 'Content-Type: application/json' --data-binary @examples/api/repository-create.json "$base/conversations")
id=$(printf '%s' "$conversation" | jq -r .id)
response=$(curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" -H 'Content-Type: application/json' --data-binary @examples/api/repository-message.json "$base/conversations/$id/messages")
printf '%s' "$response" | jq '.conversation.messages[-1]'
revision=$(printf '%s' "$response" | jq .conversation.revision)
jq -n --argjson revision "$revision" '{requestId:"next-task-1",revision:$revision,content:"What is the next unfinished step for the repository chatbot?",autoSearch:false}' |
  curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" -H 'Content-Type: application/json' --data-binary @- "$base/conversations/$id/messages"
```

Each completed turn advances the revision twice. Keep the request ID and input
unchanged when retrying; use a fresh ID and current revision for a new turn.
The action contains the task ID, rationale, paths, prerequisites, proposed checks
and completion condition. The chatbot does not execute those commands. Ambiguous
follow-ups ask for clarification. Failed and cancelled replies do not update the
persisted task context. Repository scope ignores public source memory and rejects
`publicQuery` and `answerMode: "neural"`.

Explicit task IDs and file paths take precedence over the previous active task.
If several tasks or files are mentioned, select one by its task ID or exact path
before asking an ambiguous follow-up. A filename can resolve a file when it is
unique in the selected snapshot. Unknown paths produce clarification instead of
invented citations. The conversation stores a bounded selected `focusPath` and
unresolved `taskCandidates` alongside its task/topic, so a restart retains the
choice. Corrections and snapshot switches clear stale references; another session
or a failed, cancelled or pending reply cannot supply completed context.

Citations include a source path, Git blob and commit, document and passage hashes,
manifest hash, and exact `snapshot-normalized-lines` spans. These coordinates belong
to the snapshot text, not the mutable checkout. Unsupported operational questions
such as unmeasured production latency or unseen dirty changes produce an explicit
limit. A relevant excerpt is not proof that a test passed.

## Investigate a software change

Repository conversations also support focused development research. Send
`repositoryResearch` with `kind: "impact"` and an existing source path to inspect
that file's direct static imports/includes and reverse dependents, or use `kind: "symbol"`
with an identifier to find lexical occurrences in the indexed source. This uses
the conversation's pinned snapshot and performs no public search or command
execution. The setting belongs on the existing message endpoint.
Impact targets are literal repository-relative paths; symbol targets are single
identifiers. These options apply only to repository-scoped source conversations.

For a new repository conversation, the supplied impact request is:

```json
{
  "requestId": "repository-impact-1",
  "revision": 0,
  "content": "Research the impact of changing persistence/api/src/chat/service.ts",
  "repositoryResearch": {
    "kind": "impact",
    "target": "persistence/api/src/chat/service.ts"
  },
  "autoSearch": false
}
```

Use the existing headers and `Ask-Repository` helper above to run that request through
curl and inspect its structured investigation:

```powershell
$conversation = curl.exe --fail-with-body @chatHeaders --data-binary '@examples/api/repository-create.json' http://localhost:3000/api/v1/conversations | ConvertFrom-Json
$response = curl.exe --fail-with-body @chatHeaders --data-binary '@examples/api/repository-research.json' "http://localhost:3000/api/v1/conversations/$($conversation.id)/messages" | ConvertFrom-Json
$conversation = $response.conversation
$conversation.messages[-1].investigation | ConvertTo-Json -Depth 12
Ask-Repository 'Which tests cover that?'
Ask-Repository 'What should I verify?'
Ask-Repository 'What depends on that?'
Ask-Repository 'Continue this investigation'
```

Natural requests such as `Research the impact of changing
persistence/api/src/chat/service.ts` and `Find references to ChatService` select
the same investigation types. For explicit symbol requests, supply
`"repositoryResearch": {"kind":"symbol","target":"ChatService"}` with the current
revision and a new request ID. Follow-ups retain the investigation's kind and
target within that conversation and snapshot. Switching snapshots clears that
context so results are recomputed against the selected version.

The assistant record includes `investigation` as structured data alongside its
readable `content` and exact `sources` citations:

| Investigation field | Meaning |
| --- | --- |
| `kind`, `target` | The requested impact path or symbol identifier |
| `snapshot` | Repository, commit and manifest hash used for the investigation |
| `findings` | Target, dependency, dependent or symbol-occurrence records, with an evidence `path`, optional `relatedPath` and cited `sourceId` |
| `relatedTasks` | Reviewed task IDs, titles and proposed command/working-directory pairs, with the registry citation's `sourceId` |
| `limitations` | Limits on interpreting the static or lexical findings |
| `truncated` | Whether bounds or unavailable citations left the result incomplete |

Each `sourceId` refers to the message's `sources` citations. Related verification commands
come from reviewed tasks listing the impact target or the symbol-occurrence files. They
are proposed checks; a matching task or test filename does not establish that a
test covers a particular behavior, and the chatbot does not run them. An empty
`relatedTasks` list means no matching reviewed verification task was found.

Dependency indexing recognizes TypeScript/JavaScript literal import, export,
dynamic-import and type-import forms, plus C quoted includes. Python imports and
computed loading are not indexed. This bounded map is not a complete runtime call
graph. Generated code, unresolved external packages and files excluded from the
snapshot can leave gaps; transitive effects and build-flag conditions are not
evaluated. Symbol matches are
lexical occurrences rather than compiler-resolved references and can include
comments or strings. Consult the investigation's limitations before drawing a
conclusion about impact or test coverage. Unsupported targets produce explicit
uncertainty instead of invented source locations.

## Change the source revision

Prepare snapshot B in a new subdirectory after committing changes. Configure
`CGAI_REPOSITORY_ADDITIONAL_SNAPSHOTS` as a JSON array of container paths, for example
`["/app/knowledge/snapshots/<B-commit>"]`, and recreate the API with both Compose files.
Copy B's complete identity from `GET /api/v1/knowledge/repository` into:

```json
{
  "revision": 12,
  "snapshot": {
    "repository": "centroid-codebase",
    "commit": "<full-B-commit>",
    "manifestSha256": "<B-manifest-sha256>"
  }
}
```

POST it to `/api/v1/conversations/<id>/repository`. A successful switch advances
the revision once, records the transition and clears the active topic/task.
Historical messages retain their original citations. An unknown snapshot returns
404; stale revisions or pending work return 409 without changing the conversation.
Mentioning a commit in chat never switches the source. Keep old snapshots mounted
while conversations still use them.

## Capture verification results

The task registry is reviewed project state. Saying "I finished it" records
user-reported progress and does not mark a task verified. An operator can capture
the selected task's reviewed commands from a clean checkout of the exact snapshot:

```sh
python -m tools.knowledge.verification --repo . --snapshot build/knowledge/repository/<commit> --task source-followup-calibration --output build/knowledge/repository/<commit>/verification.json
```

The capture tool binds command, working directory, exit status and bounded output
to the snapshot identity. Restart the API to load the sidecar. Reports are labeled
operator-captured, not independently attested. The server reads them from its
configured snapshot mount; pasted JSON in a message is never execution proof.
Passing checks still require review against the task's completion condition before
changing its registry status in a new commit.

The registry currently labels source-follow-up calibration `implemented`, with
development review and committed verification still pending. The next action is
to review that behavior and capture the checks, rather than treating code presence
or a conversational completion claim as a verified result.

Checks run against an uncommitted workspace can be reported with its dirty state
and file hashes, but they do not verify the existing `main` commit. The capture
command requires a clean checkout whose HEAD matches the selected snapshot. To
capture an importable result before changing the developer branch, use a complete
isolated Git copy containing the runnable implementation, tests and dependency
lockfiles; run the reviewed commands there and identify its temporary commit as
such. The searchable snapshot exclusion policy remains separate from the complete
test checkout. Never relabel a workspace log with an older snapshot identity.

## Run the reproducible HTTP suite

```sh
cd persistence
bun run typecheck
bun run test:native
bun run --cwd api repository:test
bun run test:repository
```

`test:repository` prepares two isolated Git fixture commits from allowlisted current
source, starts a unique Compose project, runs the existing API integration tests,
then runs the 40 repository regression scenarios, eight development follow-up
chains and eight development research chains with public research disabled. It removes only
its own containers and database volume. Fixture provenance records the original
HEAD and dirty state; fixture hashes do not identify commits on the developer branch.
Ports default to 3100 and 55432; `CGAI_E2E_API_PORT` and `CGAI_E2E_POSTGRES_PORT`
override them. `CGAI_PYTHON` can select a Python executable.

Reports in `build/evaluation/repository-chat/<run>/` include raw JSONL requests and
responses, assertions, a summary and a Markdown transcript generated from captured
replies. The unchanged 40-scenario regression suite covers 24 factual/unsupported
questions, eight context chains, four refresh loops and four HTTP scenarios.
The separate eight-chain development suite exercises explicit task/file references,
ambiguity and subsequent selection, corrections, restart and session isolation.
Eight further development research chains exercise impact and symbol investigations,
cited findings, related reviewed checks and durable investigation context. The
research results appear separately under `repositoryResearch` in the report.
Each development suite has its own denominator and may guide implementation
changes; neither is an independent held-out acceptance measurement. All fixtures
and assertions stay outside the searchable corpus. Core follow-ups cross a real
API restart.
Unrun scenarios retain their denominator and cannot count as passes. Cancellation
records a completion race honestly; forced cancellation also has a unit regression.
Timing results describe the named host and run. Client RSS is not server/native
memory, and no production latency budget is inferred from this local suite.

Implementation hashes describe the evaluator's current checkout; they do not
attest the source of an arbitrary remote API server. Fixture provenance identifies
the copied source and its original dirty state separately from the fixture commit.
Operator reports created for synthetic fixtures exercise report import; their
fixture commands are not results of the real project's reviewed checks.

For an existing isolated test API, use the runner directly:

```sh
cd persistence/api
node src/evaluation/repository-chat.ts --snapshot <A-directory> --additional-snapshot <B-directory> --lane full --followups ../../examples/chat/repository-followups-v1.json --research ../../examples/chat/repository-research-v1.json --output <new-report-directory>
```

Set `CGAI_API_URL`, `CGAI_CHAT_API_TOKEN` and `CGAI_E2E_PROJECT` for the test stack.
The runner requires its explicit restart hook; use the wrapper for complete fixture
setup. The API returns complete JSON responses. Streaming and calibrated answer
confidence remain future work.
