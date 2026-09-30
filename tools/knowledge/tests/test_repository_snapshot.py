"""Repository coverage, exclusions and committed bootstrap checks without network."""

from dataclasses import replace
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.knowledge.bootstrap import bootstrap
from tools.knowledge.config import Config
from tools.knowledge.corpus import digest, snapshot


class RepositorySnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Snapshot Test")
        self.git("config", "user.email", "snapshot@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.write("LICENSE", "MIT fixture\n")
        self.write("docs/brief.md", "Committed source overview and supported interfaces.\n")
        self.commit()
        self.config = Config(name="repository-fixture", paths=["docs"], license="MIT", min_words=1,
                             required_paths=["docs/brief.md"], validation_percent=0)
        self.output = self.root / "snapshot"

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
        self.git("commit", "-m", "Update fixture")

    def test_operational_formats_require_explicit_admission(self):
        config = replace(self.config, paths=["docs", "compose.yaml", "api", ".github/workflows/ci.yml"])
        for path in ("compose.yaml", "api/Dockerfile", "api/.env.example", ".github/workflows/ci.yml"):
            self.assertFalse(config.includes(path), path)
        config = replace(config, extensions=[".md", ".yaml", ".yml"], filenames=["api/Dockerfile", "api/.env.example"])
        for path in ("compose.yaml", "api/Dockerfile", "api/.env.example", ".github/workflows/ci.yml"):
            self.assertTrue(config.includes(path), path)
        for path in ("api/.env", "api/.env.production", "api/.private/notes.md", ".github/other.yml",
                     "api/node_modules/README.md", "api/build/report.md"):
            self.assertFalse(config.includes(path), path)

    def test_exact_filename_and_exclusion_configuration_is_narrow(self):
        for changes in ({"filenames": ["api/.env"]}, {"filenames": ["api/private.pem"]},
                        {"exclude_paths": ["../outside"]}, {"excluded_filenames": ["nested/gold.json"]},
                        {"required_paths": ["/outside"]}, {"exclude_paths": "docs"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                replace(self.config, **changes)

    def test_excluded_fixture_basename_remains_excluded_after_move(self):
        self.write("docs/repository-scenarios-v1.json", '{"gold":"do not index"}\n')
        self.write("docs/archive/repository-scenarios-v1.json", '{"gold":"moved answer"}\n')
        self.write("docs/reports/result.md", "Generated benchmark transcript answer.\n")
        self.write("docs/.env", "SECRET=private\n")
        self.commit()
        config = replace(self.config, extensions=[".md", ".json"], exclude_paths=["docs/reports"],
                         excluded_filenames=["repository-scenarios-v1.json"])
        snapshot(self.repo, "HEAD", config, self.output)
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(manifest["coverage"]["included_paths"], ["docs/brief.md"])
        skipped = {item["path"]: item["reason"] for item in manifest["coverage"]["skipped"]}
        self.assertEqual(skipped["docs/archive/repository-scenarios-v1.json"], "excluded-filename")
        self.assertEqual(skipped["docs/reports/result.md"], "excluded-path")
        self.assertEqual(skipped["docs/.env"], "environment-secret-file")
        self.assertNotIn("gold", (self.output / "train.jsonl").read_text())

    def test_required_operational_file_must_be_committed_and_usable(self):
        self.write("compose.yaml", "services: {}\n")
        config = replace(self.config, paths=["docs", "compose.yaml"], extensions=[".md", ".yaml"],
                         required_paths=["docs/brief.md", "compose.yaml"])
        with self.assertRaisesRegex(ValueError, "required committed content"):
            snapshot(self.repo, "HEAD", config, self.output)
        self.assertFalse(self.output.exists())
        self.write("compose.yaml", "bad\0binary")
        self.commit()
        with self.assertRaisesRegex(ValueError, "required committed content"):
            snapshot(self.repo, "HEAD", config, self.output)
        self.assertFalse(self.output.exists())

    def test_bootstrap_reuses_exact_identity_and_reports_dirty_changes_ignored(self):
        first = bootstrap(self.repo, "HEAD", self.config, self.output)
        self.assertTrue(first["prepared"])
        self.assertEqual(first["worktree"], {"dirty": False, "changesIncluded": False})
        self.write("docs/brief.md", "Dirty overview must remain invisible.\n")
        self.write("docs/untracked.md", "Untracked addition must remain invisible.\n")
        second = bootstrap(self.repo, "HEAD", self.config, self.output)
        self.assertFalse(second["prepared"])
        self.assertEqual(first["manifestSha256"], second["manifestSha256"])
        self.assertEqual(second["worktree"], {"dirty": True, "changesIncluded": False})
        self.assertNotIn("invisible", (self.output / "train.txt").read_text())
        self.commit()
        with self.assertRaisesRegex(ValueError, "snapshot commit"):
            bootstrap(self.repo, "HEAD", self.config, self.output)

    def test_bootstrap_refuses_stale_policy_or_corrupted_content(self):
        bootstrap(self.repo, "HEAD", self.config, self.output)
        with self.assertRaisesRegex(ValueError, "admission policy"):
            bootstrap(self.repo, "HEAD", replace(self.config, min_words=2), self.output)
        (self.output / "train.txt").write_text("corruption", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "checksum"):
            bootstrap(self.repo, "HEAD", self.config, self.output)

    def test_bootstrap_detects_rehashed_text_that_does_not_match_git_blob(self):
        bootstrap(self.repo, "HEAD", self.config, self.output)
        records = self.output / "train.jsonl"
        document = json.loads(records.read_text())
        document["text"] = "Fabricated answer preserving real Git object identifiers."
        document["sha256"] = digest(document["text"].encode())
        records.write_text(json.dumps(document) + "\n", encoding="utf-8")
        (self.output / "train.txt").write_text(document["text"] + "\n\n", encoding="utf-8")
        manifest_path = self.output / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        for name in ("train.jsonl", "train.txt"):
            manifest["files"][name] = digest((self.output / name).read_bytes())
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "text does not match"):
            bootstrap(self.repo, "HEAD", self.config, self.output)

    def test_project_policy_covers_operations_and_excludes_benchmark_answers(self):
        config = Config.read(Path(__file__).resolve().parents[3] / "examples/knowledge/codebase.json")
        for path in config.required_paths:
            self.assertTrue(config.includes(path), path)
        for path in ("compose.repository.yaml", "persistence/api/.env.example", "persistence/db/src/prisma/contract.prisma",
                     "persistence/api/src/chat/repository-code-index.ts", "persistence/api/src/chat/repository-investigation.ts",
                     "persistence/api/src/chat/repository-research.ts", "examples/api/repository-research.json"):
            self.assertTrue(config.includes(path), path)
        for path in ("docs/repository-chat-plan.md", "persistence/api/src/evaluation/repository.ts",
                     "docs/archive/repository-scenarios-v1.json", "persistence/api/src/repository-chat-api.test.ts",
                     "persistence/api/src/chat/repository-service.test.ts", "persistence/db/src/prisma/contract.json",
                     "examples/neural/heldout.txt", "docs/moved/heldout.txt", "docs/moved/repository-fixture.ts",
                     "docs/moved/repository-followups-v1.json", "docs/moved/repository-context.test.ts",
                     "docs/moved/repository-reference.test.ts", "docs/moved/repository-retrieval.test.ts",
                     "docs/moved/repository-chat-followups.ts", "docs/moved/repository-chat-followups.test.ts",
                     "docs/moved/repository-research-v1.json", "docs/moved/repository-code-index.test.ts",
                     "docs/moved/repository-investigation.test.ts", "docs/moved/repository-research.test.ts",
                     "docs/moved/repository-chat-research.test.ts",
                     "src/knowledge_catalog/01-database-and-persistence.c"):
            self.assertFalse(config.includes(path), path)


if __name__ == "__main__":
    unittest.main()
