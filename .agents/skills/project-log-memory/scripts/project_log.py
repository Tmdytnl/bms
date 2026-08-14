#!/usr/bin/env python3
"""Safely initialize, validate, hash, and commit a project memory log.

This module intentionally uses only the Python 3.9+ standard library.

Stable process exit codes:
    0  success
    2  command-line or unsafe-path error
    3  invalid log/candidate schema (or strict limit failure)
    4  SHA-256 baseline conflict
    5  lock acquisition timeout
    6  refused non-overwriting write
    7  filesystem or encoding I/O error
"""

from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
import re
import stat
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


EXIT_OK = 0
EXIT_USAGE = 2
EXIT_INVALID = 3
EXIT_CONFLICT = 4
EXIT_LOCK_TIMEOUT = 5
EXIT_REFUSED = 6
EXIT_IO = 7

MAX_ACTIVE_LOG_BYTES = 24 * 1024
MAX_RECENT_TASKS = 20
REPARSE_POINT = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)

DEFAULT_LOG = Path(".project-memory") / "PROJECT_LOG.md"
DEFAULT_TEMPLATE = (
    Path(__file__).resolve().parent.parent / "assets" / "PROJECT_LOG.md"
)

EXPECTED_HEADINGS: Tuple[Tuple[int, str], ...] = (
    (1, "Project Log"),
    (2, "Current Snapshot"),
    (2, "Current Goal and Scope"),
    (2, "Confirmed Facts and Decisions"),
    (2, "Active Artifacts"),
    (2, "Invalidated Artifact Tombstones"),
    (2, "Open Work and Blockers"),
    (2, "Recent Task History"),
)

CONFIRMED_COLUMNS: Tuple[str, ...] = (
    "ID",
    "Fact or decision",
    "Evidence",
    "Revision",
    "Updated",
)
ACTIVE_COLUMNS: Tuple[str, ...] = (
    "ID",
    "Path",
    "Revision",
    "Status",
    "Gate",
    "Purpose",
    "Evidence",
    "Reuse guidance",
    "Updated",
)
TOMBSTONE_COLUMNS: Tuple[str, ...] = (
    "ID",
    "Path",
    "Revision",
    "Status",
    "Gate",
    "Reason",
    "Replacement",
    "Evidence",
    "Updated",
)

STATUS_GATE: Dict[str, str] = {
    "VALIDATED": "ALLOW",
    "USABLE": "RECHECK",
    "EXPERIMENTAL": "BLOCK",
    "REJECTED": "BLOCK",
    "SUPERSEDED": "BLOCK",
    "UNKNOWN": "BLOCK",
}
ACTIVE_STATUSES = frozenset(("VALIDATED", "USABLE", "EXPERIMENTAL", "UNKNOWN"))
TOMBSTONE_STATUSES = frozenset(("REJECTED", "SUPERSEDED"))

HISTORY_FIELDS: Tuple[str, ...] = (
    "Request",
    "Outcome",
    "Artifacts",
    "Validation",
    "Decisions",
    "Invalidated",
    "Remaining",
    "Next",
)

_HEADING_RE = re.compile(r"^(#{1,6})[ \t]+(.+?)[ \t]*$")
_FENCE_RE = re.compile(r"^[ \t]*(`{3,}|~{3,})")
_SEPARATOR_RE = re.compile(r"^:?-{3,}:?$")
_SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
_ARTIFACT_ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
_ARTIFACT_REVISION_RE = re.compile(
    r"^(?:"
    r"sha256:[0-9a-f]{64}"
    r"|git:(?:[0-9a-f]{40}|[0-9a-f]{64})"
    r"|external:[a-z0-9][a-z0-9._-]*:[A-Za-z0-9][A-Za-z0-9._~:@/+%=-]*"
    r")$"
)
_UTC_TIMESTAMP_RE = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$"
)
_HISTORY_HEADING_RE = re.compile(
    r"^### "
    r"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)"
    r" \| ([^|]+) \| (.+)$"
)
_PLAIN_HISTORY_FIELD_RE = re.compile(
    r"^-\s+(" + "|".join(HISTORY_FIELDS) + r"):\s*(.*)$"
)
_BOLD_HISTORY_FIELD_RE = re.compile(
    r"^-\s+\*\*(" + "|".join(HISTORY_FIELDS) + r"):\*\*\s*(.*)$"
)

SNAPSHOT_FIELDS: Tuple[str, ...] = (
    "Project",
    "Phase",
    "State",
    "Last updated",
)
GOAL_FIELDS: Tuple[str, ...] = (
    "Goal",
    "In scope",
    "Out of scope",
)
_NONE_OPEN_WORK = frozenset(
    ("none", "none recorded", "no open work", "no blockers", "nothing open")
)


class ToolError(Exception):
    """An expected CLI failure with a stable exit code and diagnostic code."""

    def __init__(self, exit_code: int, diagnostic: str, message: str) -> None:
        super().__init__(message)
        self.exit_code = exit_code
        self.diagnostic = diagnostic
        self.message = message


@dataclass(frozen=True)
class Issue:
    code: str
    message: str
    line: Optional[int] = None

    def as_dict(self) -> Dict[str, object]:
        result: Dict[str, object] = {"code": self.code, "message": self.message}
        if self.line is not None:
            result["line"] = self.line
        return result


@dataclass
class ValidationReport:
    byte_count: int
    history_entries: int = 0
    errors: List[Issue] = field(default_factory=list)
    warnings: List[Issue] = field(default_factory=list)

    @property
    def schema_valid(self) -> bool:
        return not self.errors

    def valid(self, strict_limits: bool = False) -> bool:
        return self.schema_valid and not (strict_limits and self.warnings)


@dataclass(frozen=True)
class Heading:
    level: int
    title: str
    index: int

    @property
    def line(self) -> int:
        return self.index + 1


@dataclass(frozen=True)
class ArtifactRecord:
    line: int
    artifact_id: str
    path: str
    revision: str
    status: str
    gate: str
    replacement: Optional[str]

    @property
    def reference(self) -> str:
        return f"{self.artifact_id}@{self.revision}"


@dataclass(frozen=True)
class ArchiveEntry:
    timestamp: str
    task_id: str
    text: str
    raw: bytes


@dataclass(frozen=True)
class ArchiveUpdate:
    path: Path
    original: Optional[bytes]
    content: bytes


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _is_link_or_reparse(info: os.stat_result) -> bool:
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0) & REPARSE_POINT
    )


def _inspect_component_chain(
    path: Path,
    label: str,
    require_all: bool,
) -> Optional[os.stat_result]:
    """lstat every existing lexical component and reject links/reparse points."""

    absolute = Path(os.path.abspath(os.fspath(path)))
    parts = absolute.parts
    if not parts:
        raise ToolError(EXIT_USAGE, "UNSAFE_PATH", f"cannot inspect empty {label} path")

    current = Path(parts[0])
    final_info: Optional[os.stat_result] = None
    missing_seen = False
    for index, part in enumerate(parts):
        if index:
            current = current / part
        is_final = index == len(parts) - 1
        try:
            info = os.lstat(os.fspath(current))
        except FileNotFoundError:
            missing_seen = True
            if require_all:
                raise ToolError(
                    EXIT_IO,
                    "FILE_NOT_FOUND",
                    f"{label} path component does not exist: {current}",
                )
            continue
        except OSError as exc:
            raise ToolError(
                EXIT_USAGE,
                "UNSAFE_PATH",
                f"cannot lstat {label} path component {current}: {exc}",
            ) from exc

        if missing_seen:
            raise ToolError(
                EXIT_USAGE,
                "UNSAFE_PATH",
                f"{label} has an existing child below a missing component: {current}",
            )
        if _is_link_or_reparse(info):
            raise ToolError(
                EXIT_USAGE,
                "UNSAFE_REPARSE_POINT",
                f"{label} may not traverse a symlink or reparse point: {current}",
            )
        if not is_final and not stat.S_ISDIR(info.st_mode):
            raise ToolError(
                EXIT_USAGE,
                "UNSAFE_PATH_COMPONENT",
                f"{label} path component is not a directory: {current}",
            )
        if is_final:
            final_info = info
    return final_info


def _require_regular_file(path: Path, label: str) -> os.stat_result:
    info = _inspect_component_chain(path, label, require_all=True)
    if info is None or not stat.S_ISREG(info.st_mode):
        raise ToolError(EXIT_IO, "NOT_A_REGULAR_FILE", f"{label} is not a regular file: {path}")
    return info


def _stat_signature(info: os.stat_result) -> Tuple[int, int, int, int, int]:
    return (
        int(info.st_dev),
        int(info.st_ino),
        int(info.st_size),
        int(getattr(info, "st_mtime_ns", int(info.st_mtime * 1_000_000_000))),
        int(getattr(info, "st_ctime_ns", int(info.st_ctime * 1_000_000_000))),
    )


def _read_bytes(path: Path, label: str) -> bytes:
    before = _require_regular_file(path, label)
    try:
        with path.open("rb") as handle:
            data = handle.read()
    except FileNotFoundError as exc:
        raise ToolError(EXIT_IO, "FILE_NOT_FOUND", f"{label} does not exist: {path}") from exc
    except OSError as exc:
        raise ToolError(EXIT_IO, "READ_FAILED", f"cannot read {label} {path}: {exc}") from exc
    after = _require_regular_file(path, label)
    if _stat_signature(before) != _stat_signature(after):
        raise ToolError(
            EXIT_IO,
            "FILE_CHANGED_DURING_READ",
            f"{label} changed while it was being read: {path}",
        )
    return data


