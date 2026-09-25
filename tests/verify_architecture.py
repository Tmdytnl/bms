#!/usr/bin/env python3
"""Verify the distributable APP package and its layer dependency boundary."""

from __future__ import annotations

import hashlib
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "APP"
FAILURES: list[str] = []


def check(condition: bool, description: str) -> None:
    if condition:
        print(f"PASS: {description}")
    else:
        print(f"FAIL: {description}")
        FAILURES.append(description)


def source_without_comments(path: Path) -> str:
    source = path.read_text(encoding="utf-8", errors="replace")
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)


def layer_sources(layer: str) -> list[Path]:
    return sorted(
        path for path in (APP / layer).glob("*")
        if path.suffix.lower() in {".c", ".h"}
    )


required_layers = {"apl", "fml", "bsp", "os", "RTD", "bms_main", "keil"}
actual_layers = {path.name for path in APP.iterdir() if path.is_dir()}
check(actual_layers == required_layers,
      "APP contains the seven agreed production directories")
check({path.name for path in (APP / "bms_main").iterdir()} == {"main.c"},
      "bms_main contains only main.c")
check(not any(path.is_dir() for path in (APP / "fml").iterdir()),
      "FML is flat and has no per-module directory hierarchy")
check(not any(path.is_dir() for path in (APP / "apl").iterdir()),
      "APL task sources are flat")
check(not any(path.is_dir() for path in (APP / "bsp").iterdir()),
      "project-owned BSP drivers are flat")
check(not any(path.suffix.lower() in {".md", ".pdf", ".docx"}
              for path in APP.rglob("*")),
      "APP contains no project documents")

project_path = APP / "keil" / "BMS_V1.uvprojx"
try:
    project = ET.parse(project_path)
    project_files = [
        (project_path.parent / (node.text or "").replace("\\", "/")).resolve()
        for node in project.findall(".//FilePath")
    ]
    check(bool(project_files) and all(path.is_file() and path.is_relative_to(APP)
                                      for path in project_files),
          "every Keil source is an existing file inside APP")
    include_nodes = project.findall(".//IncludePath")
    include_dirs = [
        (project_path.parent / value.replace("\\", "/")).resolve()
        for node in include_nodes for value in (node.text or "").split(";") if value
    ]
    check(bool(include_dirs) and
          all(path.is_dir() and path.is_relative_to(APP) for path in include_dirs),
          "every Keil include directory is inside APP")
except (ET.ParseError, OSError, ValueError) as exc:
    check(False, f"Keil project is readable: {exc}")

fml = layer_sources("fml")
bsp = layer_sources("bsp")
apl = layer_sources("apl")
os_sources = layer_sources("os")

native_os = re.compile(
    r'#\s*include\s*[<"](?:FreeRTOS|task|queue|semphr|event_groups)\.h[>"]'
    r'|\b(?:xTask\w*|vTask\w*|xQueue\w*|vQueue\w*|xSemaphore\w*|'
    r'xEventGroup\w*|vEventGroup\w*|taskENTER_CRITICAL|taskEXIT_CRITICAL|'
    r'portYIELD_FROM_ISR)\s*\('
)
check(not any(native_os.search(source_without_comments(path))
              for path in (*fml, *bsp, *apl)),
      "project layers use OS_* instead of native FreeRTOS")
check(not any(re.search(r'#\s*include\s*"(?:stm32f10x|misc\.h)',
                        source_without_comments(path))
              for path in fml),
      "FML uses BSP interfaces without including STM32 hardware headers")
check(not any(re.search(r'#\s*include\s*"(?:fml_|apl_)',
                        source_without_comments(path))
              for path in (*bsp, *os_sources)),
      "BSP and OS do not depend on application or functional headers")
check(not any(re.search(r'\bBMS_[A-Z][a-z]\w*\s*\(',
                        source_without_comments(path))
              for path in fml),
      "project-owned FML functions have the FML_ prefix")
check(not any(re.search(r'\b(?:BQ76940|SoftI2C)_[A-Z][a-z]\w*\s*\(',
                        source_without_comments(path))
              for path in bsp),
      "project-owned BSP driver functions have the BSP_ prefix")

vendor_roots = (
    (ROOT / "docs/reference/ST/STM32F10x Standard Peripheral Library",
     APP / "RTD/ST"),
    (ROOT / "docs/FreeRTOS", APP / "os/FreeRTOS"),
)
vendor_unchanged = True
for original_root, package_root in vendor_roots:
    for package_file in package_root.rglob("*"):
        if not package_file.is_file():
            continue
        original = original_root / package_file.relative_to(package_root)
        if not original.is_file() or hashlib.sha256(package_file.read_bytes()).digest() != \
                hashlib.sha256(original.read_bytes()).digest():
            vendor_unchanged = False
            break
check(vendor_unchanged, "copied third-party source is byte-for-byte unchanged")

if FAILURES:
    print(f"APP ARCHITECTURE FAIL ({len(FAILURES)} checks)")
    sys.exit(1)
print("APP ARCHITECTURE PASS")
