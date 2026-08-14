from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
live_path = root / ".project-memory" / "PROJECT_LOG.md"
candidate_path = Path(__file__).with_name("PROJECT_LOG.candidate.md")
archive_path = Path(__file__).with_name("archive") / "2026-08.md"

data = live_path.read_bytes()
newline = b"\r\n" if b"\r\n" in data else b"\n"
starts = list(re.finditer(
    br"(?m)^### [0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z \| ([^|\r\n]+) \|",
    data,
))
if len(starts) != 6:
    raise SystemExit("unexpected live history entry count")

task_ids = [match.group(1).strip().decode("utf-8") for match in starts]
expected = [
    "phase2-gate-20260814",
    "phase1-baseline-20260813",
    "software-gate-close-20260813",
    "finalize-output-layout-20260813",
    "separate-output-layout-20260813",
    "bms-v1-prep-review-20260813",
]
if task_ids != expected:
    raise SystemExit("live history order changed")

boundaries = [match.start() for match in starts] + [len(data)]
raw_entries = []
for index in range(1, len(starts)):
    block = data[boundaries[index]:boundaries[index + 1]]
    next_line = re.search(br"(?m)^- Next: [^\r\n]*(?:\r?\n|$)", block)
    if next_line is None:
        raise SystemExit(f"missing Next field in {task_ids[index]}")
    raw_entries.append(block[:next_line.end()].rstrip(b"\r\n"))

archive_path.write_bytes((newline + newline).join(raw_entries) + newline)
phase2_block = data[boundaries[0]:boundaries[1]].rstrip(b"\r\n")
candidate_path.write_bytes(data[:boundaries[0]] + phase2_block + newline)