def _resolve_root(value: str) -> Path:
    root = Path(os.path.abspath(os.path.expanduser(value)))
    try:
        info = _inspect_component_chain(root, "project root", require_all=True)
    except ToolError as exc:
        raise ToolError(
            EXIT_USAGE,
            "INVALID_ROOT",
            f"project root is unavailable or unsafe: {root}: {exc.message}",
        ) from exc
    if info is None or not stat.S_ISDIR(info.st_mode):
        raise ToolError(EXIT_USAGE, "INVALID_ROOT", f"project root is not a directory: {root}")
    return root


def _is_within(path: Path, root: Path) -> bool:
    try:
        common = os.path.commonpath((os.fspath(path), os.fspath(root)))
    except ValueError:
        return False
    return os.path.normcase(common) == os.path.normcase(os.fspath(root))


def _lexists(path: Path) -> bool:
    return os.path.lexists(os.fspath(path))


def _same_path(first: Path, second: Path) -> bool:
    if _lexists(first) and _lexists(second):
        try:
            if os.path.samefile(os.fspath(first), os.fspath(second)):
                return True
        except OSError:
            pass
    return os.path.normcase(os.path.abspath(os.fspath(first))) == os.path.normcase(
        os.path.abspath(os.fspath(second))
    )


def _safe_project_path(root: Path, value: str, label: str) -> Path:
    supplied = Path(value).expanduser()
    combined = supplied if supplied.is_absolute() else root / supplied
    lexical = Path(os.path.abspath(os.fspath(combined)))
    if not _is_within(lexical, root):
        raise ToolError(
            EXIT_USAGE,
            "PATH_OUTSIDE_ROOT",
            f"{label} must stay inside project root {root}: {lexical}",
        )

    _inspect_component_chain(lexical, label, require_all=False)
    return lexical


def _resolve_input_path(root: Path, value: str, label: str) -> Path:
    supplied = Path(value).expanduser()
    combined = supplied if supplied.is_absolute() else root / supplied
    lexical = Path(os.path.abspath(os.fspath(combined)))
    _require_regular_file(lexical, label)
    return lexical


def _ensure_directory_chain(root: Path, directory: Path) -> None:
    if not _is_within(directory, root):
        raise ToolError(
            EXIT_USAGE,
            "PATH_OUTSIDE_ROOT",
            f"directory must stay inside project root {root}: {directory}",
        )
    relative = Path(os.path.relpath(os.fspath(directory), os.fspath(root)))
    cursor = root
    for part in relative.parts:
        if part in ("", "."):
            continue
        cursor = cursor / part
        try:
            info = os.lstat(os.fspath(cursor))
        except FileNotFoundError:
            info = None
        except OSError as exc:
            raise ToolError(
                EXIT_USAGE,
                "UNSAFE_PATH",
                f"cannot lstat directory path component {cursor}: {exc}",
            ) from exc
        if info is not None:
            if _is_link_or_reparse(info):
                raise ToolError(
                    EXIT_USAGE,
                    "UNSAFE_REPARSE_POINT",
                    f"refusing to create through symlink or reparse point: {cursor}",
                )
            if not stat.S_ISDIR(info.st_mode):
                raise ToolError(EXIT_IO, "NOT_A_DIRECTORY", f"path component is not a directory: {cursor}")
            continue
        try:
            cursor.mkdir()
        except FileExistsError:
            appeared = _inspect_component_chain(cursor, "created directory", require_all=True)
            if appeared is None or not stat.S_ISDIR(appeared.st_mode):
                raise ToolError(
                    EXIT_USAGE,
                    "UNSAFE_DIRECTORY_RACE",
                    f"unsafe path appeared while creating directory: {cursor}",
                )
        except OSError as exc:
            raise ToolError(EXIT_IO, "MKDIR_FAILED", f"cannot create directory {cursor}: {exc}") from exc


def _strip_html_comments(line: str, in_comment: bool) -> Tuple[str, bool]:
    visible: List[str] = []
    rest = line
    while True:
        if in_comment:
            end = rest.find("-->")
            if end < 0:
                return "".join(visible), True
            rest = rest[end + 3 :]
            in_comment = False
        start = rest.find("<!--")
        if start < 0:
            visible.append(rest)
            return "".join(visible), False
        visible.append(rest[:start])
        rest = rest[start + 4 :]
        in_comment = True


def _scan_headings(lines: Sequence[str]) -> Tuple[List[Heading], bool, bool]:
    headings: List[Heading] = []
    in_comment = False
    fence_char: Optional[str] = None
    fence_length = 0

    for index, raw_line in enumerate(lines):
        line, in_comment = _strip_html_comments(raw_line, in_comment)
        stripped = line.strip()
        fence = _FENCE_RE.match(line)
        if fence_char is not None:
            if fence:
                marker = fence.group(1)
                if marker[0] == fence_char and len(marker) >= fence_length:
                    fence_char = None
                    fence_length = 0
            continue
        if fence:
            marker = fence.group(1)
            fence_char = marker[0]
            fence_length = len(marker)
            continue
        match = _HEADING_RE.match(stripped)
        if match:
            headings.append(Heading(len(match.group(1)), match.group(2), index))

    return headings, in_comment, fence_char is not None


def _semantic_lines(lines: Iterable[Tuple[int, str]]) -> List[Tuple[int, str]]:
    result: List[Tuple[int, str]] = []
    in_comment = False
    for number, raw_line in lines:
        line, in_comment = _strip_html_comments(raw_line, in_comment)
        if line.strip():
            result.append((number, line.strip()))
    return result


def _split_table_row(line: str) -> Optional[List[str]]:
    value = line.strip()
    if "|" not in value:
        return None
    if value.startswith("|"):
        value = value[1:]

    trailing_pipe = value.endswith("|")
    if trailing_pipe:
        slash_count = 0
        for char in reversed(value[:-1]):
            if char != "\\":
                break
            slash_count += 1
        if slash_count % 2 == 0:
            value = value[:-1]

    cells: List[str] = []
    current: List[str] = []
    index = 0
    while index < len(value):
        char = value[index]
        if char == "\\" and index + 1 < len(value) and value[index + 1] == "|":
            current.append("|")
            index += 2
            continue
        if char == "|":
            cells.append("".join(current).strip())
            current = []
        else:
            current.append(char)
        index += 1
    cells.append("".join(current).strip())
    return cells


def _plain_cell(value: str) -> str:
    result = value.strip()
    while len(result) >= 2 and result.startswith("`") and result.endswith("`"):
        result = result[1:-1].strip()
    return result


def _is_placeholder(value: str) -> bool:
    plain = _plain_cell(value)
    lowered = plain.casefold().rstrip(".")
    if not plain:
        return True
    if lowered in {
        "-",
        "—",
        "none",
        "n/a",
        "na",
        "unknown",
        "pending",
        "tbd",
        "todo",
        "placeholder",
    }:
        return True
    return (
        (plain.startswith("<") and plain.endswith(">"))
        or (plain.startswith("{{") and plain.endswith("}}"))
    )


def _parse_table(
    section_name: str,
    section_lines: Sequence[Tuple[int, str]],
    expected_columns: Sequence[str],
    report: ValidationReport,
) -> List[Tuple[int, Dict[str, str]]]:
    visible = _semantic_lines(section_lines)
    if len(visible) < 2:
        report.errors.append(
            Issue(
                "PL020",
                f"{section_name} must contain its table header and separator, even when empty",
            )
        )
        return []

    header_line, header_text = visible[0]
    header = _split_table_row(header_text)
    if header != list(expected_columns):
        report.errors.append(
            Issue(
                "PL021",
                f"{section_name} columns must be exactly: {' | '.join(expected_columns)}",
                header_line,
            )
        )
        return []

    separator_line, separator_text = visible[1]
    separator = _split_table_row(separator_text)
    if (
        separator is None
        or len(separator) != len(expected_columns)
        or not all(_SEPARATOR_RE.fullmatch(cell.strip()) for cell in separator)
    ):
        report.errors.append(
            Issue(
                "PL022",
                f"{section_name} must use a valid Markdown separator row",
                separator_line,
            )
        )
        return []

    rows: List[Tuple[int, Dict[str, str]]] = []
    for line_number, text in visible[2:]:
        cells = _split_table_row(text)
        if cells is None or len(cells) != len(expected_columns):
            report.errors.append(
                Issue(
                    "PL023",
                    f"{section_name} row must contain {len(expected_columns)} cells",
                    line_number,
                )
            )
            continue
        row = dict(zip(expected_columns, cells))
        empty_columns = [column for column, value in row.items() if not value.strip()]
        if empty_columns:
            report.errors.append(
                Issue(
                    "PL024",
                    f"{section_name} row has empty cells: {', '.join(empty_columns)}; use an explicit value such as None",
                    line_number,
                )
            )
        rows.append((line_number, row))
    return rows


