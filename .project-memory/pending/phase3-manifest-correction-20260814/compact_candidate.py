from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
live_path = root / ".project-memory" / "PROJECT_LOG.md"
candidate_path = Path(__file__).with_name("PROJECT_LOG.candidate.md")
archive_path = Path(__file__).with_name("archive") / "2026-08.md"


def entries(data: bytes):
    starts = list(re.finditer(
        br"(?m)^### [0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z \| ([^|\r\n]+) \|",
        data,
    ))
    boundaries = [match.start() for match in starts] + [len(data)]
    result = []
    for index, match in enumerate(starts):
        block = data[boundaries[index]:boundaries[index + 1]]
        next_line = re.search(br"(?m)^- Next: [^\r\n]*(?:\r?\n|$)", block)
        if next_line is None:
            raise SystemExit("missing Next field")
        result.append((
            match.group(1).strip().decode("utf-8"),
            block[:next_line.end()].rstrip(b"\r\n"),
            boundaries[index],
        ))
    return result


live = live_path.read_bytes()
newline = b"\r\n" if b"\r\n" in live else b"\n"
live_entries = entries(live)
if [item[0] for item in live_entries] != [
    "phase3-gate-20260814", "phase2-gate-20260814"
]:
    raise SystemExit("live history order changed")
archive_path.write_bytes(
    (newline + newline).join(item[1] for item in live_entries) + newline
)

candidate = candidate_path.read_bytes()
candidate_entries = entries(candidate)
if [item[0] for item in candidate_entries] != [
    "phase3-manifest-correction-20260814", "phase3-gate-20260814"
]:
    raise SystemExit("candidate history order changed")
prefix = candidate[:candidate_entries[0][2]]
candidate_path.write_bytes(prefix + candidate_entries[0][1] + newline)
