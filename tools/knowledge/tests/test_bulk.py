"""Deterministic preparation and file-budget checks; all fixtures are local."""

import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from tools.knowledge.bulk import canonical_json, cluster_articles, deterministic_gzip, FILE_LIMIT, select_articles, write_bounded, write_shard


class BulkTests(unittest.TestCase):
    def test_canonical_json_and_gzip_are_repeatable(self):
        first = canonical_json({"z": "encyclopedia", "a": 42})
        second = canonical_json({"a": 42, "z": "encyclopedia"})
        self.assertEqual(first, second)
        self.assertEqual(deterministic_gzip(first), deterministic_gzip(second))
        self.assertEqual(gzip.decompress(deterministic_gzip(first)), first)
        self.assertEqual(deterministic_gzip(first)[4:8], b"\0\0\0\0")

    def test_file_budget_rejects_oversized_output(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oversized"
            with self.assertRaisesRegex(ValueError, "1 MiB"):
                write_bounded(path, b"a" * (FILE_LIMIT + 1))
            self.assertFalse(path.exists())

    def test_shard_hashes_match_actual_payload_and_record_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            records = [canonical_json({"id": "1", "text": "Earth is a planet."}),
                       canonical_json({"id": "2", "text": "Mars is a planet."})]
            shard = write_shard(root, "cluster-001", 0, records)
            packed = (root / shard["path"]).read_bytes()
            self.assertEqual(shard["sha256"], hashlib.sha256(packed).hexdigest())
            self.assertEqual(shard["content_sha256"], hashlib.sha256(b"".join(records)).hexdigest())
            self.assertEqual(gzip.decompress(packed), b"".join(records))

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "bulk clustering requires numpy")
    def test_cluster_assignments_and_centroids_repeat(self):
        articles = [{"title": topic, "text": topic + " " + description} for topic, description in [
            ("astronomy", "planet moon solar star galaxy orbit"), ("space", "planet star solar orbit moon"),
            ("biology", "plant animal cell life species evolution"), ("life", "plant cell evolution animal species"),
            ("history", "ancient empire kingdom dynasty battle war"), ("empires", "kingdom battle dynasty ancient empire")]]
        labels, centers = cluster_articles(articles, 3)
        self.assertEqual((labels, centers), cluster_articles(articles, 3))
        self.assertEqual(len(labels), 6)
        self.assertEqual(len(centers), 3)
        self.assertEqual({0, 1, 2}, set(labels))

    @unittest.skipUnless(importlib.util.find_spec("pyarrow"), "bulk selection requires pyarrow")
    def test_selection_filters_and_pins_hash_rank(self):
        import pyarrow as arrow
        import pyarrow.parquet as parquet
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.parquet"
            rows = [{"id": str(i), "title": f"Page {i}", "url": f"https://simple.wikipedia.org/wiki/Page_{i}",
                     "text": f"Observation {i}. " + "Earth water planet ocean nature life science geography. " * 6}
                    for i in range(20)]
            rows.append({**rows[0], "id": "100"})
            rows.append({**rows[0], "id": "101", "text": "short"})
            parquet.write_table(arrow.Table.from_pylist(rows), path)
            selected, report = select_articles(path, 5)
            expected = sorted(rows[:20], key=lambda row: hashlib.sha256(("simplewiki:" + row["id"]).encode()).hexdigest())[:5]
            self.assertEqual({row["id"] for row in selected}, {row["id"] for row in expected})
            self.assertEqual(report["rejected"], {"duplicate_text": 1, "short_or_binary": 1})
            self.assertEqual((selected, report), select_articles(path, 5))


if __name__ == "__main__":
    unittest.main()