def _validate_unique_id(
    value: str,
    line: int,
    seen_ids: Dict[str, int],
    report: ValidationReport,
) -> None:
    plain = _plain_cell(value)
    if _is_placeholder(plain):
        report.errors.append(Issue("PL025", "table row ID must be a non-placeholder value", line))
        return
    if not _ARTIFACT_ID_RE.fullmatch(plain):
        report.errors.append(
            Issue(
                "PL026",
                "table row ID must use lowercase letters, digits, dots, underscores, or hyphens",
                line,
            )
        )
    key = plain.casefold()
    if key in seen_ids:
        report.errors.append(
            Issue(
                "PL027",
                f"duplicate table row ID {plain!r}; first used on line {seen_ids[key]}",
                line,
            )
        )
    else:
        seen_ids[key] = line


def _is_utc_timestamp(value: str) -> bool:
    if not _UTC_TIMESTAMP_RE.fullmatch(value):
        return False
    try:
        datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        return False
    return True


def _validate_updated(
    value: str,
    line: int,
    context: str,
    report: ValidationReport,
) -> None:
    plain = _plain_cell(value)
    if not _is_utc_timestamp(plain):
        report.errors.append(
            Issue(
                "PL028",
                f"{context} Updated must be UTC YYYY-MM-DDTHH:MM:SSZ",
                line,
            )
        )


def _is_repo_relative_posix_path(value: str) -> bool:
    if not value or value == "." or _is_placeholder(value):
        return False
    if "\\" in value or value.startswith("/") or re.match(r"^[A-Za-z]:", value):
        return False
    if value.startswith("//") or value.endswith("/"):
        return False
    parts = value.split("/")
    return all(part not in ("", ".", "..") for part in parts)


def _validate_artifact_rows(
    rows: Sequence[Tuple[int, Dict[str, str]]],
    allowed_statuses: frozenset,
    section_name: str,
    seen_ids: Dict[str, int],
    report: ValidationReport,
) -> List[ArtifactRecord]:
    records: List[ArtifactRecord] = []
    for line, row in rows:
        _validate_unique_id(row["ID"], line, seen_ids, report)
        artifact_id = _plain_cell(row["ID"])
        path_value = _plain_cell(row["Path"])
        revision_value = _plain_cell(row["Revision"])
        if not _is_repo_relative_posix_path(path_value):
            report.errors.append(
                Issue(
                    "PL029",
                    f"{section_name} Path must be a repository-relative POSIX path without '.', '..', drives, or backslashes",
                    line,
                )
            )
        if not _ARTIFACT_REVISION_RE.fullmatch(revision_value):
            report.errors.append(
                Issue(
                    "PL030",
                    "Revision must be sha256:<64 lowercase hex>, git:<40 or 64 lowercase hex>, "
                    "or external:<provider>:<immutable-id>",
                    line,
                )
            )

        status_value = _plain_cell(row["Status"])
        gate_value = _plain_cell(row["Gate"])
        if status_value not in STATUS_GATE:
            report.errors.append(
                Issue(
                    "PL031",
                    f"unsupported Status {status_value!r}; use one of {', '.join(STATUS_GATE)}",
                    line,
                )
            )
            continue
        if status_value not in allowed_statuses:
            report.errors.append(
                Issue(
                    "PL032",
                    f"Status {status_value} is not allowed in {section_name}",
                    line,
                )
            )
        expected_gate = STATUS_GATE[status_value]
        if gate_value != expected_gate:
            report.errors.append(
                Issue(
                    "PL033",
                    f"Status {status_value} requires Gate {expected_gate}, not {gate_value!r}",
                    line,
                )
            )

        if _is_placeholder(row["Evidence"]):
            report.errors.append(
                Issue(
                    "PL034",
                    f"{status_value} requires concrete Evidence for this immutable revision",
                    line,
                )
            )

        if section_name == "Invalidated Artifact Tombstones" and _is_placeholder(row["Reason"]):
            report.errors.append(
                Issue("PL035", "an invalidated artifact requires a concrete Reason", line)
            )
        _validate_updated(row["Updated"], line, section_name, report)
        replacement: Optional[str] = None
        if "Replacement" in row and not _is_placeholder(row["Replacement"]):
            replacement = _plain_cell(row["Replacement"])
        if status_value == "SUPERSEDED" and replacement is None:
            report.errors.append(
                Issue(
                    "PL036",
                    "SUPERSEDED requires Replacement in artifact-id@revision form",
                    line,
                )
            )
        records.append(
            ArtifactRecord(
                line=line,
                artifact_id=artifact_id,
                path=path_value,
                revision=revision_value,
                status=status_value,
                gate=gate_value,
                replacement=replacement,
            )
        )
    return records


def _validate_artifact_relationships(
    records: Sequence[ArtifactRecord],
    report: ValidationReport,
) -> None:
    identities: Dict[Tuple[str, str], int] = {}
    references: Dict[str, ArtifactRecord] = {}
    for record in records:
        identity = (record.path, record.revision)
        if identity in identities:
            report.errors.append(
                Issue(
                    "PL037",
                    f"duplicate artifact identity ({record.path!r}, {record.revision!r}); "
                    f"first used on line {identities[identity]}",
                    record.line,
                )
            )
        else:
            identities[identity] = record.line
        if record.reference in references:
            report.errors.append(
                Issue(
                    "PL038",
                    f"duplicate artifact reference {record.reference!r}",
                    record.line,
                )
            )
        else:
            references[record.reference] = record

    for record in records:
        if record.replacement is None:
            continue
        replacement = references.get(record.replacement)
        if replacement is None:
            report.errors.append(
                Issue(
                    "PL039",
                    f"Replacement {record.replacement!r} does not identify an existing artifact row",
                    record.line,
                )
            )
            continue
        if replacement.reference == record.reference:
            report.errors.append(
                Issue("PL050", "an artifact may not replace itself", record.line)
            )
        expected_gate = STATUS_GATE.get(replacement.status)
        if expected_gate is None or replacement.gate != expected_gate:
            report.errors.append(
                Issue(
                    "PL051",
                    f"Replacement {replacement.reference!r} has an invalid status/gate pair",
                    record.line,
                )
            )


def _parse_fixed_fields(
    section_name: str,
    section_lines: Sequence[Tuple[int, str]],
    expected_fields: Sequence[str],
    report: ValidationReport,
) -> Dict[str, str]:
    visible = _semantic_lines(section_lines)
    found_names: List[str] = []
    values: Dict[str, str] = {}
    field_pattern = re.compile(
        r"^-\s+(" + "|".join(re.escape(name) for name in expected_fields) + r"):\s*(.*)$"
    )
    for line, text in visible:
        match = field_pattern.fullmatch(text)
        if not match:
            report.errors.append(
                Issue(
                    "PL052",
                    f"{section_name} must contain only these fields: {', '.join(expected_fields)}",
                    line,
                )
            )
            continue
        name, value = match.group(1), match.group(2).strip()
        found_names.append(name)
        if name in values:
            report.errors.append(
                Issue("PL053", f"{section_name} field {name!r} appears more than once", line)
            )
        else:
            values[name] = value
        if not value:
            report.errors.append(
                Issue("PL054", f"{section_name} field {name!r} must not be empty", line)
            )
    if found_names != list(expected_fields):
        report.errors.append(
            Issue(
                "PL055",
                f"{section_name} fields must appear exactly once in this order: "
                + ", ".join(expected_fields),
            )
        )
    return values


def _validate_open_work(
    section_lines: Sequence[Tuple[int, str]],
    report: ValidationReport,
) -> bool:
    visible = _semantic_lines(section_lines)
    if not visible:
        report.errors.append(
            Issue(
                "PL056",
                "Open Work and Blockers must explicitly record None or contain a non-empty list",
            )
        )
        return False
    values: List[Tuple[int, str]] = []
    for line, text in visible:
        match = re.fullmatch(r"-\s+(.+)", text)
        if not match or not match.group(1).strip():
            report.errors.append(
                Issue(
                    "PL057",
                    "Open Work and Blockers entries must be non-empty '- ...' list items",
                    line,
                )
            )
            continue
        values.append((line, match.group(1).strip()))
    none_entries = [
        (line, value)
        for line, value in values
        if value.casefold().rstrip(".") in _NONE_OPEN_WORK
    ]
    if none_entries and len(values) != 1:
        report.errors.append(
            Issue(
                "PL058",
                "an explicit None open-work entry may not be combined with other entries",
                none_entries[0][0],
            )
        )
    return len(visible) == 1 and visible[0][1] == "- None recorded."


def _history_field(line: str) -> Optional[Tuple[str, str]]:
    match = _PLAIN_HISTORY_FIELD_RE.match(line)
    if not match:
        match = _BOLD_HISTORY_FIELD_RE.match(line)
    if not match:
        return None
    return match.group(1), match.group(2).strip()


