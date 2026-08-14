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
task_ids = [match.group(1).strip().decode("utf-8") for match in starts]
if task_ids != ["phase3-gate-20260814", "phase2-gate-20260814"]:
    raise SystemExit("live history order changed")

boundaries = [match.start() for match in starts] + [len(data)]
phase2_block = data[boundaries[1]:boundaries[2]]
next_line = re.search(br"(?m)^- Next: [^\r\n]*(?:\r?\n|$)", phase2_block)
if next_line is None:
    raise SystemExit("missing Phase 2 Next field")
archive_path.write_bytes(phase2_block[:next_line.end()].rstrip(b"\r\n") + newline)

phase3_block = data[boundaries[0]:boundaries[1]].rstrip(b"\r\n")
candidate_path.write_bytes(data[:boundaries[0]] + phase3_block + newline)
