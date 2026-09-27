"""Build immutable, attributable corpora from one Git commit and monitor changes."""

from dataclasses import asdict
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import unicodedata
from urllib.parse import urlsplit
import uuid

from . import git_source
from .config import CODE_EXTENSIONS
from .lock import acquire


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalize(data: bytes, min_words: int, *, code: bool = False) -> str:
    """Preserve text/Markdown structure while normalizing Unicode and whitespace."""
    text = data.decode("utf-8-sig", errors="strict")
    if "\0" in text or text.startswith("version https://git-lfs.github.com/spec/v1"):
        raise ValueError("binary content or Git LFS pointer")
    if any(unicodedata.category(c) == "Cc" and c not in "\n\r\t" for c in text):
        raise ValueError("unexpected control characters")
    text = unicodedata.normalize("NFC", text)
    text = "\n".join(line.rstrip() for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n")).strip()
    words = text.split()
    if len(words) < min_words:
        raise ValueError("too few words")
    if not code and sum(c.isalpha() for c in text) / max(1, len(text)) < 0.4:
        raise ValueError("low alphabetic content")
    if not code and len({word.casefold() for word in words}) / len(words) < 0.05:
        raise ValueError("excessive repetition")
    return text


def references(repo, commit, config):
    if config.references_path is None:
        return {}, None
    raw = git_source.metadata_blob(repo, commit, config.references_path, config.max_file_bytes)
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError("references must map content paths to citation lists")
    for path, citations in value.items():
        if not config.includes(path) or not isinstance(citations, list):
            raise ValueError("reference paths must match configured content")
        for citation in citations:
            if not isinstance(citation, dict) or not all(isinstance(citation.get(key), str)
                    and citation[key].strip() for key in ("url", "title", "accessed", "license")):
                raise ValueError("citations require url, title, accessed, and license")
            url = urlsplit(citation["url"])
            if url.scheme not in {"https", "http"} or not url.hostname or url.username is not None or url.password is not None:
                raise ValueError("citation URL must be public HTTP(S) without credentials")
            datetime.strptime(citation["accessed"], "%Y-%m-%d")
    return value, digest(raw)


def inventory(repo, commit, config):
    entries = git_source.entries(repo, commit, config)
    license_data = git_source.metadata_blob(repo, commit, config.license_path, config.max_file_bytes)
    if not license_data.strip():
        raise ValueError("source license is empty")
    citations, references_hash = references(repo, commit, config)
    return entries, license_data, citations, references_hash


def snapshot(repo: Path, ref: str, config, output: Path) -> dict:
    """Import only committed files; staged, dirty, and untracked content is ignored."""
    output.parent.mkdir(parents=True, exist_ok=True)
    with acquire(output.with_name(output.name + ".lock")):
        if output.exists():
            raise ValueError("snapshot output must be a new directory")
        commit = git_source.resolve(repo, ref)
        entries, license_data, citations, references_hash = inventory(repo, commit, config)
        documents, rejected = {}, []
        for entry in entries:
            try:
                text = normalize(git_source.blob(repo, entry.oid, config.max_file_bytes), config.min_words,
                                 code=Path(entry.path).suffix.lower() in CODE_EXTENSIONS)
            except (ValueError, UnicodeError) as exc:
                rejected.append({"path": entry.path, "reason": str(exc)})
                continue
            sha = digest(text.encode("utf-8"))
            item = documents.setdefault(sha, {"sha256": sha, "text": text, "sources": []})
            item["sources"].append({"path": entry.path, "blob": entry.oid, "commit": commit,
                                    "license": config.license, "references": citations.get(entry.path, [])})
        if not documents:
            raise ValueError("no usable encyclopedia documents in this commit")
        staging = output.with_name(output.name + ".partial-" + uuid.uuid4().hex)
        staging.mkdir()
        counts = {"train": 0, "validation": 0}
        for split in counts:
            with (staging / f"{split}.jsonl").open("w", encoding="utf-8", newline="\n") as records, \
                    (staging / f"{split}.txt").open("w", encoding="utf-8", newline="\n") as text_file:
                for sha, item in sorted(documents.items()):
                    selected = "validation" if int(sha[:8], 16) % 100 < config.validation_percent else "train"
                    if selected == split:
                        records.write(json.dumps(item, ensure_ascii=False, sort_keys=True) + "\n")
                        text_file.write(item["text"] + "\n\n")
                        counts[split] += 1
        if not counts["train"]:
            raise ValueError("empty training split; collect more documents or reduce validation_percent")
        (staging / "SOURCE_LICENSE.txt").write_bytes(license_data)
        files = {path.name: digest(path.read_bytes()) for path in sorted(staging.iterdir())}
        manifest = {
            "schema_version": 1,
            "source": {"name": config.name, "remote_url": config.remote_url, "commit": commit,
                       "license_sha256": digest(license_data), "references_sha256": references_hash},
            "config": asdict(config), "config_sha256": config.fingerprint(),
            "entries": [asdict(entry) for entry in entries], "rejected": rejected,
            "documents": counts, "files": files,
            "normalization": "utf8-nfc-line-endings-v1", "deduplication": "normalized-text-sha256",
            "split": "first-32-hash-bits-modulo-100"}
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        os.rename(staging, output)
        return {"output": str(output), "commit": commit, "documents": counts, "rejected": rejected}


def load_manifest(path):
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported snapshot schema")
    # An accidentally edited corpus must not silently remain the monitoring baseline.
    required = {"train.txt", "validation.txt", "train.jsonl", "validation.jsonl", "SOURCE_LICENSE.txt"}
    if set(manifest["files"]) != required:
        raise ValueError("invalid snapshot file inventory")
    for name, expected in manifest["files"].items():
        if digest((path.parent / name).read_bytes()) != expected:
            raise ValueError(f"snapshot checksum mismatch: {name}")
    return manifest


def monitor(repo: Path, ref: str, config, manifest_path: Path) -> dict:
    """Compare a snapshot with a local commit or advertised remote head; never fetch."""
    previous = load_manifest(manifest_path)
    if previous["source"]["name"] != config.name:
        raise ValueError("snapshot source name does not match configuration")
    policy_changed = previous["config_sha256"] != config.fingerprint()
    if config.remote_url:
        commit = git_source.remote_head(config)
        return {"update_available": commit != previous["source"]["commit"] or policy_changed,
                "previous_commit": previous["source"]["commit"], "current_commit": commit,
                "config_changed": policy_changed, "mode": "remote-ref-only"}
    commit = git_source.resolve(repo, ref)
    entries, license_data, _, references_hash = inventory(repo, commit, config)
    before = {entry["path"]: entry["oid"] for entry in previous["entries"]}
    after = {entry.path: entry.oid for entry in entries}
    changes = {"added": sorted(after.keys() - before.keys()),
               "removed": sorted(before.keys() - after.keys()),
               "modified": sorted(path for path in before.keys() & after.keys() if before[path] != after[path])}
    metadata_changed = (digest(license_data) != previous["source"]["license_sha256"]
                        or references_hash != previous["source"]["references_sha256"])
    return {"update_available": policy_changed or metadata_changed or any(changes.values()),
            "previous_commit": previous["source"]["commit"], "current_commit": commit,
            "config_changed": policy_changed, "metadata_changed": metadata_changed,
            "mode": "local-committed-content", "changes": changes}
