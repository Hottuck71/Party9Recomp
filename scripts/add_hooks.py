"""Copy main.dol's translated chunks with the runtime's hooks added: what the launcher's builder.cpp add_hooks()
does, for building by hand. A call inside a chunk jumps straight to its target, so each replaced function gets a
check right after its entry label.

usage: python add_hooks.py <generated/chunks> <replacements.txt> <out_dir>
"""
import os
import re
import sys

src, repl, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
addresses = []
for line in open(repl):
    line = line.split("#")[0].split()
    if line:
        addresses.append(line[0].upper())
chunks = sorted((int(re.search(r"_([0-9A-F]{8})\.c$", n).group(1), 16), n) for n in os.listdir(src) if n.endswith(".c"))
hooks = {n: [] for _, n in chunks}
for a in addresses:
    v = int(a, 16)
    owner = [n for s, n in chunks if s <= v][-1]
    hooks[owner].append(a)
for _, name in chunks:
    text = open(os.path.join(src, name), encoding="utf-8", errors="replace", newline="").read()
    text = text.replace('#include "../generated.h"', '#include "generated.h"\nint native_replaced(CPUState* ctx, u32 pc);', 1)
    for a in hooks[name]:
        label = f"label_{a}:"
        if text.count(label) != 1:
            sys.exit(f"{name}: expected one {label} for a replaced function")
        end = text.index("\n", text.index(label))
        text = text[:end + 1] + f"    if (native_replaced(ctx, 0x{a}u)) return;\n" + text[end + 1:]
    with open(os.path.join(out, name), "w", encoding="utf-8", newline="") as f:
        f.write(text)
print(f"{len(addresses)} hooks in {sum(1 for v in hooks.values() if v)} of {len(chunks)} chunks")
