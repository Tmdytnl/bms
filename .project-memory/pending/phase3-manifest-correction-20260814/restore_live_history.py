from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
live_path = root / ".project-memory" / "PROJECT_LOG.md"
candidate_path = Path(__file__).with_name("PROJECT_LOG.candidate.md")

live = live_path.read_bytes()
candidate = candidate_path.read_bytes()
newline = b"\r\n" if b"\r\n" in live else b"\n"
live_first = re.search(br"(?m)^### 2026-08-14T05:35:56Z \| phase3-gate-20260814 \|", live)
if live_first is None:
    raise SystemExit("live Phase 3 entry changed")
if b"phase2-gate-20260814" not in live[live_first.start():]:
    raise SystemExit("live Phase 2 entry missing")
if b"phase3-manifest-correction-20260814" not in candidate:
    raise SystemExit("correction entry missing")
candidate_path.write_bytes(
    candidate.rstrip(b"\r\n") + newline + newline + live[live_first.start():]
)
