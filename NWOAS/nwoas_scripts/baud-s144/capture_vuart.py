#!/usr/bin/env python3
"""Continuously capture the second m1n1 CDC pipe across re-enumeration."""

from __future__ import annotations

import argparse
import os
import signal
import time

import serial


running = True


def stop(*_args) -> None:
    global running
    running = False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    with open(args.output, "ab", buffering=0) as output:
        while running:
            if not os.path.exists(args.port):
                time.sleep(0.2)
                continue
            try:
                with serial.Serial(args.port, 115200, timeout=0.25) as device:
                    while running:
                        block = device.read(65536)
                        if block:
                            output.write(block)
            except (OSError, serial.SerialException):
                time.sleep(0.2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