def _validate_history(
    section_lines: Sequence[Tuple[int, str]],
    report: ValidationReport,
) -> None:
    visible = _semantic_lines(section_lines)
    entry_starts = [
        index for index, (_line, text) in enumerate(visible) if text.startswith("### ")
    ]
    report.history_entries = len(entry_starts)
    if not visible:
        return
    if not entry_starts:
        report.errors.append(
            Issue(
                "PL040",
                "Recent Task History may be empty or contain only fixed-format task entries",
                visible[0][0],
            )
        )
        return
    if entry_starts[0] != 0:
        report.errors.append(
            Issue(
                "PL041",
                "content before the first Recent Task History entry is not allowed",
                visible[0][0],
            )
        )

    seen_task_ids: Dict[str, int] = {}
    ordered_timestamps: List[Tuple[datetime, int, str]] = []
    boundaries = entry_starts + [len(visible)]
    for position, start in enumerate(entry_starts):
        end = boundaries[position + 1]
        heading_line, heading_text = visible[start]
        heading_match = _HISTORY_HEADING_RE.fullmatch(heading_text)
        if not heading_match:
            report.errors.append(
                Issue(
                    "PL042",
                    "history heading must be: ### YYYY-MM-DDTHH:MM:SSZ | <task-id> | <title>",
                    heading_line,
                )
            )
        else:
            timestamp, task_id, title = (
                heading_match.group(1),
                heading_match.group(2).strip(),
                heading_match.group(3).strip(),
            )
            try:
                parsed_timestamp = datetime.strptime(timestamp, "%Y-%m-%dT%H:%M:%SZ")
            except ValueError:
                report.errors.append(
                    Issue("PL043", f"invalid UTC history timestamp {timestamp!r}", heading_line)
                )
            else:
                ordered_timestamps.append((parsed_timestamp, heading_line, timestamp))
            if _is_placeholder(task_id):
                report.errors.append(
                    Issue("PL044", "history task-id must be non-placeholder", heading_line)
                )
            else:
                key = task_id.casefold()
                if key in seen_task_ids:
                    report.errors.append(
                        Issue(
                            "PL045",
                            f"duplicate history task-id {task_id!r}; first used on line {seen_task_ids[key]}",
                            heading_line,
                        )
                    )
                else:
                    seen_task_ids[key] = heading_line
            if _is_placeholder(title):
                report.errors.append(
                    Issue("PL046", "history title must be non-placeholder", heading_line)
                )

        body = visible[start + 1 : end]
        found_fields: List[str] = []
        empty_fields: List[Tuple[str, int]] = []
        current_field: Optional[str] = None
        for line_number, text in body:
            parsed = _history_field(text)
            if parsed is not None:
                field_name, value = parsed
                found_fields.append(field_name)
                current_field = field_name
                if not value:
                    empty_fields.append((field_name, line_number))
                continue
            if current_field is not None and (
                text.startswith("  ") or text.startswith("\t")
            ):
                continue
            report.errors.append(
                Issue(
                    "PL047",
                    f"unexpected history content; expected one of: {', '.join(HISTORY_FIELDS)}",
                    line_number,
                )
            )

        if found_fields != list(HISTORY_FIELDS):
            report.errors.append(
                Issue(
                    "PL048",
                    "history fields must appear exactly once and in this order: "
                    + ", ".join(HISTORY_FIELDS),
                    heading_line,
                )
            )
        for field_name, line_number in empty_fields:
            report.errors.append(
                Issue("PL049", f"history field {field_name} must have a value", line_number)
            )

    for previous, current in zip(ordered_timestamps, ordered_timestamps[1:]):
        if current[0] > previous[0]:
            report.errors.append(
                Issue(
                    "PL063",
                    "Recent Task History must be newest-first (UTC timestamps non-increasing); "
                    f"{current[2]} appears after older {previous[2]}",
                    current[1],
                )
            )


def _history_entries_from_lines(
    lines: Sequence[str],
    label: str,
    expected_month: Optional[str] = None,
    allow_empty: bool = True,
) -> List[ArchiveEntry]:
    starts = [index for index, line in enumerate(lines) if line.startswith("### ")]
    semantic_nonentry_prefix = [
        line
        for line in lines[: starts[0] if starts else len(lines)]
        if line.strip() and not line.strip().startswith("<!--")
    ]
    if semantic_nonentry_prefix:
        raise ToolError(
            EXIT_INVALID,
            "ARCHIVE_INVALID",
            f"{label} contains content before its first task entry",
        )
    if not starts:
        if allow_empty:
            return []
        raise ToolError(
            EXIT_INVALID,
            "ARCHIVE_EMPTY",
            f"{label} must contain at least one complete task entry",
        )

    validation = ValidationReport(byte_count=0)
    _validate_history([(index + 1, line) for index, line in enumerate(lines)], validation)
    if validation.errors:
        detail = "; ".join(
            f"{issue.code}{f' line {issue.line}' if issue.line else ''}: {issue.message}"
            for issue in validation.errors
        )
        raise ToolError(EXIT_INVALID, "ARCHIVE_INVALID", f"{label} is invalid: {detail}")

    entries: List[ArchiveEntry] = []
    boundaries = starts + [len(lines)]
    for position, start in enumerate(starts):
        heading = _HISTORY_HEADING_RE.fullmatch(lines[start].strip())
        if heading is None:
            raise ToolError(
                EXIT_INVALID,
                "ARCHIVE_INVALID",
                f"{label} has an invalid task heading on line {start + 1}",
            )
        timestamp = heading.group(1)
        task_id = heading.group(2).strip()
        if expected_month is not None and timestamp[:7] != expected_month:
            raise ToolError(
                EXIT_INVALID,
                "ARCHIVE_MONTH_MISMATCH",
                f"{label} task {task_id!r} belongs to {timestamp[:7]}, not {expected_month}",
            )
        end = boundaries[position + 1]
        block = "\n".join(lines[start:end]).strip("\n") + "\n"
        entries.append(ArchiveEntry(timestamp, task_id, block, block.encode("utf-8")))
    return entries


def _exact_history_raw_map(
    data: bytes,
    label: str,
    log_document: bool,
) -> Dict[str, bytes]:
    raw_lines = data.splitlines(keepends=True)
    try:
        logical_lines = [
            line.rstrip(b"\r\n").decode("utf-8") for line in raw_lines
        ]
    except UnicodeDecodeError as exc:
        raise ToolError(EXIT_INVALID, "HISTORY_INVALID", f"{label} is not UTF-8: {exc}") from exc

    section_start = 0
    if log_document:
        try:
            section_start = logical_lines.index("## Recent Task History") + 1
        except ValueError as exc:
            raise ToolError(
                EXIT_INVALID,
                "LOG_INVALID",
                f"{label} lacks ## Recent Task History",
            ) from exc
    section_raw = raw_lines[section_start:]
    section_logical = logical_lines[section_start:]
    starts = [
        index for index, line in enumerate(section_logical) if line.startswith("### ")
    ]
    boundaries = starts + [len(section_logical)]
    result: Dict[str, bytes] = {}
    for position, start in enumerate(starts):
        heading = _HISTORY_HEADING_RE.fullmatch(section_logical[start].strip())
        if heading is None:
            raise ToolError(
                EXIT_INVALID,
                "HISTORY_INVALID",
                f"{label} has an invalid task heading",
            )
        task_id = heading.group(2).strip()
        end_boundary = boundaries[position + 1]
        body_end: Optional[int] = None
        for index in range(start + 1, end_boundary):
            parsed = _history_field(section_logical[index].strip())
            if parsed is not None and parsed[0] == "Next":
                body_end = index + 1
        if body_end is None:
            raise ToolError(
                EXIT_INVALID,
                "HISTORY_INVALID",
                f"{label} task {task_id!r} lacks its Next field",
            )
        body = list(section_raw[start:body_end])
        body[-1] = body[-1].rstrip(b"\r\n")
        result[task_id.casefold()] = b"".join(body)
    return result


def _with_exact_raw(
    entries: Sequence[ArchiveEntry],
    raw_by_id: Dict[str, bytes],
    label: str,
) -> List[ArchiveEntry]:
    result: List[ArchiveEntry] = []
    for entry in entries:
        raw = raw_by_id.get(entry.task_id.casefold())
        if raw is None:
            raise ToolError(
                EXIT_INVALID,
                "HISTORY_INVALID",
                f"{label} could not bind exact bytes for task-id {entry.task_id!r}",
            )
        result.append(ArchiveEntry(entry.timestamp, entry.task_id, entry.text, raw))
    return result


def _history_entries_from_log(data: bytes, label: str) -> List[ArchiveEntry]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ToolError(EXIT_INVALID, "LOG_INVALID", f"{label} is not UTF-8: {exc}") from exc
    lines = text.replace("\r\n", "\n").replace("\r", "\n").splitlines()
    try:
        start = lines.index("## Recent Task History") + 1
    except ValueError as exc:
        raise ToolError(
            EXIT_INVALID,
            "LOG_INVALID",
            f"{label} lacks ## Recent Task History",
        ) from exc
    entries = _history_entries_from_lines(lines[start:], label, allow_empty=True)
    return _with_exact_raw(entries, _exact_history_raw_map(data, label, True), label)


def _archive_entries_from_bytes(
    data: bytes,
    label: str,
    expected_month: str,
    allow_empty: bool,
) -> List[ArchiveEntry]:
    if data.startswith(b"\xef\xbb\xbf"):
        raise ToolError(EXIT_INVALID, "ARCHIVE_INVALID", f"{label} must not contain a UTF-8 BOM")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ToolError(EXIT_INVALID, "ARCHIVE_INVALID", f"{label} is not UTF-8: {exc}") from exc
    if "\x00" in text:
        raise ToolError(EXIT_INVALID, "ARCHIVE_INVALID", f"{label} contains NUL")
    lines = text.replace("\r\n", "\n").replace("\r", "\n").splitlines()
    entries = _history_entries_from_lines(lines, label, expected_month, allow_empty)
    return _with_exact_raw(entries, _exact_history_raw_map(data, label, False), label)


