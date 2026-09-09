#!/usr/bin/env python3
"""Verify every immutable artifact in an NWOAS runtime manifest."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    manifest = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (
        root / "NWOAS-RUNTIME-MANIFEST-2026-09-07.json"
    )
    data = json.loads(manifest.read_text())
    if data.get("schema") != 1:
        raise SystemExit(f"FAIL unsupported schema: {data.get('schema')!r}")

    checked = 0
    failures: list[str] = []
    for section in ("artifacts", "evidence_logs"):
        entries = data.get(section)
        if not isinstance(entries, list):
            failures.append(f"{section}: missing list")
            continue
        for entry in entries:
            rel = entry.get("path")
            path = root / rel if isinstance(rel, str) else None
            if path is None or not path.is_file():
                failures.append(f"{section}: missing {rel!r}")
                continue
            actual_size = path.stat().st_size
            if actual_size != entry.get("bytes"):
                failures.append(
                    f"{rel}: size {actual_size} != {entry.get('bytes')}"
                )
                continue
            actual_hash = sha256(path)
            if actual_hash != entry.get("sha256"):
                failures.append(
                    f"{rel}: sha256 {actual_hash} != {entry.get('sha256')}"
                )
                continue
            checked += 1

    completion = data.get("completion", {})
    if completion.get("physical_cursor_movement_observed") and not completion.get(
        "physical_hid_report_observed"
    ):
        failures.append("completion: cursor=true requires physical_hid_report=true")

    monitor = data.get("live_monitor", {})
    mutable_log = monitor.get("mutable_log")
    if mutable_log and not (root / mutable_log).is_file():
        failures.append(f"live_monitor: mutable log missing: {mutable_log}")

    if failures:
        print("FAIL runtime manifest")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(f"PASS runtime manifest: {checked} immutable files")
    print(f"manifest_sha256={sha256(manifest)}")
    if mutable_log:
        print(f"live_log={mutable_log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
