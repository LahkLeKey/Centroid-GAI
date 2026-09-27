"""Offline bulk encyclopedia acquisition and deterministic, Git-sized clustering."""

import argparse
from collections import Counter
from functools import lru_cache
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import unicodedata
import urllib.request

from .lock import acquire
from .config import Config, relative_path
from .git_source import remote_head


DATASET_REVISION = "3e1f92c331f318af862b87e2319ed5dc26d80f5d"
DATASET_PATH = "20231101.simple/train-00000-of-00001.parquet"
DATASET_SHA256 = "31bded16768a47c286becd292079122f5d7d4397a17b87d4250a00ccd581e6f0"
DATASET_BYTES = 156885218
DATASET_URL = f"https://huggingface.co/datasets/wikimedia/wikipedia/resolve/{DATASET_REVISION}/{DATASET_PATH}"
FILE_LIMIT = 1024 * 1024
RELEASE_LIMIT = 128 * 1024 * 1024
SHARD_TEXT_LIMIT = 256 * 1024
STOP_WORDS = frozenset("the and that this with from they their there were was are for not have has had but also which when who into about some other than been more most such only first can all one two its his her she him them then these those would could will may many very used after before over between each both what where years year people during made known called new became being through under same time part name including because later while until out did does how like usually often much several different well born however about is it as on to of in at by be an or if so we he us our".split())