def validate_log_bytes(data: bytes) -> ValidationReport:
    """Validate exact PROJECT_LOG.md bytes without modifying them."""

    report = ValidationReport(byte_count=len(data))
    if data.startswith(b"\xef\xbb\xbf"):
        report.errors.append(Issue("PL001", "UTF-8 BOM is not allowed"))
        return report
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        report.errors.append(
            Issue(
                "PL002",
                f"log must be valid UTF-8: byte {exc.start}: {exc.reason}",
            )
        )
        return report
    if "\x00" in text:
        report.errors.append(Issue("PL003", "NUL characters are not allowed"))
        return report

    lines = text.splitlines()
    headings, unclosed_comment, unclosed_fence = _scan_headings(lines)
    if unclosed_comment:
        report.errors.append(Issue("PL004", "unclosed HTML comment"))
    if unclosed_fence:
        report.errors.append(Issue("PL005", "unclosed fenced code block"))

    top_headings = [heading for heading in headings if heading.level <= 2]
    actual = [(heading.level, heading.title) for heading in top_headings]
    if actual != list(EXPECTED_HEADINGS):
        expected_text = " -> ".join(
            ("#" * level) + " " + title for level, title in EXPECTED_HEADINGS
        )
        report.errors.append(
            Issue(
                "PL006",
                "top-level headings must appear exactly once in this order: " + expected_text,
            )
        )
    else:
        in_comment = False
        first_content_line: Optional[int] = None
        for index, raw_line in enumerate(lines):
            visible, in_comment = _strip_html_comments(raw_line, in_comment)
            if visible.strip():
                first_content_line = index + 1
                break
        if first_content_line != top_headings[0].line:
            report.errors.append(
                Issue("PL007", "# Project Log must be the first non-comment content")
            )

        section_ranges: Dict[str, List[Tuple[int, str]]] = {}
        for index, heading in enumerate(top_headings[1:], start=1):
            next_index = (
                top_headings[index + 1].index
                if index + 1 < len(top_headings)
                else len(lines)
            )
            section_ranges[heading.title] = [
                (line_index + 1, lines[line_index])
                for line_index in range(heading.index + 1, next_index)
            ]

        section_for_line: Dict[int, str] = {}
        for name, section_lines in section_ranges.items():
            for number, _line in section_lines:
                section_for_line[number] = name
        for heading in headings:
            if heading.level >= 3:
                section = section_for_line.get(heading.line)
                if section != "Recent Task History" or heading.level != 3:
                    report.errors.append(
                        Issue(
                            "PL008",
                            "subheadings are allowed only for level-3 Recent Task History entries",
                            heading.line,
                        )
                    )

        snapshot_fields = _parse_fixed_fields(
            "Current Snapshot",
            section_ranges["Current Snapshot"],
            SNAPSHOT_FIELDS,
            report,
        )
        goal_fields = _parse_fixed_fields(
            "Current Goal and Scope",
            section_ranges["Current Goal and Scope"],
            GOAL_FIELDS,
            report,
        )
        open_work_is_template_none = _validate_open_work(
            section_ranges["Open Work and Blockers"], report
        )

        seen_ids: Dict[str, int] = {}
        confirmed_rows = _parse_table(
            "Confirmed Facts and Decisions",
            section_ranges["Confirmed Facts and Decisions"],
            CONFIRMED_COLUMNS,
            report,
        )
        for line, row in confirmed_rows:
            _validate_unique_id(row["ID"], line, seen_ids, report)
            if _is_placeholder(row["Fact or decision"]):
                report.errors.append(
                    Issue("PL059", "Fact or decision must be non-placeholder", line)
                )
            if _is_placeholder(row["Evidence"]):
                report.errors.append(
                    Issue("PL060", "a confirmed fact or decision requires Evidence", line)
                )
            if not _ARTIFACT_REVISION_RE.fullmatch(_plain_cell(row["Revision"])):
                report.errors.append(
                    Issue(
                        "PL064",
                        "Confirmed Facts Revision must be sha256:<64 lowercase hex>, "
                        "git:<40 or 64 lowercase hex>, or external:<provider>:<immutable-id>",
                        line,
                    )
                )
            _validate_updated(row["Updated"], line, "Confirmed Facts and Decisions", report)

        active_rows = _parse_table(
            "Active Artifacts",
            section_ranges["Active Artifacts"],
            ACTIVE_COLUMNS,
            report,
        )
        active_records = _validate_artifact_rows(
            active_rows,
            ACTIVE_STATUSES,
            "Active Artifacts",
            seen_ids,
            report,
        )

        tombstone_rows = _parse_table(
            "Invalidated Artifact Tombstones",
            section_ranges["Invalidated Artifact Tombstones"],
            TOMBSTONE_COLUMNS,
            report,
        )
        tombstone_records = _validate_artifact_rows(
            tombstone_rows,
            TOMBSTONE_STATUSES,
            "Invalidated Artifact Tombstones",
            seen_ids,
            report,
        )
        _validate_artifact_relationships(active_records + tombstone_records, report)

        _validate_history(section_ranges["Recent Task History"], report)
        last_updated = snapshot_fields.get("Last updated")
        if last_updated == "UNKNOWN":
            pristine_unknown_state = (
                snapshot_fields.get("Project") == "UNKNOWN"
                and snapshot_fields.get("Phase") == "UNKNOWN"
                and snapshot_fields.get("State")
                == "No verified project state recorded."
                and all(goal_fields.get(field) == "UNKNOWN" for field in GOAL_FIELDS)
                and not confirmed_rows
                and not active_rows
                and not tombstone_rows
                and open_work_is_template_none
                and report.history_entries == 0
            )
            if not pristine_unknown_state:
                report.errors.append(
                    Issue(
                        "PL061",
                        "Current Snapshot Last updated may be UNKNOWN only for the exact "
                        "zero-history template state; any real state requires a UTC timestamp",
                    )
                )
        elif last_updated is not None and not _is_utc_timestamp(last_updated):
            report.errors.append(
                Issue(
                    "PL062",
                    "Current Snapshot Last updated must be UNKNOWN for a zero-history template "
                    "or UTC YYYY-MM-DDTHH:MM:SSZ",
                )
            )

    if len(data) > MAX_ACTIVE_LOG_BYTES:
        report.warnings.append(
            Issue(
                "PLW001",
                f"active log is {len(data)} bytes; recommended maximum is {MAX_ACTIVE_LOG_BYTES} bytes. "
                "Archive oldest history explicitly; no data was removed.",
            )
        )
    if report.history_entries > MAX_RECENT_TASKS:
        report.warnings.append(
            Issue(
                "PLW002",
                f"Recent Task History has {report.history_entries} entries; recommended maximum is "
                f"{MAX_RECENT_TASKS}. Archive oldest entries explicitly; no data was removed.",
            )
        )
    return report


def _print_issues(report: ValidationReport) -> None:
    for issue in report.errors:
        location = f" line {issue.line}" if issue.line is not None else ""
        print(f"ERROR [{issue.code}]{location}: {issue.message}", file=sys.stderr)
    for issue in report.warnings:
        location = f" line {issue.line}" if issue.line is not None else ""
        print(f"WARNING [{issue.code}]{location}: {issue.message}", file=sys.stderr)


def _validation_json(
    path: Path,
    data: bytes,
    report: ValidationReport,
    strict_limits: bool,
) -> str:
    payload = {
        "path": str(path),
        "sha256": _sha256(data),
        "bytes": report.byte_count,
        "history_entries": report.history_entries,
        "schema_valid": report.schema_valid,
        "valid": report.valid(strict_limits),
        "strict_limits": strict_limits,
        "errors": [issue.as_dict() for issue in report.errors],
        "warnings": [issue.as_dict() for issue in report.warnings],
    }
    return json.dumps(payload, ensure_ascii=False, sort_keys=True)


def _fsync_directory(directory: Path) -> None:
    if os.name == "nt":
        return
    flags = os.O_RDONLY
    if hasattr(os, "O_DIRECTORY"):
        flags |= os.O_DIRECTORY
    try:
        descriptor = os.open(os.fspath(directory), flags)
    except OSError:
        return
    try:
        os.fsync(descriptor)
    except OSError:
        pass
    finally:
        os.close(descriptor)


def _same_identity(first: os.stat_result, second: os.stat_result) -> bool:
    try:
        return os.path.samestat(first, second)
    except (AttributeError, OSError):
        return (first.st_dev, first.st_ino) == (second.st_dev, second.st_ino)


def _write_exclusive(path: Path, data: bytes) -> None:
    flags = (
        os.O_WRONLY
        | os.O_CREAT
        | os.O_EXCL
        | getattr(os, "O_BINARY", 0)
        | getattr(os, "O_CLOEXEC", 0)
        | getattr(os, "O_NOFOLLOW", 0)
    )
    descriptor: Optional[int] = None
    opened_info: Optional[os.stat_result] = None
    try:
        descriptor = os.open(os.fspath(path), flags, 0o600)
        opened_info = os.fstat(descriptor)
        if not stat.S_ISREG(opened_info.st_mode):
            raise OSError(errno.EINVAL, "new output is not a regular file")
        with os.fdopen(descriptor, "wb") as handle:
            descriptor = None
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        published_info = _require_regular_file(path, "new output")
        if opened_info is None or not _same_identity(opened_info, published_info):
            raise ToolError(
                EXIT_IO,
                "OUTPUT_REPLACED",
                f"new output path changed while it was being written: {path}",
            )
        _fsync_directory(path.parent)
    except FileExistsError:
        raise
    except ToolError:
        raise
    except OSError as exc:
        if descriptor is not None:
            os.close(descriptor)
        if opened_info is not None:
            try:
                current_info = os.lstat(os.fspath(path))
                if _same_identity(opened_info, current_info):
                    path.unlink()
            except OSError:
                pass
        raise ToolError(EXIT_IO, "WRITE_FAILED", f"cannot create {path}: {exc}") from exc


