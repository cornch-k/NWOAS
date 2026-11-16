# SPDX-License-Identifier: MIT
"""Shared, mostly pure helpers for the J274 (Mac mini M1) AGX G0/G1 gate.

Adapted from paulsmir/windows-on-m1 @ 6c65673 (MIT, see
LICENSE.paulsmir-windows-on-m1):
  tools/agx_contract.py        -> validate_contract / canonical_bytes
  tools/agx_inventory.py       -> extract_contract
  tools/agx_live_inventory.py  -> json_value / node_record / adt identity
Changes for NWOAS:
  * platform J274 instead of J313; firmware pinned to V13_5/G13 only;
  * the J313 source pins (root/m1n1/Mu git commits) are replaced by the
    identities that actually exist here: the installed raw m1n1 "D4" (file MD5,
    build string, in-memory marker bytes), the /chosen firmware strings and a
    digest of the host-side python AGX stack;
  * extraction works on an m1n1.adt.ADTNode directly, so the same code runs
    on a saved ADT blob (offline) and on u.adt (live);
  * a companion "review" record pins the pmgr power chain, perf states and
    m1n1 marker bytes, and lists memory ranges for an overlap check.

Nothing in this module opens a serial port.  connect_proxy() does, and only
when a caller invokes it.
"""

from contextlib import contextmanager
from collections.abc import Mapping
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import struct
import sys
import time
from types import MappingProxyType

HERE = Path(__file__).resolve().parent
NWOAS = HERE.parents[1]
PROXYCLIENT = NWOAS / "m1n1_windows" / "proxyclient"
if str(PROXYCLIENT) not in sys.path:
    sys.path.insert(0, str(PROXYCLIENT))

PLATFORM = "J274"
FIRMWARE_GENERATION = "G13"
FIRMWARE_VERSION = "V13_5"
CHIP_ID = 0x8103
BOARD_ID = 0x22
REQUIRED_PATHS = ("/arm-io/sgx", "/arm-io/gfx-asc")
CLOCK_PATHS = ("/arm-io/gfx-asc", "/arm-io/sgx")   # same order as paulsmir

# Installed raw m1n1 "D4" (recovery-tahoe-proxy-20260906/m1n1-bare.bin).
D4_FILE = NWOAS / "recovery-tahoe-proxy-20260906" / "m1n1-bare.bin"
D4_MD5 = "7bec0225edc101857be3b7f195dd7357"
D4_MARKERS = (b"##m1n1_ver##", b"NWOAS-DCP-DEFER")

# Host python files whose behaviour the G1 cycle depends on.
HOST_STACK_FILES = (
    "m1n1/adt.py", "m1n1/constructutils.py", "m1n1/malloc.py",
    "m1n1/proxy.py", "m1n1/proxyutils.py", "m1n1/utils.py",
    "m1n1/hw/uat.py", "m1n1/hw/agx.py", "m1n1/hw/asc.py",
    "m1n1/fw/asc/__init__.py", "m1n1/fw/asc/base.py", "m1n1/fw/asc/mgmt.py",
    "m1n1/fw/asc/crash.py", "m1n1/fw/asc/syslog.py", "m1n1/fw/asc/oslog.py",
    "m1n1/fw/asc/ioreporting.py", "m1n1/fw/asc/kdebug.py",
    "m1n1/fw/agx/__init__.py", "m1n1/fw/agx/channels.py",
    "m1n1/fw/agx/cmdqueue.py", "m1n1/fw/agx/handoff.py",
    "m1n1/fw/agx/initdata.py", "m1n1/fw/agx/microsequence.py",
    "m1n1/agx/__init__.py", "m1n1/agx/channels.py", "m1n1/agx/context.py",
    "m1n1/agx/event.py", "m1n1/agx/initdata.py", "m1n1/agx/object.py",
    "m1n1/agx/shim.py", "m1n1/agx/uapi.py", "m1n1/agx/render.py",
)


