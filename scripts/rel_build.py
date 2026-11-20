"""Translate Wii Party's REL modules, one at a time, each at its own fixed slot address.

On the console the game loads a module into a heap buffer wherever there is room and the SDK's Link() relocates
it there. Translated code can't move, so every module is translated for one address (its slot) and the runtime
(native/src/rel.c) relocates each module it links to that slot instead. Modules never import from each other,
only from main.dol and themselves, so each one translates on its own. Modules of the same kind are never loaded
together (one minigame at a time), so they share a slot; the runtime stops with a report if two ever overlap.

Slots sit above the 24 MB the game is told it has, in memory only the runtime knows about, and below 0x82000000,
so main.dol (from 0x80004000) is still in reach of the modules' 24-bit branches.

Each module's functions are renamed rel<id>_<address> so modules sharing a slot don't collide, and a lookup
function per module (rel_find_<id>) lets the runtime's dispatch loop find them.

usage: python rel_build.py <rel_dir> <dolrecomp.exe> <work_dir> <out_dir> [-jN]
  rel_dir   unpacked .rel files (scripts/unlz11.py)
  work_dir  DolRecomp's raw output, kept so a re-run only redoes what changed
  out_dir   chunks/ (renamed, ready to compile) and rel_table.c
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

SLOT_TOP = 0x82000000

# (kind, base). A module's kind is its name without the trailing number, or the whole name for one-offs.
SLOTS = [
    ("boot", 0x81800000),
    ("loading", 0x81801000),
    ("inst", 0x81810000),
    ("menu", 0x818C0000),
    ("openmess", 0x81990000),
    ("ranking", 0x81A10000),
    ("ms", 0x81A90000),
    ("mr", 0x81B70000),
    ("mg", 0x81CB0000),
]


def kind_of(name):
    for kind, _ in SLOTS:
        if name == kind or (len(kind) == 2 and name.startswith(kind) and name[2:].isdigit()):
            return kind
    raise SystemExit(f"no slot for module {name}")


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def module_info(path):
    b = open(path, "rb").read()
    fix = be32(b, 0x48)
    bss_align = be32(b, 0x44) or 32
    bss_size = be32(b, 0x20)
    return {"id": be32(b, 0), "fix": fix, "bss_size": bss_size, "bss_align": bss_align, "hash": hashlib.sha256(b).hexdigest()}


def align(v, a):
    return (v + a - 1) // a * a


def translate(dolrecomp, rel_path, base, work):
    stamp = os.path.join(work, "stamp.txt")
    want = f"broadway {base:08X} {hashlib.sha256(open(rel_path, 'rb').read()).hexdigest()}\n"
    if os.path.exists(stamp) and open(stamp).read() == want:
        return
    os.makedirs(work, exist_ok=True)
    r = subprocess.run([dolrecomp, "--cpu", "broadway", "--rel-base", f"0x{base:08X}", rel_path, "SUPE01", work],
                       capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"dolrecomp failed on {rel_path}:\n{r.stdout}\n{r.stderr}")
    open(stamp, "w").write(want)


FUNC = re.compile(r"\bfunc_([0-9A-F]{8})\b")


def post_process(mid, gen_dir, out_chunks):
    """Copies the chunks with renamed functions; returns the module's lookup function as C."""
    chunks = os.path.join(gen_dir, "chunks")
    for name in sorted(os.listdir(chunks)):
        text = open(os.path.join(chunks, name), encoding="utf-8", errors="replace").read()
        text = text.replace('#include "../generated.h"', "/* helpers come from fastmem.h's include of main.dol's generated.h */", 1)
        text = FUNC.sub(lambda m: f"rel{mid}_{m.group(1)}", text)
        target = os.path.join(out_chunks, f"rel{mid}_{name}")
        if not os.path.exists(target) or open(target, encoding="utf-8", errors="replace").read() != text:
            with open(target, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
    header = open(os.path.join(gen_dir, "generated.h"), encoding="utf-8").read()
    decls = sorted(set(FUNC.findall(header)))
    body = re.search(r"static inline DolRecompFunction dolrecomp_find_original\(u32 address\) \{\n(.*?)\n\}\n", header, re.S)
    if not body:
        raise SystemExit(f"no lookup in {gen_dir}/generated.h")
    lookup = FUNC.sub(lambda m: f"rel{mid}_{m.group(1)}", body.group(1))
    return "".join(f"void rel{mid}_{a}(CPUState* ctx);\n" for a in decls) + \
        f"DolRecompFunction rel_find_{mid}(u32 address) {{\n{lookup}\n}}\n\n"


def main():
    rel_dir, dolrecomp, work_dir, out_dir = sys.argv[1:5]
    jobs = next((int(a[2:]) for a in sys.argv if a.startswith("-j")), os.cpu_count() or 4)
    out_chunks = os.path.join(out_dir, "chunks")
    os.makedirs(out_chunks, exist_ok=True)

    modules = {}
    for name in sorted(os.listdir(rel_dir)):
        if not name.endswith(".rel"):
            continue
        info = module_info(os.path.join(rel_dir, name))
        info["name"] = name[:-4]
        prior = modules.get(info["id"])
        if prior:  # the disc has a few modules twice under two names, byte for byte the same
            if prior["hash"] != info["hash"]:
                raise SystemExit(f"modules {prior['name']} and {info['name']} share id {info['id']} but differ")
            continue
        modules[info["id"]] = info

    bases = dict(SLOTS)
    order = [k for k, _ in SLOTS]
    for i, (kind, base) in enumerate(SLOTS):
        end = SLOTS[i + 1][1] if i + 1 < len(SLOTS) else SLOT_TOP
        for m in modules.values():
            if kind_of(m["name"]) != kind:
                continue
            m["base"] = base
            m["bss"] = align(base + m["fix"], m["bss_align"])
            m["end"] = m["bss"] + m["bss_size"]
            if m["end"] > end:
                raise SystemExit(f"{m['name']} ends at {m['end']:08X}, past its slot's end {end:08X}")

    def one(m):
        work = os.path.join(work_dir, f"{m['name']}_{m['id']}")
        translate(dolrecomp, os.path.join(rel_dir, m["name"] + ".rel"), m["base"], work)
        gen = os.path.join(work, "generated")  # a single .rel goes straight here, not under rels/
        return post_process(m["id"], gen, out_chunks)

    ordered = sorted(modules.values(), key=lambda m: (order.index(kind_of(m["name"])), m["name"]))
    with ThreadPoolExecutor(jobs) as pool:
        lookups = list(pool.map(one, ordered))

    keep = {f for f in os.listdir(out_chunks)}
    with open(os.path.join(out_dir, "rel_table.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("/* Made by scripts/rel_build.py: every translated REL module, its slot and its lookup. */\n")
        f.write('#include "generated.h"\n#include "rel.h"\n\n')
        f.writelines(lookups)
        f.write("const RelModule rel_modules[] = {\n")
        for m in ordered:
            f.write(f'    {{{m["id"]}, "{m["name"]}", 0x{m["base"]:08X}u, 0x{m["end"]:08X}u, 0x{m["bss"]:08X}u, '
                    f'{m["bss_size"]}u, rel_find_{m["id"]}}},\n')
        f.write("};\nconst unsigned rel_module_count = sizeof rel_modules / sizeof *rel_modules;\n")
    print(f"{len(ordered)} modules translated; {len(keep)} chunks in {out_chunks}")
    for kind, base in SLOTS:
        ms = [m for m in ordered if kind_of(m["name"]) == kind]
        print(f"  slot {kind:9} {base:08X}: {len(ms):3} modules, largest ends {max(m['end'] for m in ms):08X}")


if __name__ == "__main__":
    main()