def _atomic_replace(path: Path, data: bytes) -> None:
    temporary: Optional[Path] = None
    try:
        target_info = _require_regular_file(path, "atomic replace target")
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.",
            suffix=".tmp",
            dir=os.fspath(path.parent),
        )
        temporary = Path(temporary_name)
        mode = stat.S_IMODE(target_info.st_mode)
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        try:
            os.chmod(os.fspath(temporary), mode)
        except OSError:
            pass
        os.replace(os.fspath(temporary), os.fspath(path))
        temporary = None
        _fsync_directory(path.parent)
    except OSError as exc:
        raise ToolError(EXIT_IO, "ATOMIC_COMMIT_FAILED", f"cannot atomically replace {path}: {exc}") from exc
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError:
                pass


def _atomic_create_via_link(path: Path, data: bytes) -> None:
    """Atomically publish a complete new file without replacing an existing path."""

    temporary: Optional[Path] = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.",
            suffix=".tmp",
            dir=os.fspath(path.parent),
        )
        temporary = Path(temporary_name)
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(os.fspath(temporary), 0o600)
        temporary_info = os.lstat(os.fspath(temporary))
        try:
            os.link(os.fspath(temporary), os.fspath(path))
        except FileExistsError:
            raise
        except OSError as exc:
            raise ToolError(
                EXIT_IO,
                "ATOMIC_CREATE_UNSUPPORTED",
                f"cannot atomically publish new file with a same-directory hard link {path}: {exc}",
            ) from exc
        published_info = _require_regular_file(path, "atomically created file")
        if not _same_identity(temporary_info, published_info):
            raise ToolError(
                EXIT_IO,
                "ATOMIC_CREATE_VERIFY_FAILED",
                f"published file is not the completed temporary file: {path}",
            )
        _fsync_directory(path.parent)
    except FileExistsError:
        raise
    except ToolError:
        raise
    except OSError as exc:
        raise ToolError(
            EXIT_IO,
            "ATOMIC_CREATE_FAILED",
            f"cannot atomically create {path}: {exc}",
        ) from exc
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError:
                pass


class ExclusiveLock:
    """Portable advisory lock whose ownership is released by the operating system."""

    def __init__(self, path: Path, timeout: float) -> None:
        self.path = path
        self.timeout = timeout
        self.descriptor: Optional[int] = None
        self.acquired = False

    def _try_lock(self) -> None:
        if self.descriptor is None:
            raise OSError(errno.EBADF, "lock descriptor is closed")
        os.lseek(self.descriptor, 0, os.SEEK_SET)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(self.descriptor, msvcrt.LK_NBLCK, 1)
        else:
            import fcntl

            fcntl.flock(self.descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)

    def _unlock(self) -> None:
        if self.descriptor is None or not self.acquired:
            return
        os.lseek(self.descriptor, 0, os.SEEK_SET)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(self.descriptor, msvcrt.LK_UNLCK, 1)
        else:
            import fcntl

            fcntl.flock(self.descriptor, fcntl.LOCK_UN)

    def _verify_path(self, opened_info: os.stat_result) -> None:
        current_info = _require_regular_file(self.path, "advisory lock")
        if not _same_identity(opened_info, current_info):
            raise ToolError(
                EXIT_USAGE,
                "LOCK_PATH_REPLACED",
                f"advisory lock path changed while opening it: {self.path}",
            )

    def __enter__(self) -> "ExclusiveLock":
        deadline = time.monotonic() + self.timeout
        flags = (
            os.O_RDWR
            | os.O_CREAT
            | getattr(os, "O_BINARY", 0)
            | getattr(os, "O_CLOEXEC", 0)
            | getattr(os, "O_NOFOLLOW", 0)
        )
        try:
            self.descriptor = os.open(os.fspath(self.path), flags, 0o600)
            opened_info = os.fstat(self.descriptor)
            if not stat.S_ISREG(opened_info.st_mode):
                raise ToolError(
                    EXIT_USAGE,
                    "UNSAFE_LOCK",
                    f"advisory lock is not a regular file: {self.path}",
                )
            self._verify_path(opened_info)
            if opened_info.st_size == 0:
                os.write(self.descriptor, b"\0")
                os.fsync(self.descriptor)
            while True:
                try:
                    self._try_lock()
                    self.acquired = True
                    self._verify_path(opened_info)
                    return self
                except OSError as exc:
                    if exc.errno not in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
                        raise
                if time.monotonic() >= deadline:
                    raise ToolError(
                        EXIT_LOCK_TIMEOUT,
                        "LOCK_TIMEOUT",
                        f"timed out after {self.timeout:g}s waiting for lock {self.path}",
                    )
                remaining = max(0.0, deadline - time.monotonic())
                time.sleep(min(0.1, remaining))
        except ToolError:
            if self.descriptor is not None:
                os.close(self.descriptor)
                self.descriptor = None
            raise
        except OSError as exc:
            if self.descriptor is not None:
                os.close(self.descriptor)
                self.descriptor = None
            raise ToolError(
                EXIT_IO,
                "LOCK_CREATE_FAILED",
                f"cannot open or acquire advisory lock {self.path}: {exc}",
            ) from exc

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        try:
            self._unlock()
        except OSError as unlock_error:
            print(
                f"WARNING [LOCK_RELEASE_FAILED]: cannot unlock {self.path}: {unlock_error}",
                file=sys.stderr,
            )
        finally:
            self.acquired = False
            if self.descriptor is not None:
                os.close(self.descriptor)
                self.descriptor = None


def _parse_baseline(value: str) -> str:
    baseline = value.strip()
    if baseline.lower().startswith("sha256:"):
        baseline = baseline[7:]
    if not _SHA256_RE.fullmatch(baseline):
        raise ToolError(
            EXIT_USAGE,
            "INVALID_BASELINE",
            "--baseline must be a 64-character SHA-256 hex digest",
        )
    return baseline.lower()


def _timeout_value(value: str) -> float:
    try:
        timeout = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if timeout < 0 or timeout > 60:
        raise argparse.ArgumentTypeError("must be between 0 and 60 seconds")
    return timeout


def _pending_path(root: Path, requested: Optional[str]) -> Path:
    if requested:
        return _safe_project_path(root, requested, "pending path")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    name = f"{stamp}-{os.getpid()}-{uuid.uuid4().hex[:8]}-PROJECT_LOG.md"
    return _safe_project_path(
        root,
        os.fspath(Path(".project-memory") / "pending" / name),
        "pending path",
    )


def _save_pending(
    root: Path,
    data: bytes,
    requested: Optional[str],
    disabled: bool,
) -> Optional[Path]:
    if disabled:
        return None
    destination = _pending_path(root, requested)
    try:
        _ensure_directory_chain(root, destination.parent)
        _write_exclusive(destination, data)
        saved = _read_bytes(destination, "pending copy")
        expected_hash = _sha256(data)
        saved_hash = _sha256(saved)
        if saved != data or saved_hash != expected_hash:
            print(
                f"WARNING [PENDING_VERIFY_FAILED]: pending copy hash mismatch at {destination}; "
                f"expected {expected_hash}, found {saved_hash}",
                file=sys.stderr,
            )
            return None
    except FileExistsError:
        print(
            f"WARNING [PENDING_EXISTS]: pending path already exists and was not overwritten: {destination}",
            file=sys.stderr,
        )
        return None
    except ToolError as exc:
        print(
            f"WARNING [PENDING_WRITE_FAILED]: {exc.message}",
            file=sys.stderr,
        )
        return None
    return destination


def _archive_month_from_name(path: Path, label: str) -> str:
    match = re.fullmatch(r"(\d{4}-\d{2})\.md", path.name)
    if match is None:
        raise ToolError(
            EXIT_USAGE,
            "INVALID_ARCHIVE_NAME",
            f"{label} filename must be YYYY-MM.md: {path}",
        )
    month = match.group(1)
    try:
        datetime.strptime(month, "%Y-%m")
    except ValueError as exc:
        raise ToolError(
            EXIT_USAGE,
            "INVALID_ARCHIVE_NAME",
            f"{label} filename has an invalid calendar month: {path.name}",
        ) from exc
    return month


def _load_archive_candidates(
    root: Path,
    values: Sequence[str],
) -> Dict[str, List[ArchiveEntry]]:
    candidates: Dict[str, List[ArchiveEntry]] = {}
    seen: Dict[str, ArchiveEntry] = {}
    archive_root = _safe_project_path(
        root, os.fspath(Path(".project-memory") / "archive"), "archive directory"
    )
    for value in values:
        path = _safe_project_path(root, value, "archive candidate")
        _require_regular_file(path, "archive candidate")
        month = _archive_month_from_name(path, "archive candidate")
        destination = archive_root / path.name
        if _same_path(path, destination):
            raise ToolError(
                EXIT_USAGE,
                "ARCHIVE_CANDIDATE_IS_DESTINATION",
                f"archive candidate must be a separate project file, not the live archive: {path}",
            )
        entries = _archive_entries_from_bytes(
            _read_bytes(path, "archive candidate"),
            f"archive candidate {path}",
            month,
            allow_empty=False,
        )
        for entry in entries:
            prior = seen.get(entry.task_id.casefold())
            if prior is not None and prior.raw != entry.raw:
                raise ToolError(
                    EXIT_INVALID,
                    "ARCHIVE_TASK_CONFLICT",
                    f"archive candidates disagree about task-id {entry.task_id!r}",
                )
            seen[entry.task_id.casefold()] = entry
            candidates.setdefault(month, []).append(entry)
    return candidates