class ContractError(ValueError):
    """The AGX contract or a live resource does not match."""


# --------------------------------------------------------------------------
# Contract (schema identical to paulsmir config/j313-agx.json except `source`)
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Region:
    base: int
    size: int


@dataclass(frozen=True)
class AgxContract:
    contract_version: int
    platform: str
    source: Mapping
    firmware: Mapping
    nodes: tuple
    regions: Mapping
    interrupts: tuple
    dependencies: tuple
    uat: Mapping


TOP_LEVEL = {"contract_version", "platform", "source", "firmware", "nodes",
             "regions", "interrupts", "dependencies", "uat"}
REGION_NAMES = {"sgx_mmio", "asc_mmio", "rtkit_private", "gpu", "shared",
                "handoff"}
SOURCE_KEYS = {"adt_identity", "m1n1_md5", "m1n1_build", "os_firmware_version",
               "system_firmware_version", "host_agx_stack_sha256"}
FIRMWARE_KEYS = {"generation", "version"}
UAT_KEYS = {"page_size", "num_contexts", "address_bits"}
HEX32 = re.compile(r"[0-9a-f]{32}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
ALIGNMENT = 0x4000
U64_LIMIT = 1 << 64


def _exact(value, keys, where):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ContractError(f"{where} keys must be exactly {sorted(keys)}")


def _integer(value, where, *, minimum=0, maximum=U64_LIMIT - 1):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ContractError(f"{where} is not an integer")
    if not minimum <= value <= maximum:
        raise ContractError(f"{where} is outside {minimum:#x}..{maximum:#x}")
    return value


def _string(value, where):
    if not isinstance(value, str) or not value:
        raise ContractError(f"{where} must be a non-empty string")
    return value


def _path_list(value, where):
    if not isinstance(value, list) or not value:
        raise ContractError(f"{where} must be a non-empty list")
    result = tuple(_string(item, f"{where} item") for item in value)
    if any(not item.startswith("/") for item in result):
        raise ContractError(f"{where} entries must be absolute ADT paths")
    if len(set(result)) != len(result):
        raise ContractError(f"{where} entries must be unique")
    return result


def _region(value, name):
    _exact(value, {"base", "size"}, f"regions.{name}")
    base = _integer(value["base"], f"regions.{name}.base", minimum=ALIGNMENT)
    size = _integer(value["size"], f"regions.{name}.size", minimum=ALIGNMENT)
    if base % ALIGNMENT or size % ALIGNMENT:
        raise ContractError(f"regions.{name} must be 16 KiB aligned")
    if base + size > U64_LIMIT:
        raise ContractError(f"regions.{name} wraps the 64-bit address space")
    return Region(base, size)


def validate_contract(data: dict) -> AgxContract:
    """Validate an untrusted dict; J274 + V13_5/G13 only, every field exact."""
    _exact(data, TOP_LEVEL, "contract")
    version = _integer(data["contract_version"], "contract_version",
                       minimum=2, maximum=2)
    if data["platform"] != PLATFORM:
        raise ContractError(f"platform must be {PLATFORM}, got {data['platform']!r}")

    src = data["source"]
    _exact(src, SOURCE_KEYS, "source")
    for key in SOURCE_KEYS:
        _string(src[key], f"source.{key}")
    if not HEX64.fullmatch(src["adt_identity"]):
        raise ContractError("source.adt_identity must be 64 lowercase hex digits")
    if not HEX64.fullmatch(src["host_agx_stack_sha256"]):
        raise ContractError("source.host_agx_stack_sha256 must be 64 hex digits")
    if not HEX32.fullmatch(src["m1n1_md5"]):
        raise ContractError("source.m1n1_md5 must be 32 lowercase hex digits")
    if src["m1n1_md5"] != D4_MD5:
        raise ContractError("source.m1n1_md5 is not the installed D4 m1n1")

    fw = data["firmware"]
    _exact(fw, FIRMWARE_KEYS, "firmware")
    if fw["generation"] != FIRMWARE_GENERATION or fw["version"] != FIRMWARE_VERSION:
        raise ContractError(
            f"firmware must be {FIRMWARE_VERSION}/{FIRMWARE_GENERATION}, got "
            f"{fw.get('version')!r}/{fw.get('generation')!r}")

    _exact(data["regions"], REGION_NAMES, "regions")
    regions = {n: _region(data["regions"][n], n) for n in REGION_NAMES}
    sgx, asc = regions["sgx_mmio"], regions["asc_mmio"]
    if asc.base < sgx.base or asc.base + asc.size > sgx.base + sgx.size:
        raise ContractError("regions.asc_mmio must be inside sgx_mmio")
    ordered = sorted(((n, regions[n]) for n in
                      ("rtkit_private", "gpu", "shared", "handoff")),
                     key=lambda item: item[1].base)
    for (ln, lr), (rn, rr) in zip(ordered, ordered[1:]):
        if lr.base + lr.size > rr.base:
            raise ContractError(f"regions {ln} and {rn} overlap")

    irqs = data["interrupts"]
    if not isinstance(irqs, list) or not irqs:
        raise ContractError("interrupts must be a non-empty list")
    interrupts = tuple(_integer(v, "interrupt", minimum=32, maximum=1019)
                       for v in irqs)
    if len(set(interrupts)) != len(interrupts):
        raise ContractError("interrupts must be unique")

    uat = data["uat"]
    _exact(uat, UAT_KEYS, "uat")
    _integer(uat["page_size"], "uat.page_size", minimum=ALIGNMENT,
             maximum=ALIGNMENT)
    _integer(uat["num_contexts"], "uat.num_contexts", minimum=1, maximum=256)
    _integer(uat["address_bits"], "uat.address_bits", minimum=36, maximum=48)

    return AgxContract(
        contract_version=version,
        platform=PLATFORM,
        source=MappingProxyType(dict(src)),
        firmware=MappingProxyType(dict(fw)),
        nodes=_path_list(data["nodes"], "nodes"),
        regions=MappingProxyType(regions),
        interrupts=interrupts,
        dependencies=_path_list(data["dependencies"], "dependencies"),
        uat=MappingProxyType(dict(uat)),
    )


def contract_dict(c: AgxContract) -> dict:
    return {
        "contract_version": c.contract_version,
        "platform": c.platform,
        "source": dict(sorted(c.source.items())),
        "firmware": dict(sorted(c.firmware.items())),
        "nodes": list(c.nodes),
        "regions": {n: {"base": r.base, "size": r.size}
                    for n, r in sorted(c.regions.items())},
        "interrupts": list(c.interrupts),
        "dependencies": list(c.dependencies),
        "uat": dict(sorted(c.uat.items())),
    }


def canonical_bytes(c: AgxContract) -> bytes:
    return (json.dumps(contract_dict(c), indent=2, sort_keys=True) + "\n").encode()


def comparable_bytes(c: AgxContract) -> bytes:
    """ADT traversal order is the only canonicalized field (as paulsmir)."""
    d = contract_dict(c)
    d["nodes"] = sorted(d["nodes"])
    return (json.dumps(d, indent=2, sort_keys=True) + "\n").encode()


def contract_sha256(c: AgxContract) -> str:
    return hashlib.sha256(canonical_bytes(c)).hexdigest()


def load_contract(path) -> AgxContract:
    path = Path(path)
    try:
        raw = path.read_bytes()
        data = json.loads(raw)
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(str(exc)) from exc
    contract = validate_contract(data)
    if raw != canonical_bytes(contract):
        raise ContractError(f"contract {path} is not canonical")
    return contract


def diff_contracts(expected: AgxContract, live: AgxContract) -> list:
    """Human readable list of differing fields (for refusal messages)."""
    a, b = contract_dict(expected), contract_dict(live)
    a["nodes"], b["nodes"] = sorted(a["nodes"]), sorted(b["nodes"])
    out = []

    def walk(x, y, where):
        if isinstance(x, dict) and isinstance(y, dict):
            for k in sorted(set(x) | set(y)):
                walk(x.get(k), y.get(k), f"{where}.{k}" if where else k)
        elif x != y:
            out.append(f"{where}: contract={x!r} live={y!r}")
    walk(a, b, "")
    return out


# --------------------------------------------------------------------------
# ADT decoding (works on a saved blob or on the live u.adt)
# --------------------------------------------------------------------------

def text(value):
    if isinstance(value, bytes):
        return value.split(b"\0", 1)[0].decode("ascii", "replace")
    return str(value)


def json_value(value):
    """Deterministic JSON-safe form of a decoded ADT value (from paulsmir)."""
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, bytes):
        return {"bytes_hex": value.hex()}
    if isinstance(value, Mapping):
        return {str(k): json_value(v) for k, v in value.items()
                if not str(k).startswith("_")}
    if all(hasattr(value, f) for f in ("phandle", "name", "args")):
        return {"phandle": int(value.phandle), "name": str(value.name),
                "args": [int(i) for i in value.args]}
    if isinstance(value, (list, tuple)):
        return [json_value(i) for i in value]
    try:
        return [json_value(i) for i in value]
    except TypeError as exc:
        raise TypeError(f"unsupported ADT value type {type(value).__name__}") from exc


