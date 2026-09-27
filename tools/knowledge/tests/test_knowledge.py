"""Offline Git repositories exercise the complete import and monitoring boundary."""

from dataclasses import replace
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.knowledge.config import Config
from tools.knowledge.corpus import load_manifest, monitor, snapshot
from tools.knowledge.git_source import GitError, remote_head, resolve, sync
from tools.knowledge.lock import acquire


ARTICLE = """Geography studies the places and environments in which people live.
Mountains, rivers, oceans and forests shape the landscape of our planet.
Physical geography examines natural processes while human geography considers
communities, cities, trade, agriculture and relationships between societies.
Maps use symbols and coordinates to describe locations across the Earth's surface.
Scientists compare observations to understand how landscapes change over time."""


class GitKnowledgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.repo = self.root / "source"
        self.repo.mkdir()
        self.command("init", "-b", "main")
        self.command("config", "user.name", "Knowledge Test")
        self.command("config", "user.email", "test@example.invalid")
        self.command("config", "core.autocrlf", "false")
        (self.repo / "encyclopedia").mkdir()
        (self.repo / "LICENSE").write_text("Fixture content licensed under CC0.\n", encoding="utf-8")
        (self.repo / "encyclopedia/geography.txt").write_text(ARTICLE, encoding="utf-8")
        self.commit()
        self.cfg = Config(name="test-encyclopedia", paths=["encyclopedia"], license="CC0", validation_percent=0)
        self.output = self.root / "corpus"

    def tearDown(self):
        # Windows Git object files can be read-only; TemporaryDirectory handles this.
        self.temp.cleanup()

    def command(self, *args):
        result = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True, check=True)
        return result.stdout.decode().strip()

    def commit(self):
        self.command("add", ".")
        self.command("commit", "-m", "Update encyclopedia")

    def build(self, **kwargs):
        return snapshot(self.repo, "HEAD", kwargs.get("config", self.cfg), kwargs.get("output", self.output))

    def test_committed_snapshot_ignores_dirty_staged_and_untracked_files(self):
        commit = resolve(self.repo, "HEAD")
        (self.repo / "encyclopedia/geography.txt").write_text("Dirty replacement", encoding="utf-8")
        (self.repo / "encyclopedia/new.txt").write_text(ARTICLE + " New content.", encoding="utf-8")
        self.command("add", "encyclopedia/geography.txt")
        result = self.build()
        self.assertEqual(result["commit"], commit)
        self.assertEqual(result["documents"], {"train": 1, "validation": 0})
        self.assertIn("Mountains", (self.output / "train.txt").read_text(encoding="utf-8"))
        item = json.loads((self.output / "train.jsonl").read_text(encoding="utf-8"))
        self.assertEqual(item["sources"][0]["commit"], commit)
        self.assertEqual(item["sources"][0]["path"], "encyclopedia/geography.txt")

    def test_duplicate_text_retains_both_git_sources(self):
        (self.repo / "encyclopedia/copy.md").write_text(ARTICLE + "\n\n", encoding="utf-8")
        self.commit()
        self.assertEqual(self.build()["documents"]["train"], 1)
        item = json.loads((self.output / "train.jsonl").read_text(encoding="utf-8"))
        self.assertEqual(len(item["sources"]), 2)

    def test_code_import_is_opt_in_and_preserves_punctuation_and_provenance(self):
        code = "int x[]={0,1,2,3,4,5,6,7,8,9};\n" * 30
        (self.repo / "encyclopedia/sample.c").write_text(code, encoding="utf-8")
        self.commit()
        self.assertEqual(self.build()["documents"]["train"], 1)
        cfg = replace(self.cfg, extensions=[".c"], min_words=1)
        output = self.root / "code"
        self.build(config=cfg, output=output)
        item = json.loads((output / "train.jsonl").read_text(encoding="utf-8"))
        self.assertEqual(item["text"], code.strip())
        self.assertEqual(item["sources"][0]["path"], "encyclopedia/sample.c")
        self.assertEqual(item["sources"][0]["commit"], resolve(self.repo, "HEAD"))
        (self.repo / "encyclopedia/sample.c").write_bytes(b"int x;\0binary")
        self.commit()
        with self.assertRaisesRegex(ValueError, "no usable"):
            self.build(config=cfg, output=self.root / "bad-code")

    def test_monitor_added_modified_removed_and_ignores_unrelated_commits(self):
        self.build()
        path = self.output / "manifest.json"
        self.assertFalse(monitor(self.repo, "HEAD", self.cfg, path)["update_available"])
        (self.repo / "README.md").write_text("Unrelated project documentation", encoding="utf-8")
        self.commit()
        self.assertFalse(monitor(self.repo, "HEAD", self.cfg, path)["update_available"])
        (self.repo / "encyclopedia/geography.txt").write_text(ARTICLE + " New geography findings.", encoding="utf-8")
        (self.repo / "encyclopedia/history.md").write_text(ARTICLE + " Historical observations.", encoding="utf-8")
        self.commit()
        result = monitor(self.repo, "HEAD", self.cfg, path)
        self.assertTrue(result["update_available"])
        self.assertEqual(result["changes"]["added"], ["encyclopedia/history.md"])
        self.assertEqual(result["changes"]["modified"], ["encyclopedia/geography.txt"])
        (self.repo / "encyclopedia/geography.txt").unlink()
        self.commit()
        self.assertEqual(monitor(self.repo, "HEAD", self.cfg, path)["changes"]["removed"], ["encyclopedia/geography.txt"])

    def test_old_commit_can_be_reproduced_after_edit(self):
        first = self.build()
        (self.repo / "encyclopedia/geography.txt").write_text(ARTICLE + " More recent findings.", encoding="utf-8")
        self.commit()
        snapshot(self.repo, first["commit"], self.cfg, self.root / "old")
        for name in ("train.txt", "train.jsonl", "validation.txt", "validation.jsonl", "SOURCE_LICENSE.txt", "manifest.json"):
            self.assertEqual((self.output / name).read_bytes(), (self.root / "old" / name).read_bytes())

    def test_citations_are_read_from_same_commit_and_changes_are_monitored(self):
        citations = {"encyclopedia/geography.txt": [{"url": "https://example.org/reference",
            "title": "Geography reference", "accessed": "2026-09-27", "license": "CC0",
            "search_query": "physical geography"}]}
        path = self.repo / "references.json"
        path.write_text(json.dumps(citations), encoding="utf-8")
        self.commit()
        cfg = replace(self.cfg, references_path="references.json")
        self.build(config=cfg)
        item = json.loads((self.output / "train.jsonl").read_text(encoding="utf-8"))
        self.assertEqual(item["sources"][0]["references"], citations["encyclopedia/geography.txt"])
        citations["encyclopedia/geography.txt"][0]["accessed"] = "2026-09-28"
        path.write_text(json.dumps(citations), encoding="utf-8")
        self.commit()
        self.assertTrue(monitor(self.repo, "HEAD", cfg, self.output / "manifest.json")["metadata_changed"])

    def test_license_and_policy_changes_trigger_update(self):
        self.build()
        path = self.output / "manifest.json"
        self.assertTrue(monitor(self.repo, "HEAD", replace(self.cfg, min_words=21), path)["config_changed"])
        (self.repo / "LICENSE").write_text("Updated licensing metadata", encoding="utf-8")
        self.commit()
        self.assertTrue(monitor(self.repo, "HEAD", self.cfg, path)["metadata_changed"])

    def test_snapshots_never_overwrite_and_verify_checksums(self):
        self.build()
        with self.assertRaisesRegex(ValueError, "new directory"):
            self.build()
        load_manifest(self.output / "manifest.json")
        (self.output / "train.txt").write_text("Corrupted corpus", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "checksum"):
            monitor(self.repo, "HEAD", self.cfg, self.output / "manifest.json")

    def test_byte_and_file_budgets_fail_without_publishing(self):
        for cfg in [replace(self.cfg, max_file_bytes=10), replace(self.cfg, max_total_bytes=10)]:
            with self.assertRaises(GitError):
                self.build(config=cfg)
            self.assertFalse(self.output.exists())
        (self.repo / "encyclopedia/second.txt").write_text(ARTICLE, encoding="utf-8")
        self.commit()
        with self.assertRaisesRegex(GitError, "max_files"):
            self.build(config=replace(self.cfg, max_files=1))

    def test_missing_license_fails(self):
        with self.assertRaisesRegex(GitError, "metadata missing"):
            self.build(config=replace(self.cfg, license_path="missing.txt"))

    def test_invalid_text_and_lfs_are_recorded_as_rejected(self):
        (self.repo / "encyclopedia/binary.txt").write_bytes(b"abc\x00def")
        (self.repo / "encyclopedia/encoding.txt").write_bytes(b"\xff\xfe")
        (self.repo / "encyclopedia/short.txt").write_text("too short", encoding="utf-8")
        (self.repo / "encyclopedia/lfs.txt").write_text("version https://git-lfs.github.com/spec/v1\noid sha256:abc\nsize 1000", encoding="utf-8")
        self.commit()
        self.assertEqual(len(self.build()["rejected"]), 4)

    def test_symlink_blob_is_never_followed(self):
        # Build a Git symlink directly; this works without Windows symlink privileges.
        target = self.repo / "link-target"
        target.write_text("../../private.txt", encoding="utf-8")
        oid = self.command("hash-object", "-w", "link-target")
        target.unlink()
        self.command("update-index", "--add", "--cacheinfo", "120000", oid, "encyclopedia/linked.txt")
        self.command("commit", "-m", "Add symlink fixture")
        with self.assertRaisesRegex(GitError, "regular Git blob"):
            self.build()

    def test_config_rejects_credentials_path_traversal_and_invalid_limits(self):
        for values in [{"remote_url": "https://token@example.org/repo.git"},
                       {"remote_url": "https://example.org/repo?key=secret"},
                       {"remote_url": "ssh://example.org/repo"}, {"paths": ["../private"]},
                       {"paths": [":(glob)**"]}, {"max_files": False}, {"validation_percent": 100}]:
            with self.subTest(values=values), self.assertRaises(ValueError):
                replace(self.cfg, **values)

    def test_remote_monitor_only_advertises_ref_without_fetching(self):
        cfg = replace(self.cfg, remote_url="https://example.org/encyclopedia.git")
        self.build(config=cfg)
        with patch("tools.knowledge.git_source.remote_head", return_value="a" * 40):
            result = monitor(self.repo, "HEAD", cfg, self.output / "manifest.json")
        self.assertTrue(result["update_available"])
        self.assertEqual(result["mode"], "remote-ref-only")
        with patch("tools.knowledge.git_source.git", side_effect=[b"main\n", b"a" * 40 + b"\trefs/heads/main\n"]) as command:
            self.assertEqual(remote_head(cfg), "a" * 40)
            self.assertEqual(command.call_args.args[1], "ls-remote")

    def test_sync_refuses_existing_user_repository(self):
        cfg = replace(self.cfg, remote_url="https://example.org/encyclopedia.git")
        with self.assertRaisesRegex(GitError, "tool-owned"):
            sync(self.repo, cfg)
        self.assertEqual(self.command("status", "--porcelain"), "")

    def test_sync_uses_shallow_bare_cache_and_no_checkout(self):
        cfg = replace(self.cfg, remote_url="https://example.org/encyclopedia.git")
        cache = self.root / "cache.git"
        cache.mkdir()
        (cache / "centroid-knowledge-source").write_text(cfg.remote_url, encoding="utf-8")
        with patch("tools.knowledge.git_source.git", side_effect=[b"main\n", b"true\n", b"", b"a" * 40 + b"\n"]) as command:
            self.assertEqual(sync(cache, cfg), "a" * 40)
            fetch = command.call_args_list[2].args
            self.assertIn("--depth=1", fetch)
            self.assertIn("--no-recurse-submodules", fetch)
            self.assertEqual(fetch[-1], "+refs/heads/main:refs/centroid/encyclopedia")

    def test_writer_lock_prevents_overlap(self):
        path = self.root / "snapshot.lock"
        with acquire(path), self.assertRaisesRegex(OSError, "already in use"):
            acquire(path)
        with acquire(path):
            pass

    def test_split_excludes_exact_duplicate_leakage(self):
        for i in range(20):
            (self.repo / f"encyclopedia/article-{i}.txt").write_text(ARTICLE + f" Observation {i}.", encoding="utf-8")
        self.commit()
        result = self.build(config=replace(self.cfg, validation_percent=50))
        self.assertGreater(result["documents"]["train"], 0)
        self.assertGreater(result["documents"]["validation"], 0)
        groups = [{json.loads(line)["sha256"] for line in
                   (self.output / f"{split}.jsonl").read_text(encoding="utf-8").splitlines()}
                  for split in ["train", "validation"]]
        self.assertFalse(groups[0] & groups[1])

    def test_cli_exit_status_reports_committed_updates(self):
        self.build()
        config_path = self.root / "config.json"
        from dataclasses import asdict
        import sys
        config_path.write_text(json.dumps(asdict(self.cfg)), encoding="utf-8")
        command = [sys.executable, "-m", "tools.knowledge", "monitor", "--repo", str(self.repo),
                   "--config", str(config_path), "--manifest", str(self.output / "manifest.json")]
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
        (self.repo / "encyclopedia/geography.txt").write_text(ARTICLE + " Additional findings.", encoding="utf-8")
        self.commit()
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 3)

    @unittest.skipUnless(os.environ.get("CGAI_EXECUTABLE"), "set CGAI_EXECUTABLE for native training smoke test")
    def test_snapshot_trains_and_generates_with_native_model(self):
        self.build()
        executable = str(Path(os.environ["CGAI_EXECUTABLE"]).resolve())
        model = self.root / "encyclopedia.cgai"
        trained = subprocess.run([executable, "train", str(self.output / "train.txt"), str(model), "8"],
                                 capture_output=True, text=True, timeout=30)
        self.assertEqual(trained.returncode, 0, trained.stderr)
        self.assertGreater(model.stat().st_size, 0)
        generated = subprocess.run([executable, "generate", str(model), "Geography studies", "10", "0", "42"],
                                   capture_output=True, text=True, timeout=30)
        self.assertEqual(generated.returncode, 0, generated.stderr)
        self.assertTrue(generated.stdout.strip())


if __name__ == "__main__":
    unittest.main()
