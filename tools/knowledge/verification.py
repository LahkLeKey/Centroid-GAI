"""Capture operator-run reviewed checks for one clean, committed repository snapshot."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import threading
import time
import uuid

from .bootstrap import validate
from .config import Config, relative_path
from .corpus import digest
from .git_source import GitError, git, metadata_blob, resolve

MAX_OUTPUT = 16384
DEFAULT_TIMEOUT = 600
REGISTRY = "docs/codebase-tasks.json"
POLICY = "examples/knowledge/codebase.json"


def timestamp() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def clean_commit(repo: Path, commit: str) -> None:
    if resolve(repo, "HEAD") != commit:
        raise ValueError("verification checkout HEAD must match the selected snapshot commit")
    if git(repo, "status", "--porcelain", "--untracked-files=normal"):
        raise ValueError("verification requires a clean checkout; commit source changes first")


def task_checks(directory: Path, task_id: str) -> list[dict]:
    documents = [json.loads(line) for split in ("train", "validation")
                 for line in (directory / f"{split}.jsonl").read_text(encoding="utf-8").splitlines()]
    by_path = {source["path"]: document["text"] for document in documents for source in document["sources"]}
    registry = json.loads(by_path.get(REGISTRY, "null"))
    if not isinstance(registry, dict) or registry.get("version") != 1 or not isinstance(registry.get("tasks"), list):
        raise ValueError("snapshot has no supported reviewed task registry")
    selected = [task for task in registry["tasks"] if isinstance(task, dict) and task.get("id") == task_id]
    if len(selected) != 1:
        raise ValueError("task ID must identify exactly one reviewed snapshot task")
    task = selected[0]
    evidence = task.get("evidence")
    if not isinstance(evidence, list) or not evidence or any(
            not isinstance(item, dict) or not isinstance(item.get("quote"), str) or not item["quote"]
            or item.get("path") == REGISTRY or item.get("path") not in by_path
            or item["quote"] not in by_path[item["path"]] for item in evidence):
        raise ValueError("task evidence is missing or stale")
    checks = task.get("checks")
    if not isinstance(checks, list) or not 1 <= len(checks) <= 20:
        raise ValueError("reviewed task must provide one to twenty checks")
    for check in checks:
        if (not isinstance(check, dict) or set(check) != {"command", "cwd"}
                or not isinstance(check["command"], str) or not 1 <= len(check["command"]) <= 2000
                or any(ord(char) < 32 for char in check["command"])):
            raise ValueError("invalid reviewed command")
        if check["cwd"] != ".":
            relative_path(check["cwd"])
        arguments(check["command"])
    return checks


def arguments(command: str) -> list[str]:
    """No shell expansion, pipelines, redirection, batch launchers or nested shells."""
    argv = shlex.split(command, posix=True)
    if not argv or any(any(char in token for char in "|;&<>`\n\r\0") or "$(" in token for token in argv):
        raise ValueError("reviewed checks must be simple commands without shell syntax")
    executable = Path(argv[0]).name.lower()
    if executable in {"sh", "bash", "zsh", "cmd", "cmd.exe", "powershell", "powershell.exe", "pwsh", "pwsh.exe"} or executable.endswith((".bat", ".cmd")):
        raise ValueError("reviewed checks cannot invoke a shell or batch file")
    return argv


def drain(stream, result: dict) -> None:
    captured = bytearray()
    truncated = False
    while chunk := stream.read(4096):
        remaining = MAX_OUTPUT - len(captured)
        captured.extend(chunk[:remaining])
        truncated |= len(chunk) > remaining
    stream.close()
    result.update(text=captured.decode("utf-8", errors="replace"), truncated=truncated)


def stop(process) -> None:
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True, timeout=10)
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    if process.poll() is None:
        process.kill()


def run_check(repo: Path, check: dict, timeout: float) -> dict:
    cwd = (repo / check["cwd"]).resolve()
    if not cwd.is_relative_to(repo) or not cwd.is_dir():
        raise ValueError("reviewed command directory must stay inside the checkout")
    start = time.monotonic()
    output, errors = {"text": "", "truncated": False}, {"text": "", "truncated": False}
    try:
        process = subprocess.Popen(arguments(check["command"]), cwd=cwd, shell=False,
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=os.name != "nt")
    except OSError as exc:
        return {**check, "exitCode": 127, "stdout": "", "stderr": str(exc)[:MAX_OUTPUT],
                "durationMs": round((time.monotonic() - start) * 1000), "outputTruncated": False}
    threads = [threading.Thread(target=drain, args=(stream, target), daemon=True)
               for stream, target in ((process.stdout, output), (process.stderr, errors))]
    for thread in threads:
        thread.start()
    timed_out = False
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        stop(process)
        process.wait(timeout=10)
    for thread in threads:
        thread.join(timeout=10)
    if any(thread.is_alive() for thread in threads):
        raise ValueError("verification process left inherited output pipes open")
    error_text = errors["text"]
    if timed_out:
        error_text = ("Verification deadline exceeded.\n" + error_text)[:MAX_OUTPUT]
    return {**check, "exitCode": 124 if timed_out else process.returncode,
            "stdout": output["text"], "stderr": error_text,
            "durationMs": round((time.monotonic() - start) * 1000),
            "outputTruncated": output["truncated"] or errors["truncated"]}


def capture(repo: Path, directory: Path, task_id: str, output: Path, *, timeout=DEFAULT_TIMEOUT) -> dict:
    repo, directory, output = repo.resolve(), directory.resolve(), output.resolve()
    if output.exists() or (output.is_relative_to(directory) and output != directory / "verification.json"):
        raise ValueError("verification output must be a new sidecar; only verification.json may be added to the snapshot root")
    if not isinstance(timeout, (int, float)) or not 0 < timeout <= 3600:
        raise ValueError("verification timeout must be between zero and 3600 seconds")
    if Path(git(repo, "rev-parse", "--show-toplevel").decode().strip()).resolve() != repo:
        raise ValueError("repo must be the checkout root")
    manifest_path = directory / "manifest.json"
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    commit = manifest["source"]["commit"]
    clean_commit(repo, commit)
    config = Config(**json.loads(metadata_blob(repo, commit, POLICY, 2_000_000)))
    validate(directory, repo, commit, config)
    checks = task_checks(directory, task_id)
    started = timestamp()
    results = []
    for check in checks:
        clean_commit(repo, commit)
        results.append(run_check(repo, check, timeout))
    clean_commit(repo, commit)
    if manifest_path.read_bytes() != manifest_bytes:
        raise ValueError("snapshot manifest changed during verification")
    report = {"version": 1, "provenance": "operator-captured",
              "snapshot": {"repository": manifest["source"]["name"], "commit": commit,
                           "manifestSha256": digest(manifest_bytes)},
              "taskId": task_id, "startedAt": started, "finishedAt": timestamp(),
              "status": "passed" if all(check["exitCode"] == 0 for check in results) else "failed", "checks": results}
    encoded = json.dumps(report, indent=2) + "\n"
    if len(encoded.encode("utf-8")) > 1024 * 1024:
        raise ValueError("verification report exceeds the one MiB import limit")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.with_name(output.name + ".partial-" + uuid.uuid4().hex)
    staging.write_text(encoded, encoding="utf-8")
    try:
        os.link(staging, output)
    finally:
        staging.unlink()
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--task", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = capture(args.repo, args.snapshot, args.task, args.output)
        print(json.dumps({"output": str(args.output.resolve()), "status": report["status"], "snapshot": report["snapshot"]}, indent=2))
        return 0 if report["status"] == "passed" else 1
    except (OSError, ValueError, KeyError, TypeError, GitError) as exc:
        print(f"verification: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