def node_record(node):
    try:
        regs = [[int(b), int(s)] for b, s in
                (node.get_reg(i) for i in range(len(node.reg)))]
    except (AttributeError, KeyError, TypeError, ValueError, IndexError) as exc:
        raise ContractError(f"cannot decode reg for {node._path}") from exc
    return {
        "reg": regs,
        "interrupts": [int(v) for v in getattr(node, "interrupts", [])],
        "properties": {k: json_value(v) for k, v in sorted(node._properties.items())
                       if k != "name"},
    }


def os_firmware_strings(adt) -> dict:
    chosen = adt["/chosen"]
    return {
        "firmware_version": text(chosen.firmware_version),
        "system_firmware_version": text(chosen.system_firmware_version),
        "iboot_stage_one_tag": text(getattr(chosen, "iboot_stage_one_tag", "")),
        "iboot_stage_two_tag": text(getattr(chosen, "iboot_stage_two_tag", "")),
    }


def agx_schema_version(firmware_version: str):
    """Same mapping m1n1 uses for u.version (proxyutils.VERSION_MAP)."""
    from m1n1.proxyutils import VERSION_MAP
    return VERSION_MAP.get(firmware_version)


def adt_identity(adt) -> str:
    """paulsmir's identity: sha256 of target type, chip id, OS firmware."""
    fields = {"target_type": text(adt.target_type),
              "chip_id": int(adt["/chosen"].chip_id),
              "firmware_version": text(adt["/chosen"].firmware_version)}
    return hashlib.sha256(json.dumps(fields, sort_keys=True).encode()).hexdigest()


