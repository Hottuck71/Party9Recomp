"""Summarize Wii Party's REL modules: sizes, BSS, and which modules each imports from.

usage: python rel_info.py <rel_dir> [--json out.json]
"""
import json
import os
import struct
import sys


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def parse(path):
    b = open(path, "rb").read()
    h = {
        "id": be32(b, 0), "nsec": be32(b, 0x0C), "secoff": be32(b, 0x10), "version": be32(b, 0x1C),
        "bss": be32(b, 0x20), "reloff": be32(b, 0x24), "impoff": be32(b, 0x28), "impsize": be32(b, 0x2C),
        "prolog_sec": b[0x30], "epilog_sec": b[0x31], "unres_sec": b[0x32], "bss_sec": b[0x33],
        "prolog": be32(b, 0x34), "epilog": be32(b, 0x38), "unresolved": be32(b, 0x3C),
        "align": be32(b, 0x40), "bss_align": be32(b, 0x44), "fix": be32(b, 0x48) if be32(b, 0x1C) >= 3 else 0,
        "size": len(b),
    }
    secs = []
    for i in range(h["nsec"]):
        off, size = struct.unpack_from(">II", b, h["secoff"] + i * 8)
        secs.append({"i": i, "off": off & ~1, "exec": off & 1, "size": size})
    h["sections"] = secs
    imps = []
    for i in range(h["impsize"] // 8):
        mid, off = struct.unpack_from(">II", b, h["impoff"] + i * 8)
        imps.append(mid)
    h["imports"] = imps
    return h


def main():
    d = sys.argv[1]
    out = {}
    for name in sorted(os.listdir(d)):
        if name.endswith(".rel"):
            out[name[:-4]] = parse(os.path.join(d, name))
    for name, h in out.items():
        text = sum(s["size"] for s in h["sections"] if s["exec"])
        print(f"{name:9} id {h['id']:3} v{h['version']} file {h['size']:8} fix {h['fix']:8} text {text:8} "
              f"bss {h['bss']:7} align {h['align']}/{h['bss_align']} imports {h['imports']}")
    if "--json" in sys.argv:
        with open(sys.argv[sys.argv.index("--json") + 1], "w") as f:
            json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
