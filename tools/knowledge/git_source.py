"""Read committed blobs; sync into an isolated bare cache without checking out code."""

from dataclasses import dataclass
import os
from pathlib import Path
import re
import subprocess


class GitError(RuntimeError):
    pass


def git(repo: Path | None, *args: str, limit=10_000_000) -> bytes:
    """Noninteractive Git: no hooks, helpers, submodule recursion, or remote code."""
    command = ["git", "--no-pager", "--literal-pathspecs",
               "-c", f"core.hooksPath={os.devnull}", "-c", "credential.helper=",
               "-c", "http.followRedirects=false", "-c", "submodule.recurse=false",
               "-c", "protocol.allow=never", "-c", "protocol.https.allow=always"]
    if repo is not None:
        command += ["-C", str(repo)]
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    env.update(GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1",
               GIT_CONFIG_GLOBAL=os.devnull, GIT_ATTR_NOSYSTEM="1", GIT_NO_REPLACE_OBJECTS="1",
               GIT_ASKPASS="", SSH_ASKPASS="")
    try:
        result = subprocess.run(command + list(args), capture_output=True, env=env, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise GitError(f"Git invocation failed: {type(exc).__name__}") from exc
    if result.returncode:
        message = result.stderr.decode("utf-8", errors="replace").strip()[:2000]
        raise GitError(message or f"Git exited with {result.returncode}")
    if len(result.stdout) > limit:
        raise GitError("Git output exceeds configured limit")
    return result.stdout


def resolve(repo: Path, ref: str) -> str:
    value = git(repo, "rev-parse", "--verify", "--end-of-options", ref + "^{commit}").decode().strip()
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", value):
        raise GitError("invalid commit object ID")
    return value


@dataclass(frozen=True)
class Entry:
    path: str
    oid: str
    size: int


def entries(repo: Path, commit: str, config) -> list[Entry]:
    """List only regular committed content files, never symlinks or submodules."""
    result = []
    listing = git(repo, "ls-tree", "-rlz", "--full-tree", commit, "--", *config.paths,
                  limit=max(1_000_000, config.max_files * 1024))
    for line in listing.split(b"\0"):
        if not line:
            continue
        info, raw_path = line.split(b"\t", 1)
        mode, kind, oid, size = info.split()
        path = raw_path.decode("utf-8", errors="strict")
        if not config.includes(path):
            continue
        if mode not in {b"100644", b"100755"} or kind != b"blob":
            raise GitError(f"content must be a regular Git blob: {path}")
        if int(size) > config.max_file_bytes:
            raise GitError(f"content exceeds max_file_bytes: {path}")
        result.append(Entry(path, oid.decode("ascii"), int(size)))
        if len(result) > config.max_files:
            raise GitError("content exceeds max_files")
    if sum(entry.size for entry in result) > config.max_total_bytes:
        raise GitError("content exceeds max_total_bytes")
    return result


def blob(repo: Path, oid: str, limit: int) -> bytes:
    size = int(git(repo, "cat-file", "-s", oid).strip())
    if size > limit:
        raise GitError("blob exceeds configured byte limit")
    return git(repo, "cat-file", "blob", oid, limit=limit)


def metadata_blob(repo: Path, commit: str, path: str, limit: int) -> bytes:
    listing = git(repo, "ls-tree", "-z", commit, "--", path).split(b"\0")[0]
    if not listing:
        raise GitError(f"required metadata missing from commit: {path}")
    info, raw_path = listing.split(b"\t", 1)
    mode, kind, oid = info.split()
    if mode not in {b"100644", b"100755"} or kind != b"blob" or raw_path.decode() != path:
        raise GitError(f"metadata must be a regular file: {path}")
    return blob(repo, oid.decode(), limit)


def remote_head(config) -> str:
    if not config.remote_url:
        raise ValueError("remote_url is required for remote monitoring and sync")
    git(None, "check-ref-format", "--branch", config.branch)
    ref = "refs/heads/" + config.branch
    output = git(None, "ls-remote", "--exit-code", "--refs", config.remote_url, ref).decode()
    matches = [line.split()[0] for line in output.splitlines() if line.split()[1] == ref]
    if len(matches) != 1 or not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", matches[0]):
        raise GitError("remote branch did not resolve to one commit")
    return matches[0]


def sync(cache: Path, config) -> str:
    """Fetch one branch into a tool-owned bare cache; leave application checkouts alone."""
    if not config.remote_url:
        raise ValueError("sync requires remote_url; use snapshot for an existing local repository")
    git(None, "check-ref-format", "--branch", config.branch)
    marker = cache / "centroid-knowledge-source"
    if cache.exists():
        if not marker.is_file() or marker.read_text(encoding="utf-8") != config.remote_url:
            raise GitError("cache must be tool-owned and match remote_url")
        if git(cache, "rev-parse", "--is-bare-repository").strip() != b"true":
            raise GitError("cache is not a bare repository")
    else:
        cache.parent.mkdir(parents=True, exist_ok=True)
        git(None, "init", "--bare", str(cache.resolve()))
        marker.write_text(config.remote_url, encoding="utf-8")
    git(cache, "fetch", "--no-tags", "--no-recurse-submodules", "--depth=1", config.remote_url,
        f"+refs/heads/{config.branch}:refs/centroid/encyclopedia")
    return resolve(cache, "refs/centroid/encyclopedia")