def _load_existing_archives(
    root: Path,
) -> Dict[str, Tuple[Path, bytes, List[ArchiveEntry]]]:
    archive_root = _safe_project_path(
        root, os.fspath(Path(".project-memory") / "archive"), "archive directory"
    )
    if not _lexists(archive_root):
        return {}
    info = _inspect_component_chain(archive_root, "archive directory", require_all=True)
    if info is None or not stat.S_ISDIR(info.st_mode):
        raise ToolError(EXIT_IO, "INVALID_ARCHIVE_DIRECTORY", f"not a directory: {archive_root}")

    result: Dict[str, Tuple[Path, bytes, List[ArchiveEntry]]] = {}
    try:
        with os.scandir(os.fspath(archive_root)) as iterator:
            names = sorted(entry.name for entry in iterator)
    except OSError as exc:
        raise ToolError(
            EXIT_IO,
            "ARCHIVE_SCAN_FAILED",
            f"cannot scan archive directory {archive_root}: {exc}",
        ) from exc
    for name in names:
        if not re.fullmatch(r"\d{4}-\d{2}\.md", name):
            continue
        path = _safe_project_path(root, os.fspath(Path(".project-memory") / "archive" / name), "archive")
        _require_regular_file(path, "archive")
        month = _archive_month_from_name(path, "archive")
        data = _read_bytes(path, "archive")
        entries = _archive_entries_from_bytes(
            data, f"archive {path}", month, allow_empty=True
        )
        result[month] = (path, data, entries)
    return result


def _prepare_archive_updates(
    root: Path,
    candidates: Dict[str, List[ArchiveEntry]],
) -> Tuple[List[ArchiveUpdate], Dict[str, ArchiveEntry]]:
    existing = _load_existing_archives(root)
    all_entries: Dict[str, ArchiveEntry] = {}
    for _month, (_path, _data, entries) in existing.items():
        for entry in entries:
            key = entry.task_id.casefold()
            prior = all_entries.get(key)
            if prior is not None and prior.raw != entry.raw:
                raise ToolError(
                    EXIT_INVALID,
                    "ARCHIVE_TASK_CONFLICT",
                    f"existing archives disagree about task-id {entry.task_id!r}",
                )
            all_entries[key] = entry

    archive_root = _safe_project_path(
        root, os.fspath(Path(".project-memory") / "archive"), "archive directory"
    )
    updates: List[ArchiveUpdate] = []
    for month, additions in sorted(candidates.items()):
        if month in existing:
            path, original, entries = existing[month]
            original_bytes: Optional[bytes] = original
        else:
            path = _safe_project_path(
                root,
                os.fspath(Path(".project-memory") / "archive" / f"{month}.md"),
                "archive",
            )
            original = b""
            entries = []
            original_bytes = None

        by_id = {entry.task_id.casefold(): entry for entry in entries}
        new_entries: List[ArchiveEntry] = []
        for entry in additions:
            key = entry.task_id.casefold()
            prior = by_id.get(key) or all_entries.get(key)
            if prior is not None:
                if prior.raw != entry.raw:
                    raise ToolError(
                        EXIT_INVALID,
                        "ARCHIVE_TASK_CONFLICT",
                        f"archive content conflicts for task-id {entry.task_id!r}",
                    )
                continue
            by_id[key] = entry
            all_entries[key] = entry
            new_entries.append(entry)
        if not new_entries:
            continue

        separator = b""
        if original:
            separator = b"\n" if original.endswith(b"\n") else b"\n\n"
        appended = b"\n\n".join(entry.raw for entry in new_entries)
        content = original + separator + appended + b"\n"
        _archive_entries_from_bytes(
            content, f"prospective archive {path}", month, allow_empty=False
        )
        updates.append(ArchiveUpdate(path, original_bytes, content))
    return updates, all_entries


def _apply_archive_updates(root: Path, updates: Sequence[ArchiveUpdate]) -> None:
    for update in updates:
        _ensure_directory_chain(root, update.path.parent)
        if update.original is not None:
            _require_regular_file(update.path, "archive")
            current = _read_bytes(update.path, "archive before update")
            if current != update.original:
                raise ToolError(
                    EXIT_CONFLICT,
                    "ARCHIVE_BASELINE_CONFLICT",
                    f"archive changed after merge planning; active log was not changed: {update.path}",
                )
            _atomic_replace(update.path, update.content)
        else:
            try:
                _atomic_create_via_link(update.path, update.content)
            except FileExistsError as exc:
                raise ToolError(
                    EXIT_CONFLICT,
                    "ARCHIVE_APPEARED",
                    f"archive appeared concurrently despite the project lock: {update.path}",
                ) from exc
        verified = _read_bytes(update.path, "committed archive")
        if verified != update.content:
            raise ToolError(
                EXIT_IO,
                "ARCHIVE_VERIFY_FAILED",
                f"archive verification failed after write: {update.path}",
            )


def _command_init(args: argparse.Namespace) -> int:
    root = _resolve_root(args.root)
    target = _safe_project_path(root, args.log, "log path")
    template = _resolve_input_path(root, args.template, "template")
    template_data = _read_bytes(template, "template")
    template_report = validate_log_bytes(template_data)
    if not template_report.schema_valid:
        _print_issues(template_report)
        print(
            f"ERROR [TEMPLATE_INVALID]: refusing to initialize from invalid template {template}",
            file=sys.stderr,
        )
        return EXIT_INVALID

    if _lexists(target):
        existing_data = _read_bytes(target, "existing log")
        existing_report = validate_log_bytes(existing_data)
        _print_issues(existing_report)
        if not existing_report.schema_valid:
            print(
                f"ERROR [EXISTING_LOG_INVALID]: existing log was not overwritten: {target}",
                file=sys.stderr,
            )
            return EXIT_INVALID
        print(f"UNCHANGED: existing log was not overwritten: {target}")
        print(f"SHA256: {_sha256(existing_data)}")
        return EXIT_OK

    _ensure_directory_chain(root, target.parent)
    try:
        _write_exclusive(target, template_data)
    except FileExistsError:
        appeared_data = _read_bytes(target, "concurrently created log")
        appeared_report = validate_log_bytes(appeared_data)
        _print_issues(appeared_report)
        if not appeared_report.schema_valid:
            print(
                f"ERROR [CONCURRENT_LOG_INVALID]: concurrently created log was not overwritten: {target}",
                file=sys.stderr,
            )
            return EXIT_INVALID
        print(f"UNCHANGED: valid log appeared concurrently and was not overwritten: {target}")
        print(f"SHA256: {_sha256(appeared_data)}")
        return EXIT_OK
    print(f"INITIALIZED: {target}")
    print(f"SHA256: {_sha256(template_data)}")
    return EXIT_OK


def _command_check(args: argparse.Namespace) -> int:
    root = _resolve_root(args.root)
    target = _safe_project_path(root, args.log, "log path")
    data = _read_bytes(target, "log")
    report = validate_log_bytes(data)
    if args.json:
        print(_validation_json(target, data, report, args.strict_limits))
    else:
        _print_issues(report)
        if report.valid(args.strict_limits):
            print(
                f"VALID: {target} bytes={report.byte_count} "
                f"history_entries={report.history_entries} sha256={_sha256(data)}"
            )
        elif args.strict_limits and report.schema_valid:
            print(
                "ERROR [STRICT_LIMITS]: recommended size/history limits were exceeded",
                file=sys.stderr,
            )
    return EXIT_OK if report.valid(args.strict_limits) else EXIT_INVALID


def _command_hash(args: argparse.Namespace) -> int:
    root = _resolve_root(args.root)
    target = _safe_project_path(root, args.log, "log path")
    data = _read_bytes(target, "log")
    digest = _sha256(data)
    if args.json:
        print(
            json.dumps(
                {
                    "algorithm": "sha256",
                    "bytes": len(data),
                    "path": str(target),
                    "sha256": digest,
                },
                ensure_ascii=False,
                sort_keys=True,
            )
        )
    else:
        print(digest)
    return EXIT_OK


