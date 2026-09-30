"""Prepare explicitly labeled, isolated Git snapshots of the current source for API tests.

This never commits the developer checkout. Snapshot commits identify the copied
test repository, and provenance records the original HEAD and dirty state.
"""

import argparse
import json
from pathlib import Path
import shutil
import sys

from .bootstrap import bootstrap
from .config import Config
from .git_source import git, resolve
from .verification import capture


def prepare(repo: Path, output: Path) -> dict:
    repo, output = repo.resolve(), output.resolve()
    if output.exists():
        raise ValueError("fixture output must be a new directory")
    if not output.is_relative_to(repo / "build"):
        raise ValueError("fixture output must stay within this repository's build directory")
    config = Config.read(repo / "examples/knowledge/codebase.json")
    source = output / "source"
    source.mkdir(parents=True)
    names = git(repo, "ls-files", "-z", "--cached", "--others", "--exclude-standard").decode().split("\0")
    selected = sorted({name for name in names if name and (config.includes(name) or name == config.license_path)})
    for name in selected:
        original = repo / name
        if not original.exists():
            continue
        if original.is_symlink() or not original.resolve().is_relative_to(repo) or not original.is_file():
            raise ValueError(f"fixture source must be a regular in-repository file: {name}")
        if original.stat().st_size > config.max_file_bytes:
            raise ValueError(f"fixture source exceeds byte limit: {name}")
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    provenance = {"kind": "isolated-working-tree-test-copy", "originCommit": resolve(repo, "HEAD"),
                  "originDirty": bool(git(repo, "status", "--porcelain").strip()),
                  "note": "Snapshot commits belong to the isolated fixture repository, not the developer branch."}
    (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    note = source / "docs/repository-fixture-note.md"
    note.write_text("# Repository fixture flag\n\nThe repository fixture flag is alpha.\n", encoding="utf-8")
    git(source, "init", "--initial-branch=main")

    def commit(message: str):
        git(source, "add", "--all")
        git(source, "-c", "user.name=Centroid-GAI fixture", "-c", "user.email=fixture@example.invalid",
            "-c", "commit.gpgsign=false", "commit", "--no-gpg-sign", "-m", message)

    commit("Repository chat fixture A")
    snapshots = output / "snapshots"
    first = bootstrap(source, "HEAD", config, snapshots / "a")
    tasks_file = source / "docs/codebase-tasks.json"
    registry = json.loads(tasks_file.read_text(encoding="utf-8"))
    available = [task for task in registry["tasks"] if task["status"] != "verified" and not task["dependsOn"]]
    if not available:
        raise ValueError("fixture needs an unfinished task without prerequisites")
    selected_task = min(available, key=lambda task: (task["priority"], task["id"]))
    selected_task["status"] = "verified"
    verified = {task["id"] for task in registry["tasks"] if task["status"] == "verified"}
    remaining = [task for task in registry["tasks"] if task["status"] != "verified"
                 and all(dependency in verified for dependency in task["dependsOn"])]
    if not remaining:
        raise ValueError("fixture needs another eligible unfinished task for a failed verification")
    failed_task = min(remaining, key=lambda task: (task["priority"], task["id"]))
    fixture_check = "tools/knowledge/repository_fixture_check.py"
    failed_task["checks"] = [{"command": f'"{Path(sys.executable).as_posix()}" {fixture_check}', "cwd": "."}]
    (source / fixture_check).write_text("import sys\nprint('Intentional fixture check failure')\nsys.exit(7)\n", encoding="utf-8")
    tasks_file.write_text(json.dumps(registry, indent=2) + "\n", encoding="utf-8")
    note.unlink()
    commit("Repository chat fixture B: reviewed task state and removed source")
    second = bootstrap(source, "HEAD", config, snapshots / "b")
    report_path = snapshots / "b/verification.json"
    report = capture(source, snapshots / "b", failed_task["id"], report_path)
    if report["status"] != "failed" or report["checks"][0]["exitCode"] != 7:
        raise ValueError("fixture verification must capture the intentional check failure")
    result = {"primary": str(snapshots / "a"), "additional": str(snapshots / "b"),
              "root": str(snapshots), "completedFixtureTask": selected_task["id"],
              "verification": {"taskId": failed_task["id"], "status": report["status"], "path": str(report_path)},
              "snapshots": [first, second], "provenance": provenance}
    (output / "fixture.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.repo, args.output), indent=2))


if __name__ == "__main__":
    main()
