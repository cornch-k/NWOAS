#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Lifecycle adapted from paulsmir/windows-on-m1 tools/agx_m1n1_backend.py (MIT, see
# LICENSE.paulsmir-windows-on-m1) for the J274 Mac mini on the 13.5 stub + D4 m1n1.
"""One bounded AGX firmware-only cycle (G1) on the live D4 proxy.

  M1N1DEVICE=/dev/cu.usbmodem...NY1 M1N1_KEEP_BAUD=1 PYTHONPATH=m1n1_windows/proxyclient \
    m1n1_windows/.venv-hv/bin/python3 g1_cycle.py --contract CONTRACT.json --out EVIDENCE.json

Sequence (no render work, no NVMe, no display, no disk writes on the target):
  1. live ADT -> contract; must equal the reviewed contract byte for byte (canonical form)
  2. python heap must not overlap the GPU/firmware RAM regions from the ADT
  3. select the V13_5/G13 construct schema (Ver.set_version)
  4. pmgr_adt_clocks_enable(/arm-io/gfx-asc), (/arm-io/sgx)
  5. AGX(u); agx.start()  (ASC boot, endpoints, initdata, DC_Init, DC_UpdateIdleTS)
  6. management ping -> Pong required within 1 s
  7. snapshot: ASC running, power states, firmware status, RegionC fault record, SGX IRQ counters, UAT
  8. agx.stop(); clear context-0 UAT roots; flush; invalidate
Every step has a hard SIGALRM deadline. Nothing is retried. Once clocks were requested the
result is "release_requires_reboot": only a reboot restores cold hardware state.
Exit 0 = cycle passed, 1 = cycle failed (evidence written), 2 = refused before touching clocks.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import traceback

sys.path.insert(0, str(Path(__file__).resolve().parent))
import j274_agx_common as C  # noqa: E402

PONG_DEADLINE_S = 1.0


def _reg(value):
    if value is None:
        return None
    for attr in ("val", "value"):
        if hasattr(value, attr):
            return int(getattr(value, attr))
    try:
        return int(value)
    except (TypeError, ValueError):
        return str(value)


class Cycle:
    def __init__(self, u, contract):
        self.u, self.contract = u, contract
        self.agx = None
        self.clocked = False
        self.started = False
        self.pongs = 0
        self.ev = {"steps": []}

    def step(self, name, **data):
        rec = {"step": name, "t": round(time.monotonic() - self.t0, 4), **data}
        self.ev["steps"].append(rec)
        print(f"[G1] {name} {json.dumps(data, default=str) if data else ''}", flush=True)

    # 1-2: refuse before any hardware state change
    def validate(self, d4_image):
        u = self.u
        live = C.extract_contract(u.adt, m1n1_build=C.m1n1_build_string(d4_image),
                                  host_stack=C.host_stack_sha256())
        if C.comparable_bytes(live) != C.comparable_bytes(self.contract):
            raise C.ContractError("live AGX resources differ from the contract: "
                                  + "; ".join(C.diff_contracts(self.contract, live)[:8]))
        C.check_heap_overlap(int(u.heap_base), int(u.heap_top), C.protected_ranges(u.adt))
        self.step("validated", heap=[hex(int(u.heap_base)), hex(int(u.heap_top))],
                  contract_sha256=C.contract_sha256(self.contract))

    def prepare(self):
        from m1n1.constructutils import Ver
        from m1n1.agx import AGX
        Ver.set_version(self.u)
        self.step("schema", V=Ver._version.get("V"), G=Ver._version.get("G"))
        if (Ver._version.get("V"), Ver._version.get("G")) != (C.FIRMWARE_VERSION, C.FIRMWARE_GENERATION):
            raise C.ContractError("construct schema is not V13_5/G13")
        self.clocked = True             # from here on only a reboot proves release
        for path in C.CLOCK_PATHS:
            self.u.proxy.pmgr_adt_clocks_enable(path)
        self.step("clocks_enabled", paths=list(C.CLOCK_PATHS))
        self.agx = AGX(self.u)
        mgmt = self.agx.asc.mgmt
        original = mgmt.msghandler.get(4)
        if original is None:
            raise RuntimeError("AGX management endpoint has no Pong handler")

        def tracked(msg):
            r = original(msg)
            self.pongs += 1
            return r
        mgmt.msghandler[4] = tracked
        self.step("agx_constructed")

    def start(self):
        self.started = True
        self.agx.start()
        self.step("firmware_started", asc_running=bool(self.agx.asc.is_running()))

    def heartbeat(self):
        before = self.pongs
        ev0 = int(self.agx.event_mgr.event_count)
        t = time.monotonic()
        with C.deadline(PONG_DEADLINE_S, "management Pong"):
            self.agx.asc.mgmt.ping()
            while self.pongs <= before:
                self.agx.asc.work()
        dt = time.monotonic() - t
        self.step("pong", latency_s=round(dt, 4), pongs=self.pongs,
                  event_delta=int(self.agx.event_mgr.event_count) - ev0)
        return dt

    def snapshot(self, reason):
        a = self.agx
        mgmt = a.asc.mgmt
        status = {}
        fs = getattr(a, "fw_status", None)
        for name in ("halted", "halt_count", "resume"):
            if fs is not None and hasattr(fs, name):
                status[name] = _reg(getattr(fs, name))
        fault = {}
        try:
            region = a.initdata.regionC
            region.pull()
            f = region.fault_info
            fault = {n: int(getattr(f, n)) for n in ("unk_0", "unk_4", "queue_uuid", "unk_c", "unk_10", "unk_14")}
        except Exception as exc:          # record, do not fail the cycle on a decode gap
            fault = {"error": f"{type(exc).__name__}: {exc}"}
        uat = a.uat
        snap = {
            "reason": reason,
            "asc_running": bool(a.asc.is_running()),
            "iop_power_state": int(mgmt.iop_power_state),
            "ap_power_state": int(mgmt.ap_power_state),
            "fw_status": status,
            "fault": fault,
            "sgx_irqs": [int(v) for v in a.get_irqs()],
            "uat": {"initialized": bool(uat.initialized),
                    "cached_roots": sorted(int(v) for v in uat.pt_cache),
                    "dirty_roots": sorted(int(v) for v in uat.dirty)},
        }
        self.step("snapshot", **snap)
        return snap

    def stop_and_clear(self):
        if self.started:
            self.agx.stop()
            self.started = False
            self.step("firmware_stopped")
        uat = self.agx.uat
        with uat.handoff.lock():
            uat.set_l0(0, 0, 0)
            uat.set_l0(0, 1, 0)
            uat.flush_dirty()
            uat.invalidate_cache()
        self.step("uat_roots_cleared")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--contract", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True, help="evidence JSON (must not exist)")
    ap.add_argument("--review", type=Path, help="j274-agx-review.json (D4 marker check)")
    args = ap.parse_args(argv)
    if args.out.exists():
        print(f"STOP: {args.out} exists (evidence is never reused)", file=sys.stderr)
        return 2
    contract = C.load_contract(args.contract)
    d4_image = C.D4_FILE.read_bytes()
    if hashlib.md5(d4_image).hexdigest() != C.D4_MD5:
        print("STOP: local D4 image MD5 mismatch", file=sys.stderr)
        return 2
    review = json.loads(args.review.read_text()) if args.review else None

    cyc = None
    rc = 1
    ev = {"tool": "g1_cycle.py", "contract": str(args.contract),
          "contract_sha256": C.contract_sha256(contract), "started_at": time.time()}
    try:
        with C.deadline(60, "connect"):
            iface, p, u = C.connect_proxy()
        cyc = Cycle(u, contract)
        cyc.t0 = time.monotonic()
        ev["identity"] = {"m1n1_base": hex(int(u.base)),
                          "random_seed_sha256": C.random_seed_sha256(u.adt),
                          "os_firmware": C.os_firmware_strings(u.adt)}
        if review:
            ev["identity"]["m1n1_markers"] = C.read_live_markers(u, review)
            if not all(m["match"] for m in ev["identity"]["m1n1_markers"]):
                raise C.ContractError("running m1n1 is not D4 (marker mismatch)")
        with C.deadline(60, "validate"):
            cyc.validate(d4_image)
        try:
            with C.deadline(30, "prepare"):
                cyc.prepare()
            with C.deadline(120, "start"):
                cyc.start()
            latency = cyc.heartbeat()
            with C.deadline(30, "snapshot"):
                ev["snapshot"] = cyc.snapshot("after_pong")
            ev["pong_latency_s"] = latency
        finally:
            if cyc.agx is not None:
                with C.deadline(60, "stop+clear"):
                    cyc.stop_and_clear()
        rc = 0
    except C.ContractError as exc:
        ev["refused"] = str(exc)
        rc = 2 if (cyc is None or not cyc.clocked) else 1
    except BaseException as exc:            # includes DeadlineExceeded
        ev["error"] = f"{type(exc).__name__}: {exc}"
        ev["traceback"] = traceback.format_exc(limit=6)
        rc = 1
    finally:
        if cyc is not None:
            ev["steps"] = cyc.ev["steps"]
            ev["clocks_requested"] = cyc.clocked
            ev["firmware_left_running"] = cyc.started
        ev["release_requires_reboot"] = bool(cyc is not None and cyc.clocked)
        ev["result"] = {0: "PASS", 1: "FAIL", 2: "REFUSED"}[rc]
        ev["finished_at"] = time.time()
        args.out.parent.mkdir(parents=True, exist_ok=True)
        C.atomic_json(args.out, ev)
        print(f"[G1] RESULT {ev['result']} evidence {args.out}", flush=True)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
