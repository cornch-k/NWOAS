# dump_ptio.py (NWOAS) — DIAGNOSTIC. Dump the ADT /defaults pmap-io-ranges so we can
# map EXACTLY the ranges m1n1 marks as Device-nGnRE (flags>>28 == 8), instead of the
# whole coarse APPLE_PCIE_MMIO_RANGE_6 (1GB) which broke the early boot.
from m1n1.adt import load_adt

try:
    adt = load_adt(u.get_adt())
    node = adt["/defaults"]
    ptio = node.getprop("pmap-io-ranges")
    print("[ptio] TYPE", type(ptio).__name__, "repr:", repr(ptio)[:400])
    try:
        seq = list(ptio)
    except Exception as e:
        seq = []
        print("[ptio] not iterable:", e)
    print("[ptio] COUNT", len(seq))
    for i, r in enumerate(seq):
        a = getattr(r, 'addr', None); s = getattr(r, 'size', None); f = getattr(r, 'flags', None)
        if a is None:
            print(f"[ptio] {i}: {r!r}")
        else:
            t = "nGnRE" if (f is not None and (f >> 28) == 8) else (hex(f) if f is not None else "?")
            print(f"[ptio] {i}: addr=0x{a:x} size=0x{s:x} flags=0x{f:x} -> {t}")
except Exception as e:
    import traceback
    print("[ptio] ERR:", e)
    traceback.print_exc()

print("[ptio] DONE")
