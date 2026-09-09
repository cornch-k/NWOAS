#!/usr/bin/env python3
"""Measure m1n1 USB CDC proxy latency at nominal line codings.

The J274 direct USB link uses CDC bulk endpoints.  The baud number programmed by
pyserial is only CDC ACM line-coding metadata, but measuring both values proves
whether it affects the host/target path.  This script issues NOP requests only;
it does not access target memory, NVMe, or boot the guest.  Large MEMREAD tests
are deliberately excluded because S144 found they can reset the current DWC3
device while its IN queue is being flushed.
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import time
from pathlib import Path

from m1n1.proxy import M1N1Proxy, UartInterface
from m1n1.proxyutils import bootstrap_port


def percentile(values: list[float], fraction: float) -> float:
    values = sorted(values)
    return values[min(len(values) - 1, int((len(values) - 1) * fraction))]


def measure(port: str, baud: int, rounds: int) -> dict:
    iface = UartInterface(f"{port}:{baud}")
    iface.dev.timeout = 3
    iface.dev.write_timeout = 3
    proxy = M1N1Proxy(iface, debug=False)
    result = {"requested_line_coding": baud, "rounds": rounds}

    try:
        bootstrap_port(iface, proxy)
        whoami = proxy.iodev_whoami()
        result["iodev"] = str(whoami)
        result["effective_host_line_coding"] = iface.dev.baudrate

        nop_times = []
        for _ in range(rounds):
            started = time.perf_counter()
            iface.nop()
            nop_times.append(time.perf_counter() - started)
        result["nop_ms_median"] = statistics.median(nop_times) * 1000
        result["nop_ms_p90"] = percentile(nop_times, 0.90) * 1000

    finally:
        iface.dev.close()

    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbmodemC07HL05SQ6NY1")
    parser.add_argument("--rounds", type=int, default=30)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.rounds < 1 or args.rounds > 100:
        parser.error("--rounds must be between 1 and 100")
    if not os.path.exists(args.port):
        parser.error(f"missing CDC port: {args.port}")

    report = {
        "experiment": "S144 USB CDC nominal line-coding comparison",
        "port": args.port,
        "storage_writes": False,
        "results": [],
    }
    for baud in (115200, 1500000):
        report["results"].append(measure(args.port, baud, args.rounds))
        time.sleep(0.25)

    low, high = report["results"]
    ratio = low["nop_ms_median"] / high["nop_ms_median"]
    report["nop_speed_ratio_1500000_over_115200"] = ratio
    report["conclusion"] = (
        "CDC line coding materially affects proxy latency"
        if ratio > 1.25
        else "CDC line coding is nominal; proxy latency is unchanged"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