def random_seed_sha256(adt):
    seed = adt["/chosen"]._properties.get("random-seed")
    if not isinstance(seed, (bytes, bytearray)) or not seed:
        return None
    return hashlib.sha256(bytes(seed)).hexdigest()


def uat_geometry() -> dict:
    from m1n1.hw.uat import UAT
    return {"page_size": int(UAT.PAGE_SIZE),
            "num_contexts": int(UAT.NUM_CONTEXTS),
            "address_bits": int(UAT.L0_OFF + (UAT.L0_SIZE.bit_length() - 1))}


SGX_REGION_PROPS = {
    "rtkit_private": ("rtkit-private-vm-region-base", "rtkit-private-vm-region-size"),
    "gpu": ("gpu-region-base", "gpu-region-size"),
    "shared": ("gfx-shared-region-base", "gfx-shared-region-size"),
    "handoff": ("gfx-handoff-base", "gfx-handoff-size"),
}


def host_stack_sha256(root=PROXYCLIENT) -> str:
    h = hashlib.sha256()
    for rel in HOST_STACK_FILES:
        data = (Path(root) / rel).read_bytes()
        h.update(rel.encode() + b"\0" + hashlib.sha256(data).digest())
    return h.hexdigest()


def extract_contract(adt, *, m1n1_build: str, host_stack: str) -> AgxContract:
    """Build the contract from an ADT; refuses anything that is not J274/V13_5."""
    platform = text(adt.target_type)
    if platform != PLATFORM:
        raise ContractError(f"ADT target-type is {platform!r}, not {PLATFORM}")
    chosen = adt["/chosen"]
    if int(chosen.chip_id) != CHIP_ID:
        raise ContractError(f"chip-id {int(chosen.chip_id):#x} is not T8103")
    fws = os_firmware_strings(adt)
    schema = agx_schema_version(fws["firmware_version"])
    if schema is None:
        raise ContractError(
            f"OS firmware {fws['firmware_version']!r} has no m1n1 AGX schema "
            f"(VERSION_MAP); the gate needs {FIRMWARE_VERSION} "
            f"(iBoot-8422.141.2, the macOS 13.5 stub). Refusing.")
    generation = text(adt["/arm-io"].getprop("soc-generation")).replace("H", "G")

    for path in REQUIRED_PATHS:
        try:
            adt[path]
        except KeyError as exc:
            raise ContractError(f"required ADT node {path} is missing") from exc
    sgx = node_record(adt["/arm-io/sgx"])
    asc = node_record(adt["/arm-io/gfx-asc"])
    if not sgx["reg"] or not asc["reg"]:
        raise ContractError("sgx and gfx-asc need at least one reg tuple")
    regions = {
        "sgx_mmio": {"base": sgx["reg"][0][0], "size": sgx["reg"][0][1]},
        "asc_mmio": {"base": asc["reg"][0][0], "size": asc["reg"][0][1]},
    }
    for name, (bprop, sprop) in SGX_REGION_PROPS.items():
        if bprop not in sgx["properties"] or sprop not in sgx["properties"]:
            raise ContractError(f"required sgx property {bprop}/{sprop} missing")
        regions[name] = {"base": sgx["properties"][bprop],
                         "size": sgx["properties"][sprop]}

    data = {
        "contract_version": 2,
        "platform": platform,
        "source": {
            "adt_identity": adt_identity(adt),
            "m1n1_md5": D4_MD5,
            "m1n1_build": m1n1_build,
            "os_firmware_version": fws["firmware_version"],
            "system_firmware_version": fws["system_firmware_version"],
            "host_agx_stack_sha256": host_stack,
        },
        "firmware": {"generation": generation, "version": schema},
        "nodes": list(REQUIRED_PATHS),
        "regions": regions,
        "interrupts": sgx["interrupts"] + asc["interrupts"],
        "dependencies": list(REQUIRED_PATHS),
        "uat": uat_geometry(),
    }
    return validate_contract(data)