def canonical_json(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def deterministic_gzip(data):
    return gzip.compress(data, compresslevel=9, mtime=0)


def write_bounded(path, data):
    if len(data) > FILE_LIMIT:
        raise ValueError(f"generated file exceeds 1 MiB: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def select_articles(path, limit):
    """Select a diverse stable sample by page-ID hash from a pinned dataset ordering."""
    import heapq
    import pyarrow.parquet as parquet
    candidates, seen, rejected, scanned = [], set(), Counter(), 0
    for batch in parquet.ParquetFile(path).iter_batches(batch_size=1000):
        for row in batch.to_pylist():
            scanned += 1
            text = unicodedata.normalize("NFC", row["text"].replace("\r\n", "\n").replace("\r", "\n")).strip()
            if "\0" in text or len(text.split()) < 40:
                rejected["short_or_binary"] += 1
                continue
            if len(text.encode("utf-8")) > 65536:
                rejected["over_64_KiB"] += 1
                continue
            sha = hashlib.sha256(text.encode("utf-8")).hexdigest()
            if sha in seen:
                rejected["duplicate_text"] += 1
                continue
            seen.add(sha)
            record = {"id": row["id"], "title": row["title"], "url": row["url"],
                      "text": text, "sha256": sha,
                      "attribution": "Simple English Wikipedia contributors",
                      "history_url": row["url"] + "?action=history",
                      "license": "CC-BY-SA-3.0; GFDL (upstream dataset declaration)"}
            rank = int(hashlib.sha256(("simplewiki:" + row["id"]).encode()).hexdigest(), 16)
            item = (-rank, row["id"], record)
            if len(candidates) < limit:
                heapq.heappush(candidates, item)
            elif item[:2] > candidates[0][:2]:
                heapq.heapreplace(candidates, item)
    selected = sorted((item[2] for item in candidates), key=lambda row: int(row["id"]))
    return selected, {"scanned": scanned, "eligible_unique": len(seen), "rejected": dict(sorted(rejected.items()))}


@lru_cache(maxsize=100000)
def feature(word):
    hashed = hashlib.blake2b(word.encode(), digest_size=8, person=b"cgai-kb-v1").digest()
    return int.from_bytes(hashed[:4], "little") % 128, 1 if hashed[4] & 1 else -1


def terms(article):
    return [word for word in re.findall(r"[a-z]{3,}", article.lower()) if word not in STOP_WORDS]


def cluster_articles(articles, count):
    """Deterministic spherical k-means on 128-dimensional signed term hashes."""
    import numpy as np
    vectors = np.zeros((len(articles), 128), dtype=np.float64)
    for index, article in enumerate(articles):
        counts = Counter(terms(article["text"])[:2048] + terms(article["title"]) * 4)
        for word, frequency in sorted(counts.items()):
            column, sign = feature(word)
            vectors[index, column] += sign * (1 + math.log(frequency))
    norms = np.linalg.norm(vectors, axis=1)
    vectors /= np.maximum(norms, 1e-12)[:, None]
    count = min(count, len(articles))
    centers = [vectors[0].copy()]
    similarities = vectors @ centers[0]
    chosen = {0}
    for _ in range(1, count):
        distances = similarities.copy()
        for index in chosen:
            distances[index] = 2
        index = int(np.argmin(np.round(distances, 12)))
        chosen.add(index)
        centers.append(vectors[index].copy())
        similarities = np.maximum(similarities, vectors @ centers[-1])
    centers = np.array(centers)
    for _ in range(8):
        labels = np.argmax(np.round(vectors @ centers.T, 12), axis=1)
        for index in range(count):
            members = vectors[labels == index]
            if len(members):
                center = members.sum(axis=0)
                centers[index] = np.round(center / max(np.linalg.norm(center), 1e-12), 12)
    labels = np.argmax(np.round(vectors @ centers.T, 12), axis=1)
    return labels.tolist(), centers.tolist()


def build(cache, output, article_limit=20000, cluster_count=32):
    """Create source shards; native training adds model shards as a separate offline step."""
    if article_limit < 1 or cluster_count < 1 or cluster_count > 128:
        raise ValueError("positive article_limit and 1..128 clusters required")
    if output.exists():
        raise ValueError("output must be a new release directory")
    if file_hash(cache) != DATASET_SHA256:
        raise ValueError("dataset checksum mismatch")
    articles, acquisition = select_articles(cache, article_limit)
    if not articles:
        raise ValueError("no eligible articles")
    print(f"Selected {len(articles)} articles from {acquisition['scanned']} rows", flush=True)
    labels, centers = cluster_articles(articles, cluster_count)
    output.mkdir(parents=True)
    cluster_info, all_shards = [], []
    for cluster_id, center in enumerate(centers):
        members = [article for article, label in zip(articles, labels) if label == cluster_id]
        if not members:
            continue
        prefix = f"cluster-{cluster_id:03d}"
        keywords = Counter(word for article in members for word in terms(article["title"]))
        cluster_info.append({"id": prefix, "articles": len(members), "document_centroid": center,
                             "title_terms": [word for word, _ in sorted(keywords.items(), key=lambda pair: (-pair[1], pair[0]))[:20]]})
        pending, byte_count, shard_index = [], 0, 0
        for article in members:
            record = canonical_json(article)
            if pending and byte_count + len(record) > SHARD_TEXT_LIMIT:
                all_shards.append(write_shard(output, prefix, shard_index, pending))
                shard_index += 1
                pending, byte_count = [], 0
            pending.append(record)
            byte_count += len(record)
        if pending:
            all_shards.append(write_shard(output, prefix, shard_index, pending))
    license_text = (
        "# Encyclopedia data attribution and license\n\n"
        "Text by Simple English Wikipedia contributors. Each source record retains its article URL and revision-history link.\n\n"
        "The pinned wikimedia/wikipedia dataset declares CC-BY-SA-3.0 and GFDL. This data directory is separate from the project's MIT-licensed code.\n\n"
        "https://creativecommons.org/licenses/by-sa/3.0/legalcode\n"
        "https://www.gnu.org/licenses/fdl-1.3.html\n"
        f"https://huggingface.co/datasets/wikimedia/wikipedia/tree/{DATASET_REVISION}\n\n"
        "Changes: Unicode/line-ending normalization, quality filtering, exact deduplication, deterministic sampling and clustering.\n"
        "Native models are derived experimental statistical artifacts, not verified encyclopedic answers.\n")
    write_bounded(output / "LICENSE.md", license_text.encode())
    manifest = {"schema_version": 1, "source": {"dataset": "wikimedia/wikipedia",
        "configuration": "20231101.simple", "revision": DATASET_REVISION, "path": DATASET_PATH,
        "sha256": DATASET_SHA256, "bytes": DATASET_BYTES, "url": DATASET_URL},
        "selection": {"algorithm": "lowest-sha256-of-simplewiki-page-id-v1", "article_limit": article_limit,
                      "min_words": 40, "max_article_bytes": 65536, **acquisition},
        "clustering": {"algorithm": "signed-hash-spherical-kmeans-v1", "dimensions": 128,
                       "iterations": 8, "clusters_requested": cluster_count},
        "limits": {"file_bytes": FILE_LIMIT, "release_bytes": RELEASE_LIMIT, "source_shard_bytes": SHARD_TEXT_LIMIT},
        "articles": len(articles), "clusters": cluster_info, "shards": all_shards,
        "license_sha256": file_hash(output / "LICENSE.md")}
    write_bounded(output / "sources.json", canonical_json(manifest))
    verify_sources(output)
    print(f"Wrote {len(all_shards)} bounded shards in {len(cluster_info)} document clusters", flush=True)
    return manifest


def write_shard(output, prefix, index, records):
    name = f"sources/{prefix}/shard-{index:04d}.jsonl.gz"
    raw = b"".join(records)
    packed = deterministic_gzip(raw)
    write_bounded(output / name, packed)
    return {"id": f"{prefix}-{index:04d}", "cluster": prefix, "path": name,
            "articles": len(records), "raw_bytes": len(raw), "bytes": len(packed),
            "sha256": hashlib.sha256(packed).hexdigest(), "content_sha256": hashlib.sha256(raw).hexdigest()}


def verify_sources(output):
    manifest = json.loads((output / "sources.json").read_text(encoding="utf-8"))
    paths = [path for path in output.rglob("*") if path.is_file()]
    if any(path.stat().st_size > FILE_LIMIT for path in paths):
        raise ValueError("release contains a file over the 1 MiB limit")
    if sum(path.stat().st_size for path in paths) > RELEASE_LIMIT:
        raise ValueError("release exceeds 128 MiB budget")
    if file_hash(output / "LICENSE.md") != manifest["license_sha256"]:
        raise ValueError("license checksum mismatch")
    total, ids = 0, set()
    for shard in manifest["shards"]:
        relative_path(shard["path"])
        path = output / shard["path"]
        if file_hash(path) != shard["sha256"]:
            raise ValueError(f"source checksum mismatch: {path}")
        with gzip.open(path, "rb") as stream:
            raw = stream.read(SHARD_TEXT_LIMIT + 1)
        if len(raw) > SHARD_TEXT_LIMIT:
            raise ValueError("decompressed source exceeds shard budget")
        if len(raw) != shard["raw_bytes"] or hashlib.sha256(raw).hexdigest() != shard["content_sha256"]:
            raise ValueError("source content checksum mismatch")
        lines = raw.splitlines()
        if len(lines) != shard["articles"]:
            raise ValueError("per-shard article count mismatch")
        for line in lines:
            article = json.loads(line)
            if article["id"] in ids or hashlib.sha256(article["text"].encode()).hexdigest() != article["sha256"]:
                raise ValueError("duplicate article ID or invalid article checksum")
            ids.add(article["id"])
            total += 1
    if total != manifest["articles"]:
        raise ValueError("article count mismatch")
    return {"articles": total, "files": len(paths), "bytes": sum(path.stat().st_size for path in paths),
            "largest_file_bytes": max(path.stat().st_size for path in paths)}


def monitor_upstream(output):
    """Check the public dataset's Git branch without downloading data or using keys."""
    manifest = json.loads((output / "sources.json").read_text(encoding="utf-8"))
    config = Config(name="wikimedia/wikipedia", paths=["20231101.simple"], license="CC-BY-SA-3.0",
                    remote_url="https://huggingface.co/datasets/wikimedia/wikipedia")
    current = remote_head(config)
    pinned = manifest["source"]["revision"]
    result = {"update_available": current != pinned, "pinned_revision": pinned,
              "upstream_revision": current, "mode": "remote-ref-only", "downloads_performed": False}
    print(json.dumps(result, indent=2))
    return 3 if result["update_available"] else 0


def restore_model(directory, shard_id, destination):
    """Restore one checksum-verified complete native artifact for existing model APIs."""
    if destination.exists():
        raise ValueError("model destination already exists")
    manifest = json.loads((directory / "models.json").read_text(encoding="utf-8"))
    shard = next((item for item in manifest["shards"] if item["id"] == shard_id), None)
    if shard is None:
        raise ValueError("unknown model shard")
    relative_path(shard["model"]["path"])
    path = directory / shard["model"]["path"]
    if file_hash(path) != shard["model"]["sha256"]:
        raise ValueError("compressed model checksum mismatch")
    with gzip.open(path, "rb") as stream:
        payload = stream.read(32 * 1024 * 1024 + 1)
    if len(payload) > 32 * 1024 * 1024 or len(payload) != shard["artifactBytes"]:
        raise ValueError("invalid decompressed model size")
    if hashlib.sha256(payload).hexdigest() != shard["artifactSha256"]:
        raise ValueError("native model checksum mismatch")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as stream:
        stream.write(payload)
    return {"shard": shard_id, "output": str(destination), "bytes": len(payload)}


def file_hash(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def download(destination: Path):
    """Download one pinned public artifact anonymously and verify its upstream hash."""
    if destination.exists():
        if destination.stat().st_size == DATASET_BYTES and file_hash(destination) == DATASET_SHA256:
            return destination
        raise ValueError("existing dataset cache does not match pinned upstream checksum")
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    request = urllib.request.Request(DATASET_URL, headers={"User-Agent": "Centroid-GAI-KnowledgeBuilder/1.0"})
    total = 0
    with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as output:
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > DATASET_BYTES:
                raise ValueError("download exceeds pinned artifact size")
            output.write(chunk)
            if total % (16 * 1024 * 1024) == 0:
                print(f"Downloaded {total // (1024 * 1024)} MiB", flush=True)
    if total != DATASET_BYTES or file_hash(partial) != DATASET_SHA256:
        raise ValueError("download checksum/size mismatch")
    partial.rename(destination)
    print(f"Verified {total} bytes: {DATASET_SHA256}", flush=True)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["download", "build", "verify", "monitor", "restore"])
    parser.add_argument("--cache", type=Path, default=Path("build/knowledge/downloads/simplewiki.parquet"))
    parser.add_argument("--output", type=Path, default=Path("knowledge/encyclopedia/simplewiki-v1"))
    parser.add_argument("--articles", type=int, default=20000)
    parser.add_argument("--clusters", type=int, default=32)
    parser.add_argument("--shard", help="model shard ID for restore")
    parser.add_argument("--model-output", type=Path, help="new .cgai file for restore")
    args = parser.parse_args()
    if args.command == "download":
        download(args.cache)
    elif args.command == "build":
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with acquire(args.output.with_name(args.output.name + ".lock")):
            build(args.cache, args.output, args.articles, args.clusters)
    elif args.command == "verify":
        print(json.dumps(verify_sources(args.output), indent=2))
    elif args.command == "monitor":
        return monitor_upstream(args.output)
    else:
        if not args.shard or args.model_output is None:
            parser.error("restore requires --shard and --model-output")
        print(json.dumps(restore_model(args.output, args.shard, args.model_output), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
