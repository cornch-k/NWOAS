# dump_dart.py (NWOAS) v2 — recursive walk to find apcie DART node(s).
from m1n1.adt import load_adt
adt = load_adt(u.get_adt())
print("[dart] scanning /arm-io recursively...")

def basestr(node):
    try:
        b = node.get_reg(0)[0]
        return ("%#x" % b) if isinstance(b, int) else str(b)
    except Exception as e:
        return "reg-err(%s)" % e

def walk(node, path, depth=0):
    if depth > 6:
        return
    name = str(getattr(node, "name", "?"))
    if "dart" in name.lower() or "iommu" in name.lower():
        try: compat = node.compatible
        except Exception: compat = None
        print(f"[dart] {path}  base={basestr(node)}  compat={compat}")
    try:
        for c in node:
            walk(c, path + "/" + str(getattr(c, "name", "?")), depth + 1)
    except Exception:
        pass

walk(adt["/arm-io"], "/arm-io")

# apcie node props (look for iommu / dart references)
try:
    ap = adt["/arm-io/apcie"]
    print("[dart] apcie props:", [k for k in getattr(ap, "_properties", {}).keys()])
    for bp in ap:
        pn = str(getattr(bp, "name", "?"))
        print(f"[dart] apcie/{pn} props:", [k for k in getattr(bp, "_properties", {}).keys()][:14])
except Exception as e:
    print("[dart] apcie err:", e)
print("[dart] DONE")