# --------------------------------------------------------------------------
# Review record: pinned facts outside the paulsmir schema
# --------------------------------------------------------------------------

def pmgr_chain(adt, path) -> list:
    """pmgr devices m1n1's pmgr_adt_power_enable(path) walks (gates+parents)."""
    pm = adt["/arm-io/pmgr"]
    by_id = {int(adt.pmgr_dev_get_id(d)): d for d in pm.devices}
    out, seen = [], set()

    def walk(dev_id):
        if dev_id in seen:
            return
        seen.add(dev_id)
        dev = by_id.get(dev_id)
        if dev is None:
            raise ContractError(f"{path}: pmgr device {dev_id} missing")
        parents = [int(p) for p in adt.pmgr_dev_get_parents(dev)]
        out.append({"id": dev_id, "name": str(dev.name), "parents": parents,
                    "psreg": int(dev.psreg), "psidx": int(dev.psidx)})
        for p in parents:
            if p:
                walk(p)
    for gate in adt[path].clock_gates:
        walk(int(gate))
    return out


def segment_ranges(asc_node) -> list:
    """Decode gfx-asc segment-ranges as 32-byte (phys, iova, remap, size, flags).

    HYPOTHESIS layout: consistent with the Tahoe J274 ADT, where entry 1's phys
    equals sgx gfx-data-base.  Fails closed if the length is not a multiple.
    """
    raw = asc_node._properties.get("segment-ranges")
    if not isinstance(raw, (bytes, bytearray)) or len(raw) % 32:
        raise ContractError("gfx-asc segment-ranges is not 32-byte records")
    out = []
    for off in range(0, len(raw), 32):
        phys, iova, remap, size, flags = struct.unpack_from("<QQQII", raw, off)
        out.append({"phys": phys, "iova": iova, "remap": remap, "size": size,
                    "flags": flags})
    return out


