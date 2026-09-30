"""Operator-captured checks bind actual clean source and reviewed task commands."""

from dataclasses import asdict
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.knowledge.config import Config
from tools.knowledge.corpus import snapshot
from tools.knowledge.verification import MAX_OUTPUT, arguments, capture, run_check


class VerificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Verification Test")
        self.git("config", "user.email", "verification@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.python = Path(sys.executable).as_posix()
        self.command = f'"{self.python}" scripts/check.py'
        self.check = {"command": self.command, "cwd": "."}
        self.config = Config(name="verification-fixture", paths=["docs", "scripts"], license="MIT",
                             extensions=[".md", ".json", ".py"], min_words=1, validation_percent=0,
                             required_paths=["docs/brief.md", "docs/codebase-tasks.json"])
        self.write("LICENSE", "MIT fixture\n")
        self.write("docs/brief.md", "Reviewed fixture checks verify the local source.\n")
        self.write("scripts/check.py", "import sys\nprint('actual fixture output')\nsys.exit(0)\n")
        registry = {"version": 1, "tasks": [{"id": "fixture-check", "checks": [self.check],
                     "evidence": [{"path": "docs/brief.md", "quote": "Reviewed fixture checks"}]}]}
        self.write("docs/codebase-tasks.json", json.dumps(registry))
        self.write("examples/knowledge/codebase.json", json.dumps(asdict(self.config)))
        self.commit()
        self.directory = self.root / "snapshot"
        self.output = self.root / "verification.json"
        snapshot(self.repo, "HEAD", self.config, self.directory)

    def tearDown(self):
        self.temp.cleanup()

    def git(self, *args):
        result = subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True)
        return result.stdout.decode().strip()

    def write(self, path, text):
        target = self.repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-m", "Update verification fixture")

    def test_capture_records_actual_output_identity_and_operator_provenance(self):
        before = (self.directory / "manifest.json").read_bytes()
        report = capture(self.repo, self.directory, "fixture-check", self.output)
        self.assertEqual(report["provenance"], "operator-captured")
        self.assertEqual(report["snapshot"]["repository"], "verification-fixture")
        self.assertEqual(report["snapshot"]["commit"], self.git("rev-parse", "HEAD"))
        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["checks"][0]["command"], self.command)
        self.assertEqual(report["checks"][0]["exitCode"], 0)
        self.assertIn("actual fixture output", report["checks"][0]["stdout"])
        self.assertEqual(json.loads(self.output.read_text()), report)
        self.assertEqual((self.directory / "manifest.json").read_bytes(), before)
        with self.assertRaisesRegex(ValueError, "new sidecar"):
            capture(self.repo, self.directory, "fixture-check", self.output)

    def test_changed_or_uncommitted_source_cannot_claim_snapshot_checks(self):
        self.write("scripts/check.py", "print('dirty source')\n")
        with self.assertRaisesRegex(ValueError, "clean checkout"):
            capture(self.repo, self.directory, "fixture-check", self.output)
        self.assertFalse(self.output.exists())
        self.commit()
        with self.assertRaisesRegex(ValueError, "HEAD must match"):
            capture(self.repo, self.directory, "fixture-check", self.output)

    def test_unknown_task_and_snapshot_sidecar_path_are_refused(self):
        with self.assertRaisesRegex(ValueError, "exactly one"):
            capture(self.repo, self.directory, "invented-command", self.output)
        for output in (self.directory / "train.jsonl", self.directory / "nested/verification.json"):
            with self.assertRaisesRegex(ValueError, "new sidecar"):
                capture(self.repo, self.directory, "fixture-check", output)

    def test_exact_snapshot_sidecar_is_allowed_without_changing_manifest(self):
        before = (self.directory / "manifest.json").read_bytes()
        output = self.directory / "verification.json"
        report = capture(self.repo, self.directory, "fixture-check", output)
        self.assertEqual(report["status"], "passed")
        self.assertEqual((self.directory / "manifest.json").read_bytes(), before)
        self.assertEqual(json.loads(output.read_text()), report)

    def test_failed_reviewed_command_is_captured_as_failure(self):
        self.write("scripts/check.py", "import sys\nprint('observed failure', file=sys.stderr)\nsys.exit(9)\n")
        self.commit()
        directory = self.root / "failed-snapshot"
        snapshot(self.repo, "HEAD", self.config, directory)
        report = capture(self.repo, directory, "fixture-check", self.output)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["checks"][0]["exitCode"], 9)
        self.assertIn("observed failure", report["checks"][0]["stderr"])

    def test_manifest_change_during_checks_cannot_rebind_the_report(self):
        def changed_manifest(repo, check, timeout):
            result = run_check(repo, check, timeout)
            manifest = self.directory / "manifest.json"
            manifest.write_bytes(manifest.read_bytes() + b"\n")
            return result
        with patch("tools.knowledge.verification.run_check", side_effect=changed_manifest):
            with self.assertRaisesRegex(ValueError, "manifest changed"):
                capture(self.repo, self.directory, "fixture-check", self.output)
        self.assertFalse(self.output.exists())

    def test_shell_syntax_shell_launchers_and_escaping_cwd_are_rejected(self):
        for command in ("bun run test && echo forged", "sh -c echo", "cmd.exe /c echo", "checks.cmd", "node script.js > result"):
            with self.subTest(command=command), self.assertRaises(ValueError):
                arguments(command)
        with self.assertRaisesRegex(ValueError, "inside the checkout"):
            run_check(self.repo, {"command": self.command, "cwd": "../"}, 1)

    def test_output_and_process_duration_are_bounded(self):
        self.write("scripts/check.py", "import sys\nprint('x' * 100000)\nprint('y' * 100000, file=sys.stderr)\n")
        result = run_check(self.repo, self.check, 5)
        self.assertEqual(result["exitCode"], 0)
        self.assertTrue(result["outputTruncated"])
        self.assertLessEqual(len(result["stdout"]), MAX_OUTPUT)
        self.assertLessEqual(len(result["stderr"]), MAX_OUTPUT)
        self.write("scripts/check.py", "import time\ntime.sleep(30)\n")
        result = run_check(self.repo, self.check, 0.1)
        self.assertEqual(result["exitCode"], 124)
        self.assertIn("deadline", result["stderr"])


if __name__ == "__main__":
    unittest.main()
