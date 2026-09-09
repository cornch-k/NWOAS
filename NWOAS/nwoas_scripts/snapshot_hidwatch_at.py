#!/usr/bin/env python3
"""Create an immutable live-log snapshot after a verified uptime threshold."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import time


ERROR_FIELDS = (
    "whea_count",
    "cper_count",
    "bugcheck_count",
    "guest_exception_count",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def copy_prefix_atomic(source: Path, destination: Path, byte_count: int) -> None:
    temporary = destination.with_name(f".{destination.name}.tmp-{os.getpid()}")
    remaining = byte_count
    try:
        with source.open("rb") as reader, temporary.open("xb") as writer:
            while remaining:
                block = reader.read(min(1024 * 1024, remaining))
                if not block:
                    raise RuntimeError(
                        f"live log ended at {byte_count - remaining} of {byte_count} bytes"
                    )
                writer.write(block)
                remaining -= len(block)
            writer.flush()
            os.fsync(writer.fileno())
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("state", type=Path)
    parser.add_argument("minimum_alive_seconds", type=int)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--poll-seconds", type=float, default=15.0)
    args = parser.parse_args()

    if args.minimum_alive_seconds < 0 or args.poll_seconds <= 0:
        parser.error("threshold must be nonnegative and poll interval must be positive")
    if args.destination.exists():
        raise SystemExit(f"refusing to overwrite snapshot: {args.destination}")

    while True:
        state = json.loads(args.state.read_text())
        if not state.get("runner_alive"):
            raise SystemExit("runner stopped before snapshot threshold")
        errors = {field: int(state.get(field, 0)) for field in ERROR_FIELDS}
        if any(errors.values()):
            raise SystemExit(f"guest error before snapshot threshold: {errors}")
        alive = int(state.get("last_alive_seconds", 0))
        if alive >= args.minimum_alive_seconds:
            source = Path(state["log"])
            byte_count = source.stat().st_size
            args.destination.parent.mkdir(parents=True, exist_ok=True)
            copy_prefix_atomic(source, args.destination, byte_count)
            result = {
                "snapshot": str(args.destination.resolve()),
                "bytes": args.destination.stat().st_size,
                "sha256": sha256(args.destination),
                "minimum_alive_seconds": args.minimum_alive_seconds,
                "observed_alive_seconds": alive,
                "verdict": state.get("verdict"),
                "errors": errors,
            }
            print(json.dumps(result, ensure_ascii=False, sort_keys=True), flush=True)
            return 0
        time.sleep(args.poll_seconds)


if __name__ == "__main__":
    raise SystemExit(main())
