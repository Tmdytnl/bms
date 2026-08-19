# Firmware Tests

The numbered `verify_phase1.py` through `verify_phase7.py` files are historical
phase-checkpoint verifiers. Their source hashes intentionally describe the
checkpoint at which each phase report was written; later-phase changes can make
an earlier checkpoint verifier report drift.

For the current Codex-reviewed Phase 7 candidate, run this from the repository
root on Windows with Python 3, ARMCC5 5.06u7, and Keil uVision 5.38 installed
at their default paths (or pass the runner's explicit path parameters):

```powershell
& firmware\Tests\build_phase7_review.ps1
```

The no-switch command above is the authoritative full gate. The `Skip*`
switches are diagnostics only and deliberately do not produce a verifier PASS.
The script performs a production Keil Clean/Rebuild, builds fresh Phase 4,
Phase 6, and combined Phase 5/7 ARMCC5 test images, executes all three images in
the Keil Simulator, and runs `verify_phase7_review.py`. Generated objects and
AXFs remain ignored; committed plain-text build, map, simulator, and verifier
outputs under `Build/Phase7Review` are the review evidence.

Maps/logs embed checkout paths and timestamps. The chain is semantically
reproducible from a fresh clone in the stated environment; byte-identical
cross-directory hashes are not promised.

The Simulator exercises production C with deterministic transport/RTOS/BSP
fakes where hardware access is required. It is not target-board or BQ7694003
hardware validation.
