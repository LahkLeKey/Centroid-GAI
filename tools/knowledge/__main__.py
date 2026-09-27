"""Git encyclopedia maintenance: snapshot, monitor, or sync; never runtime crawling."""

import argparse
import json
from pathlib import Path
import sys

from .config import Config
from .corpus import monitor, snapshot
from .git_source import GitError, sync
from .lock import acquire


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["snapshot", "monitor", "sync"])
    parser.add_argument("--config", type=Path, default=Path("examples/knowledge/encyclopedia.json"))
    parser.add_argument("--repo", type=Path, default=Path("."), help="local source checkout (snapshot/local monitor)")
    parser.add_argument("--ref", default="HEAD", help="local commit, tag, or branch to read")
    parser.add_argument("--cache", type=Path, default=Path("build/knowledge/upstream.git"))
    parser.add_argument("--output", type=Path, help="new corpus directory (snapshot/sync)")
    parser.add_argument("--manifest", type=Path, help="previous corpus manifest (monitor)")
    args = parser.parse_args()
    if args.command in {"snapshot", "sync"} and args.output is None:
        parser.error("snapshot/sync requires --output")
    if args.command == "monitor" and args.manifest is None:
        parser.error("monitor requires --manifest")
    try:
        config = Config.read(args.config)
        if args.command == "snapshot":
            result = snapshot(args.repo, args.ref, config, args.output)
        elif args.command == "monitor":
            result = monitor(args.repo, args.ref, config, args.manifest)
        else:
            if args.output.exists():
                raise ValueError("snapshot output must be a new directory")
            args.cache.parent.mkdir(parents=True, exist_ok=True)
            with acquire(args.cache.with_name(args.cache.name + ".lock")):
                commit = sync(args.cache, config)
                result = snapshot(args.cache, commit, config, args.output)
        print(json.dumps(result, indent=2))
        return 3 if result.get("update_available") else 0
    except (OSError, ValueError, TypeError, KeyError, GitError) as exc:
        print(f"knowledge: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
