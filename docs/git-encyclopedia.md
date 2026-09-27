# Git-based encyclopedia

Git is the source of truth for encyclopedia content. The model consumes a local
training corpus pinned to a commit. A separate maintenance command monitors
updates and can fetch an upstream Git repository. Neither generation nor the
REST service crawls websites, fetches repositories, or performs public searches.
No API keys, accounts, or Python packages are required: only Git and Python 3.13.

The starter [configuration](../examples/knowledge/encyclopedia.json) imports the
four encyclopedia text files already committed under
[`examples/model_corpora/encyclopedia`](../examples/model_corpora/encyclopedia).
This supports broad knowledge about science, geography, history, society, and
everyday objects/processes. The current documents are small seed corpora rather
than a complete encyclopedia.

## Build and train from a committed snapshot

Run from the repository root:

```powershell
python -m tools.knowledge snapshot --repo . --ref HEAD --output build/knowledge/encyclopedia-v1
.\build\cgai.exe train build/knowledge/encyclopedia-v1/train.txt build/knowledge/encyclopedia-v1.cgai 64
```

Use `build/Release/cgai.exe` for a Visual Studio build, or `build/cgai` on
Linux/macOS. The model can be imported through the existing
[persistence CLI](../persistence/api/README.md). Neither command replaces a model
already selected in the application.

`--ref` accepts a commit, tag, or local branch and resolves it to a commit before
reading any content. The importer reads Git blobs directly. Dirty, staged, and
untracked files do not enter the corpus. Commit encyclopedia changes before
building a new snapshot. Git submodules and LFS objects are not downloaded;
supported text files must exist as regular blobs. Symlinks matching the content
selection fail the import.

Snapshots are new directories, never overwritten. They contain:

- `train.txt` and `validation.txt`: normalized UTF-8 corpora.
- `train.jsonl` and `validation.jsonl`: text, content hashes, source paths,
  Git blob IDs, source commit, license label, and optional research citations.
- `SOURCE_LICENSE.txt`: the license file from the same source commit.
- `manifest.json`: commit, source identity, configuration, complete selected
  file inventory, rejected files, document counts, and output checksums.

The configuration specifies literal content paths and `.txt`, `.md`, or `.rst`
extensions. Markdown/reStructuredText syntax is retained as source text; no code,
markup plugins, or embedded commands execute. UTF-8 validation, control-character
checks, minimum word count, alphabetic content, and repetition filters reject
common bad inputs. Rejections appear in the manifest. File-count, per-file, and
total-content budgets fail the import before publication when exceeded.

Exact duplicate normalized text appears once, retaining all source records.
Hash-based train/validation assignment is stable for unchanged text and a fixed
`validation_percent`. Near duplicates and changed revisions are not clustered;
use an independent benchmark for evaluating generalization. A tiny collection
may have no validation documents. Empty training exports fail explicitly.

The publisher stages files beside the destination and renames the directory after
completion. A failed import may leave a `.partial-*` directory. Do not consume it
as a corpus. Snapshot checksums detect accidental changes; they are not a digital
signature or a claim that source content is factually correct. Keep the manifest,
JSONL files, and source license alongside the training text.

## Monitor this repository

```powershell
python -m tools.knowledge monitor --repo . --ref HEAD --manifest build/knowledge/encyclopedia-v1/manifest.json
```

Local monitoring compares committed blob IDs and reports `added`, `modified`,
and `removed` paths. Unrelated application/documentation commits do not trigger a
content update. License, citation, and configuration changes do. Monitoring
verifies the snapshot checksums first; it does not alter the snapshot or model.

Process status is `0` for no update, `3` for an available update, `1` for a runtime
failure, and `2` for invalid CLI arguments. JSON output includes both commits and
the reason for the update. These exit codes make the command usable in CI, cron,
or Windows Task Scheduler. No recurring system task is installed automatically.