def m1n1_markers(image: bytes) -> list:
    """Marker strings (with their NUL) and their offsets inside the raw image.

    FACT (m1n1-raw.ld): _base is file offset 0 and P_GET_BASE returns _base,
    so the same bytes sit at u.base + offset in the running m1n1.
    """
    out = []
    for tag in D4_MARKERS:
        off = image.find(tag)
        if off < 0 or image.find(tag, off + 1) >= 0:
            raise ContractError(f"marker {tag!r} missing or not unique in m1n1 image")
        end = image.index(b"\0", off) + 1
        out.append({"offset": off, "bytes_hex": image[off:end].hex()})
    return out


def m1n1_build_string(image: bytes) -> str:
    off = image.find(b"##m1n1_ver##")
    end = image.index(b"\0", off)
    return image[off + len(b"##m1n1_ver##"):end].decode("ascii")


def review_record(adt, d4_image: bytes) -> dict:
    sgx, asc = adt["/arm-io/sgx"], adt["/arm-io/gfx-asc"]
    perf = json_value(sgx._properties.get("perf-states"))
    return {
        "review_version": 1,
        "platform": text(adt.target_type),
        "chip_id": int(adt["/chosen"].chip_id),
        "board_id": int(adt["/chosen"].board_id),
        "sgx_compatible": [str(c) for c in sgx.compatible],
        "aic_reg0": [int(v) for v in adt["/arm-io/aic"].get_reg(0)],
        "clock_ids": {p: [int(v) for v in getattr(adt[p], "clock_ids", [])]
                      for p in CLOCK_PATHS},
        "pmgr_chain": {p: pmgr_chain(adt, p) for p in CLOCK_PATHS},
        "perf_states_sha256": hashlib.sha256(
            json.dumps(perf, sort_keys=True).encode()).hexdigest(),
        "m1n1_markers": m1n1_markers(d4_image),
    }


def review_bytes(review: dict) -> bytes:
    return (json.dumps(review, indent=2, sort_keys=True) + "\n").encode()


def protected_ranges(adt) -> list:
    """Physical ranges the python heap must never overlap."""
    sgx, asc = adt["/arm-io/sgx"], adt["/arm-io/gfx-asc"]
    out = []
    for name, (bprop, sprop) in SGX_REGION_PROPS.items():
        if name == "rtkit_private":        # firmware VA, not physical
            continue
        out.append((name, int(getattr(sgx, bprop.replace("-", "_"))),
                    int(getattr(sgx, sprop.replace("-", "_")))))
    if "gfx-data-base" in sgx._properties:
        out.append(("gfx_data", int(sgx.gfx_data_base), int(sgx.gfx_data_size)))
    if "gfx-shared-l2-region-base" in sgx._properties:
        out.append(("shared_l2", int(sgx.gfx_shared_l2_region_base),
                    int(sgx.gfx_shared_l2_region_size)))
    for i, seg in enumerate(segment_ranges(asc)):
        out.append((f"asc_segment{i}", seg["phys"], seg["size"]))
    return out


