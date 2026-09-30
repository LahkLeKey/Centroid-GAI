"""Configuration for a versioned encyclopedia, independent of model runtime."""

from dataclasses import asdict, dataclass, field
import hashlib
import json
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit

PROSE_EXTENSIONS = {".txt", ".md", ".rst"}
CODE_EXTENSIONS = {".c", ".h", ".ts", ".tsx", ".js", ".py", ".json", ".sql", ".prisma", ".css"}
CODE_EXTENSIONS |= {".yaml", ".yml"}
EXACT_FILENAMES = {"Dockerfile", "CMakeLists.txt", "binding.gyp", ".env.example", "compose.env.example"}
IGNORED_DIRECTORIES = {"node_modules", "build", "out", "__pycache__", ".git", ".venv", ".ssh", ".agents", ".codex"}


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
    filenames: list[str] = field(default_factory=list)
    exclude_paths: list[str] = field(default_factory=list)
    excluded_filenames: list[str] = field(default_factory=list)
    required_paths: list[str] = field(default_factory=list)
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
        for name in ("filenames", "exclude_paths", "excluded_filenames", "required_paths"):
            values = getattr(self, name)
            if not isinstance(values, list):
                raise ValueError(f"{name} must be a list of literal paths")
            for value in values:
                relative_path(value)
        if any(PurePosixPath(path).name not in EXACT_FILENAMES for path in self.filenames):
            raise ValueError("unsupported exact source filename")
        if any("/" in name for name in self.excluded_filenames):
            raise ValueError("excluded_filenames must contain exact basenames")
        if self.references_path is not None:
            relative_path(self.references_path)
        if not self.extensions or any(ext not in PROSE_EXTENSIONS | CODE_EXTENSIONS for ext in self.extensions):
            raise ValueError("unsupported text/source extension")
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
        return self.exclusion_reason(path) is None

    def exclusion_reason(self, path: str) -> str | None:
        """Admit operational files only through explicit roots and opt-in formats."""
        relative_path(path)
        parts = PurePosixPath(path).parts
        if any(part in IGNORED_DIRECTORIES for part in parts[:-1]):
            return "generated-or-private-directory"
        if parts[-1] == ".env" or (parts[-1].startswith(".env.") and path not in self.filenames):
            return "environment-secret-file"
        if parts[-1] in self.excluded_filenames:
            return "excluded-filename"
        if any(path == prefix or path.startswith(prefix + "/") for prefix in self.exclude_paths):
            return "excluded-path"
        if path in {self.license_path, self.references_path}:
            return "metadata-only"
        if not any(path == prefix or path.startswith(prefix + "/") for prefix in self.paths):
            return "outside-configured-paths"
        for index, part in enumerate(parts):
            prefix = "/".join(parts[:index + 1])
            if part.startswith(".") and path not in self.filenames and not any(root == prefix or root.startswith(prefix + "/") for root in self.paths):
                return "hidden-path-not-explicitly-admitted"
        if path not in self.filenames and PurePosixPath(path).suffix.lower() not in self.extensions:
            return "unsupported-extension"
        return None

    def is_code(self, path: str) -> bool:
        return path in self.filenames or PurePosixPath(path).suffix.lower() in CODE_EXTENSIONS

    def fingerprint(self) -> str:
        return hashlib.sha256(json.dumps(asdict(self), sort_keys=True).encode()).hexdigest()

    @classmethod
    def read(cls, path: Path) -> "Config":
        return cls(**json.loads(path.read_text(encoding="utf-8")))
