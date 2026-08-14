# Phase 1 Tests

These checks cover only the Phase 1 project boundary and public model. They do
not test BQ CRC, software I2C, VC mapping, CAN CRC, SOC integration or hardware.

Run the deterministic repository check from the repository root:

```powershell
python firmware\Tests\verify_phase1.py
```

`test_phase1_models.c` is a portable assertion harness for the model defaults,
state values, fault-bit uniqueness and memory constants. It is compiled by
ARMCC5 as a separate compatibility check and is intentionally not linked into
the firmware target. Running it requires a suitable host or target test runner;
compilation alone is not reported as execution.
