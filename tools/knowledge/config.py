"""Configuration for a versioned encyclopedia, independent of model runtime."""

from dataclasses import asdict, dataclass, field
import hashlib
import json
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit


def relative_path(value: str) -> str:
    if (not isinstance(value, str) or not value or value.startswith(("/", "-"))
            or "\\" in value or ":" in value or any(ord(c) < 32 for c in value)
            or any(part in {"", ".", ".."} for part in value.split("/"))):
        raise ValueError("content paths must be literal relative Git paths")
    return value


@dataclass(frozen=True)
class Config:
    """Source identity, inclusion policy, and corpus resource budgets."""

    name: str
    paths: list[str]
    license: str
    license_path: str = "LICENSE"
    remote_url: str | None = None
    branch: str = "main"
    extensions: list[str] = field(default_factory=lambda: [".txt", ".md", ".rst"])
    references_path: str | None = None
    min_words: int = 20
    max_files: int = 10000
    max_file_bytes: int = 2_000_000
    max_total_bytes: int = 100_000_000
    validation_percent: int = 10

    def __post_init__(self):
        if not isinstance(self.name, str) or not self.name.strip() or not isinstance(self.license, str) or not self.license.strip():
            raise ValueError("name and license are required")
        if not isinstance(self.paths, list) or not self.paths:
            raise ValueError("paths must contain at least one content file or directory")
        for path in self.paths + [self.license_path]:
            relative_path(path)
        if self.references_path is not None:
            relative_path(self.references_path)
        if not self.extensions or any(ext not in {".txt", ".md", ".rst"} for ext in self.extensions):
            raise ValueError("supported extensions are .txt, .md, and .rst")
        for name in ("min_words", "max_files", "max_file_bytes", "max_total_bytes"):
            if type(getattr(self, name)) is not int or getattr(self, name) <= 0:
                raise ValueError(f"{name} must be a positive integer")
        if type(self.validation_percent) is not int or not 0 <= self.validation_percent <= 50:
            raise ValueError("validation_percent must be between 0 and 50")
        if (not isinstance(self.branch, str) or not self.branch or self.branch.startswith("-")
                or any(ord(c) < 33 for c in self.branch)):
            raise ValueError("invalid branch")
        if self.remote_url is not None:
            parts = urlsplit(self.remote_url)
            if (parts.scheme != "https" or not parts.hostname or parts.username is not None
                    or parts.password is not None or parts.query or parts.fragment
                    or parts.port not in (None, 443) or any(ord(c) < 33 for c in self.remote_url)):
                raise ValueError("remote_url must be public HTTPS without credentials, query, or fragment")

    def includes(self, path: str) -> bool:
        return (any(path == prefix or path.startswith(prefix + "/") for prefix in self.paths)
                and PurePosixPath(path).suffix.lower() in self.extensions
                and path not in {self.license_path, self.references_path})

    def fingerprint(self) -> str:
        return hashlib.sha256(json.dumps(asdict(self), sort_keys=True).encode()).hexdigest()

    @classmethod
    def read(cls, path: Path) -> "Config":
        return cls(**json.loads(path.read_text(encoding="utf-8")))