def _command_commit(args: argparse.Namespace) -> int:
    root = _resolve_root(args.root)
    target = _safe_project_path(root, args.log, "log path")
    candidate = _resolve_input_path(root, args.candidate, "candidate")
    if _same_path(candidate, target):
        raise ToolError(
            EXIT_USAGE,
            "CANDIDATE_IS_TARGET",
            "candidate must be a separate regular file, not the live project log",
        )
    candidate_data = _read_bytes(candidate, "candidate")
    candidate_hash = _sha256(candidate_data)
    report = validate_log_bytes(candidate_data)
    _print_issues(report)
    if not report.valid(args.strict_limits):
        diagnostic = (
            "CANDIDATE_LIMITS"
            if report.schema_valid
            else "CANDIDATE_INVALID"
        )
        print(
            f"ERROR [{diagnostic}]: target was not changed; candidate remains at {candidate}",
            file=sys.stderr,
        )
        return EXIT_INVALID

    archive_candidates = _load_archive_candidates(root, args.archive_candidate)
    baseline = _parse_baseline(args.baseline)
    if not _lexists(target):
        pending = _save_pending(root, candidate_data, args.pending, args.no_pending)
        print(
            f"ERROR [BASELINE_CONFLICT]: target does not exist; expected baseline {baseline}",
            file=sys.stderr,
        )
        print(f"CANDIDATE: {candidate}", file=sys.stderr)
        if pending:
            print(f"PENDING: {pending} sha256={candidate_hash}", file=sys.stderr)
        return EXIT_CONFLICT
    _require_regular_file(target, "current log")
    parent_info = _inspect_component_chain(target.parent, "log parent", require_all=True)
    if parent_info is None or not stat.S_ISDIR(parent_info.st_mode):
        raise ToolError(EXIT_IO, "INVALID_LOG_PARENT", f"log parent is not a directory: {target.parent}")

    lock_path = _safe_project_path(
        root, os.fspath(target.with_name(target.name + ".lock")), "advisory lock"
    )
    with ExclusiveLock(lock_path, args.lock_timeout):
        # The lock is advisory, so revalidate every lexical component and the
        # final regular file after acquisition.
        _safe_project_path(root, os.fspath(target), "current log")
        current_data = _read_bytes(target, "current log")
        current_hash = _sha256(current_data)

        # A prior writer may already have committed these exact candidate
        # bytes. Treat that as converged success even if the caller's baseline
        # is stale, while still applying any idempotent archive candidates.
        if current_data == candidate_data:
            if archive_candidates:
                archive_updates, _archived = _prepare_archive_updates(
                    root, archive_candidates
                )
                _apply_archive_updates(root, archive_updates)
            print(f"UNCHANGED: candidate already matches {target}")
            print(f"SHA256: {candidate_hash}")
            return EXIT_OK

        if current_hash != baseline:
            pending = _save_pending(root, candidate_data, args.pending, args.no_pending)
            print(
                f"ERROR [BASELINE_CONFLICT]: expected {baseline}, current {current_hash}; target was not changed",
                file=sys.stderr,
            )
            print(f"CANDIDATE: {candidate}", file=sys.stderr)
            if pending:
                print(f"PENDING: {pending} sha256={candidate_hash}", file=sys.stderr)
            return EXIT_CONFLICT

        current_history = {
            entry.task_id.casefold(): entry
            for entry in _history_entries_from_log(current_data, "current log")
        }
        candidate_history = {
            entry.task_id.casefold(): entry
            for entry in _history_entries_from_log(candidate_data, "candidate log")
        }
        rewritten = sorted(
            task_id
            for task_id in set(current_history) & set(candidate_history)
            if current_history[task_id].raw != candidate_history[task_id].raw
        )
        if rewritten:
            print(
                "ERROR [HISTORY_REWRITE]: existing task-id entries are immutable and must "
                "remain byte-identical: " + ", ".join(rewritten),
                file=sys.stderr,
            )
            print(f"CANDIDATE: {candidate}", file=sys.stderr)
            return EXIT_INVALID
        removed = sorted(set(current_history) - set(candidate_history))
        if removed or archive_candidates:
            archive_updates, archived_entries = _prepare_archive_updates(
                root, archive_candidates
            )
        else:
            archive_updates, archived_entries = [], {}
        missing = [task_id for task_id in removed if task_id not in archived_entries]
        mismatched = [
            task_id
            for task_id in removed
            if task_id in archived_entries
            and archived_entries[task_id].raw != current_history[task_id].raw
        ]
        if missing or mismatched:
            details: List[str] = []
            if missing:
                details.append("not archived: " + ", ".join(missing))
            if mismatched:
                details.append("archive content differs: " + ", ".join(mismatched))
            print(
                "ERROR [UNARCHIVED_HISTORY]: candidate removes task history without an "
                "exact archive copy; " + "; ".join(details),
                file=sys.stderr,
            )
            print(f"CANDIDATE: {candidate}", file=sys.stderr)
            return EXIT_INVALID

        # Archive first. A crash or later active-log conflict can therefore
        # leave duplicates, but can never remove the only durable task copy.
        _apply_archive_updates(root, archive_updates)

        # A non-cooperating writer may ignore the advisory lock. Recheck both
        # safety and the baseline immediately before the atomic replacement.
        _safe_project_path(root, os.fspath(target), "current log before commit")
        before_write = _read_bytes(target, "current log before commit")
        before_write_hash = _sha256(before_write)
        if before_write == candidate_data:
            print(f"UNCHANGED: candidate converged while archives were committed: {target}")
            print(f"SHA256: {candidate_hash}")
            return EXIT_OK
        if before_write_hash != baseline:
            pending = _save_pending(root, candidate_data, args.pending, args.no_pending)
            print(
                f"ERROR [BASELINE_CONFLICT]: expected {baseline}, current {before_write_hash}; "
                "archives may contain idempotent copies, but active log was not changed",
                file=sys.stderr,
            )
            print(f"CANDIDATE: {candidate}", file=sys.stderr)
            if pending:
                print(f"PENDING: {pending} sha256={candidate_hash}", file=sys.stderr)
            return EXIT_CONFLICT

        _require_regular_file(target, "current log immediately before commit")
        _atomic_replace(target, candidate_data)
        committed = _read_bytes(target, "committed log")
        if committed != candidate_data or _sha256(committed) != candidate_hash:
            raise ToolError(
                EXIT_IO,
                "COMMIT_VERIFY_FAILED",
                f"committed log does not match candidate hash {candidate_hash}: {target}",
            )
        print(f"COMMITTED: {target}")
        print(f"BASELINE: {baseline}")
        print(f"SHA256: {candidate_hash}")
        return EXIT_OK


def _add_common_paths(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--root",
        default=".",
        help="project root (default: current directory)",
    )
    parser.add_argument(
        "--log",
        default=os.fspath(DEFAULT_LOG),
        help="log path relative to --root (default: .project-memory/PROJECT_LOG.md)",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Safely initialize, validate, hash, and commit PROJECT_LOG.md",
        epilog=(
            "Exit codes: 0 success; 2 usage/path; 3 invalid schema; "
            "4 baseline conflict; 5 lock timeout; 6 refused overwrite; 7 I/O."
        ),
    )
    parser.add_argument("--version", action="version", version="project_log.py 1.0.0")
    subparsers = parser.add_subparsers(dest="command", required=True)

    init_parser = subparsers.add_parser(
        "init",
        help="create a zero-history log only when it does not already exist",
    )
    _add_common_paths(init_parser)
    init_parser.add_argument(
        "--template",
        default=os.fspath(DEFAULT_TEMPLATE),
        help="template file (default: skill assets/PROJECT_LOG.md)",
    )
    init_parser.set_defaults(handler=_command_init)

    check_parser = subparsers.add_parser(
        "check",
        help="validate the fixed log schema and report size/history limits",
    )
    _add_common_paths(check_parser)
    check_parser.add_argument(
        "--strict-limits",
        action="store_true",
        help="treat the 24 KiB and 20-history-entry recommendations as failures",
    )
    check_parser.add_argument("--json", action="store_true", help="emit a JSON report")
    check_parser.set_defaults(handler=_command_check)

    hash_parser = subparsers.add_parser(
        "hash",
        help="print the exact-byte SHA-256 baseline for the current log",
    )
    _add_common_paths(hash_parser)
    hash_parser.add_argument("--json", action="store_true", help="emit JSON")
    hash_parser.set_defaults(handler=_command_hash)

    commit_parser = subparsers.add_parser(
        "commit",
        help="validate and atomically commit a candidate when its baseline still matches",
    )
    _add_common_paths(commit_parser)
    commit_parser.add_argument(
        "--candidate",
        required=True,
        help="candidate PROJECT_LOG.md (relative paths are resolved from --root)",
    )
    commit_parser.add_argument(
        "--baseline",
        required=True,
        help="SHA-256 from the hash command (plain hex or sha256:<hex>)",
    )
    commit_parser.add_argument(
        "--archive-candidate",
        action="append",
        default=[],
        metavar="PROJECT_RELATIVE_YYYY-MM.md",
        help=(
            "project-root-contained file with complete history entries for its filename month; "
            "repeat for multiple months. Entries are task-id deduplicated and archives are "
            "committed before the active log."
        ),
    )
    commit_parser.add_argument(
        "--lock-timeout",
        type=_timeout_value,
        default=10.0,
        metavar="SECONDS",
        help="cross-platform advisory lock timeout from 0 to 60 seconds (default: 10)",
    )
    commit_parser.add_argument(
        "--strict-limits",
        action="store_true",
        help="reject an otherwise-valid candidate over 24 KiB or 20 history entries",
    )
    pending_group = commit_parser.add_mutually_exclusive_group()
    pending_group.add_argument(
        "--pending",
        help="conflict-copy path relative to --root (default: generated under .project-memory/pending)",
    )
    pending_group.add_argument(
        "--no-pending",
        action="store_true",
        help="do not create a conflict copy; the original candidate is still preserved",
    )
    commit_parser.set_defaults(handler=_command_commit)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.handler(args))
    except ToolError as exc:
        print(f"ERROR [{exc.diagnostic}]: {exc.message}", file=sys.stderr)
        return exc.exit_code
    except KeyboardInterrupt:
        print("ERROR [INTERRUPTED]: interrupted; target was not intentionally changed", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
