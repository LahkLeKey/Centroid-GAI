"""Prepare or verify committed repository knowledge without changing the checkout."""

from dataclasses import asdict
import json
from pathlib import Path

from . import git_source
from .corpus import digest, inventory, load_manifest, normalize, snapshot


def validate(directory: Path, repo: Path, commit: str, config) -> dict:
    """Check hashes, current admission policy, exact Git inventory and coverage."""
    manifest = load_manifest(directory / "manifest.json")
    if manifest["source"]["commit"] != commit:
        raise ValueError("snapshot commit does not match requested ref; use a new output directory")
    if manifest["config_sha256"] != config.fingerprint() or manifest["config"] != asdict(config):
        raise ValueError("snapshot admission policy changed; use a new output directory")
    entries, license_data, _, references_hash = inventory(repo, commit, config)
    if manifest["entries"] != [asdict(entry) for entry in entries]:
        raise ValueError("snapshot inventory does not match committed source")
    if (manifest["source"]["license_sha256"] != digest(license_data)
            or manifest["source"]["references_sha256"] != references_hash):
        raise ValueError("snapshot metadata does not match committed source")
    known = {entry.path: entry for entry in entries}
    included = set()
    for split in ("train", "validation"):
        lines = (directory / f"{split}.jsonl").read_text(encoding="utf-8").splitlines()
        if len(lines) != manifest["documents"][split]:
            raise ValueError("snapshot document count mismatch")
        for line in lines:
            document = json.loads(line)
            if digest(document["text"].encode("utf-8")) != document["sha256"]:
                raise ValueError("snapshot document hash mismatch")
            for source in document["sources"]:
                entry = known.get(source["path"])
                if entry is None or source["commit"] != commit or source["blob"] != entry.oid:
                    raise ValueError("snapshot provenance does not match committed source")
                if not config.includes(source["path"]):
                    raise ValueError("excluded path found in snapshot: " + source["path"])
                original = git_source.blob(repo, entry.oid, config.max_file_bytes)
                if document["text"] != normalize(original, config.min_words, code=config.is_code(entry.path)):
                    raise ValueError("snapshot text does not match committed source")
                included.add(source["path"])
    missing = sorted(set(config.required_paths) - included)
    if missing:
        raise ValueError("required committed content missing or rejected: " + ", ".join(missing))
    coverage = manifest.get("coverage", {})
    if coverage.get("included_paths") != sorted(included) or coverage.get("required_paths") != config.required_paths:
        raise ValueError("snapshot coverage does not match document inventory")
    return manifest


def bootstrap(repo: Path, ref: str, config, output: Path) -> dict:
    """Create a snapshot once, or validate its requested identity before API startup."""
    commit = git_source.resolve(repo, ref)
    prepared = not output.exists()
    if prepared:
        snapshot(repo, commit, config, output)
    manifest = validate(output, repo, commit, config)
    bare = git_source.git(repo, "rev-parse", "--is-bare-repository").strip() == b"true"
    dirty = None if bare else bool(git_source.git(repo, "status", "--porcelain", "--untracked-files=normal"))
    return {"output": str(output.resolve()), "prepared": prepared, "commit": commit,
            "manifestSha256": digest((output / "manifest.json").read_bytes()),
            "coverage": manifest["coverage"], "rejected": manifest["rejected"],
            "worktree": {"dirty": dirty, "changesIncluded": False},
            "notice": "Only committed files are visible. Commit working-tree changes and prepare a new snapshot to expose them."}