def check_heap_overlap(heap_base: int, heap_top: int, ranges) -> None:
    for name, base, size in ranges:
        if base < heap_top and heap_base < base + size:
            raise ContractError(
                f"python heap {heap_base:#x}-{heap_top:#x} overlaps {name} "
                f"{base:#x}+{size:#x}")


# --------------------------------------------------------------------------
# Hard deadlines (SIGALRM, nested; inner never outlives outer)
# --------------------------------------------------------------------------

class DeadlineExceeded(Exception):
    pass


_deadline_stack = []


def _arm():
    if not _deadline_stack:
        signal.setitimer(signal.ITIMER_REAL, 0)
        return
    when, step = min(_deadline_stack)
    remaining = when - time.monotonic()
    signal.setitimer(signal.ITIMER_REAL, max(remaining, 0.001))


def _on_alarm(signum, frame):
    now = time.monotonic()
    expired = [s for w, s in _deadline_stack if w <= now + 0.0005]
    if expired:
        raise DeadlineExceeded(f"hard deadline exceeded: {expired[0]}")
    _arm()


@contextmanager
def deadline(seconds: float, step: str):
    if not seconds or seconds <= 0:
        raise ValueError("deadline must be positive")
    signal.signal(signal.SIGALRM, _on_alarm)
    entry = (time.monotonic() + float(seconds), f"{step} ({seconds:g}s)")
    _deadline_stack.append(entry)
    _arm()
    try:
        yield
    finally:
        _deadline_stack.remove(entry)
        _arm()


# --------------------------------------------------------------------------
# Live connection (no m1n1.setup: no HV object, no PMU write)
# --------------------------------------------------------------------------

def connect_proxy():
    """Open M1N1DEVICE exactly like m1n1.setup minus PMU/HV side effects."""
    if not os.environ.get("M1N1DEVICE"):
        raise ContractError("M1N1DEVICE is required")
    # Never let bootstrap_port change the baud rate / cpufreq on the target.
    os.environ["M1N1_KEEP_BAUD"] = "1"
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p)
    return iface, p, u


def read_live_markers(u, review) -> list:
    out = []
    for m in review["m1n1_markers"]:
        want = bytes.fromhex(m["bytes_hex"])
        got = u.iface.readmem(int(u.base) + int(m["offset"]), len(want))
        out.append({"offset": m["offset"], "match": got == want,
                    "got_hex": got.hex()})
    return out


def live_identity(u, review) -> dict:
    """Per-boot identity: m1n1 base + /chosen random-seed digest + markers."""
    adt = u.adt
    markers = read_live_markers(u, review)
    return {
        "platform": text(adt.target_type),
        "firmware": u.version,
        "os_firmware": os_firmware_strings(adt),
        "m1n1_base": int(u.base),
        "random_seed_sha256": random_seed_sha256(adt),
        "m1n1_markers": markers,
        "m1n1_markers_ok": all(m["match"] for m in markers),
    }


def atomic_json(path, data) -> None:
    path = Path(path)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True, default=str) + "\n")
    tmp.replace(path)


class CappedStream:
    """stdout replacement: writes to a file, stops after `limit` bytes."""

    def __init__(self, path, limit=16 << 20):
        self.f = open(path, "a", buffering=1)
        self.left = limit
        self.capped = False

    def write(self, s):
        if self.left <= 0:
            if not self.capped:
                self.f.write("\n[log capped]\n")
                self.capped = True
            return len(s)
        chunk = s[: self.left]
        self.left -= len(chunk)
        self.f.write(chunk)
        return len(s)

    def flush(self):
        self.f.flush()

    def isatty(self):
        return False