## Pull and monitor an external encyclopedia repository

Copy the starter configuration and set:

```json
{
  "name": "upstream-encyclopedia",
  "remote_url": "https://your-git-host.example/organization/encyclopedia.git",
  "branch": "main",
  "paths": ["articles"],
  "license": "the source repository's license identifier",
  "license_path": "LICENSE",
  "validation_percent": 10
}
```

Use a public repository with committed text content and a license appropriate
for your corpus. Authentication, embedded credentials, query tokens, SSH, and
remote helper protocols are not supported. Git runs noninteractively with
credential helpers, hooks, redirects, and recursive submodule fetching disabled.
Repository scripts are never checked out or executed.

```powershell
python -m tools.knowledge sync --config build/upstream-encyclopedia.json --cache build/knowledge/upstream.git --output build/knowledge/upstream-v1
python -m tools.knowledge monitor --config build/upstream-encyclopedia.json --manifest build/knowledge/upstream-v1/manifest.json
```

`sync` performs a shallow, single-branch fetch into a tool-owned bare cache and
then builds a snapshot. It refuses to reuse an ordinary checkout or a cache
belonging to a different remote. OS-held locks prevent overlapping syncs or
snapshot writes to the same destination and release on process exit. Existing
snapshots and application checkouts are left intact if fetch/import fails.

Remote monitoring uses `git ls-remote` only: it compares the advertised branch
commit without pulling content. A changed remote commit can include unrelated
files, so it reports that an update may be available. Fetch and inspect a new
snapshot before training. Pin `snapshot --ref <commit>` when using an existing
checkout; a shallow cache does not guarantee retention of every historical commit.

Schedule monitoring as an external maintenance task. A typical workflow is:

1. Check the source branch against the last accepted corpus manifest.
2. Fetch changed Git content and create a new snapshot directory.
3. Review content/license/citation differences and rejected documents.
4. Train a new model and compare it with the current baseline.
5. Select the new artifact in the application when ready.

Fetch timeout is 120 seconds. Content budgets apply to imported blobs; they do
not bound the size of Git packs downloaded during fetch. Choose a text-focused
repository and monitor cache disk usage for large sources. Corpus assembly and
native training load their text into memory. Native training currently retains
context across blank-line-separated documents.

## Public-web research enriches Git content

Internet searches belong to encyclopedia maintenance. An editor or offline
research tool searches a topic, checks sources, updates articles, records citations,
and commits the reviewed change. Consumers monitor Git and train from those
commits. There is no crawler or search provider in the training/runtime pipeline.

Set `references_path` to a committed JSON file that maps repository-relative
article paths to citation lists:

```json
{
  "articles/geography.md": [
    {
      "url": "https://source.example/reference",
      "title": "Reference title",
      "accessed": "2026-09-27",
      "license": "source license or reuse basis",
      "search_query": "the public search that found this source"
    }
  ]
}
```

Each citation requires `url`, `title`, `accessed` in YYYY-MM-DD format, and `license`.
Additional context such as search queries is preserved. Citation URLs are never
fetched by the importer. Citations are read from the same commit as the articles
and exported alongside their text. Existing original seed content does not have
invented external citations. As the encyclopedia grows, keep knowledge edits and
their evidence together in reviewable Git diffs.

## Tests

```powershell
$env:CGAI_EXECUTABLE = 'build/cgai.exe'
python -m unittest discover -s tools/knowledge/tests -v
```

The suite creates temporary real Git repositories and checks dirty/staged-file
isolation, old-commit reproduction, citations, licensing changes, add/edit/delete
monitoring, immutable outputs, checksums, content budgets, symlinks, exact
deduplication, splits, locks, and CLI status codes. Remote operations use mocks;
tests make no network requests. `CGAI_EXECUTABLE` enables a real snapshot → native
training → generation smoke test. CI builds the trainer and runs the suite on
Linux and Windows using Python's standard library.
