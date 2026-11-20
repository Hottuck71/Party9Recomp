"""Find where the runtime's Resort addresses are in another game's main.dol, by matching function bodies.

Both games link Nintendo's SDK, so most functions the runtime hooks are the same code at a different address.
Each main.dol is cut into functions (bl targets and prologues after a blr), and each function gets a signature:
its instructions with everything that moves between builds masked out (branch-and-link targets, address halves,
small-data offsets). A Resort function maps to the Wii Party function with the same signature, if exactly one.

Data addresses (SDK globals) map through the instructions that use them: where a matched Resort function loads
or stores an address, the same instruction in the Wii Party function gives the new one.

usage: python port_addresses.py <resort main.dol> <party main.dol> <addresses.txt>
addresses.txt: "address name [code|data|label]" per line. "label" is a point inside a function.
"""
import struct
import sys
from collections import defaultdict


class Dol:
    def __init__(self, path):
        b = open(path, "rb").read()
        self.words = {}
        self.text = []
        for i in range(18):
            off, addr, size = (struct.unpack_from(">I", b, o + i * 4)[0] for o in (0, 0x48, 0x90))
            if not size:
                continue
            if i < 7:
                self.text.append((addr, addr + size))
            for k in range(0, size, 4):
                self.words[addr + k] = struct.unpack_from(">I", b, off + k)[0]
        self.entry = struct.unpack_from(">I", b, 0xE0)[0]
        self.r13 = self.r2 = None
        self.find_small_data()
        self.cut()

    def w(self, a):
        return self.words.get(a, 0)

    def find_small_data(self):
        hi = {}
        for a in range(self.entry, self.entry + 0x400, 4):
            ins = self.w(a)
            op, rd, ra, imm = ins >> 26, (ins >> 21) & 31, (ins >> 16) & 31, ins & 0xFFFF
            if op == 15 and ra == 0:
                hi[rd] = imm << 16
            elif op == 24 and rd == ra and ra in hi:  # ori rX, rX, lo
                v = hi[ra] | imm
                if ra == 13 and self.r13 is None:
                    self.r13 = v
                if ra == 2 and self.r2 is None:
                    self.r2 = v

    def cut(self):
        starts = set()
        for lo, hi in self.text:
            prev_blr = True
            for a in range(lo, hi, 4):
                ins = self.w(a)
                if ins >> 26 == 18 and ins & 1 and not ins & 2:  # bl
                    off = ins & 0x03FFFFFC
                    if off & 0x02000000:
                        off -= 0x04000000
                    t = a + off
                    if any(l <= t < h for l, h in self.text):
                        starts.add(t)
                if prev_blr and ins != 0:
                    starts.add(a)
                prev_blr = ins in (0x4E800020, 0) or ins >> 26 == 18 and not ins & 1  # blr, padding, tail b
        self.starts = sorted(starts)
        self.ends = {}
        for i, s in enumerate(self.starts):
            nxt = self.starts[i + 1] if i + 1 < len(self.starts) else s + 0x4000
            self.ends[s] = min(nxt, s + 0x4000)

    def func_of(self, addr):
        import bisect
        i = bisect.bisect_right(self.starts, addr) - 1
        return self.starts[i] if i >= 0 else None


def mask(ins):
    op = ins >> 26
    ra = (ins >> 16) & 31
    if op == 18:
        return ins & 0xFC000003
    if op == 15 and ra == 0:  # lis: an address half
        return ins & 0xFFFF0000
    if op in (14, 24, 15) or 32 <= op <= 55:  # addi, ori, addis, loads and stores
        if ra in (13, 2) or op in (14, 24, 15) and ra != 1 or 32 <= op <= 55 and ra not in (1,):
            return ins & 0xFFFF0000
    if op in (48, 49, 50, 51, 52, 53, 54, 55):
        return ins & 0xFFFF0000
    return ins


def signature(dol, start):
    return tuple(mask(dol.w(a)) for a in range(start, dol.ends[start], 4))


def refs(dol, start):
    """{instruction index: absolute data address} for lis/addi-style pairs and small-data offsets."""
    out = {}
    hi = {}
    for i, a in enumerate(range(start, dol.ends[start], 4)):
        ins = dol.w(a)
        op, rd, ra, imm = ins >> 26, (ins >> 21) & 31, (ins >> 16) & 31, ins & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 15 and ra == 0:
            hi[rd] = imm << 16
            continue
        if op in (14, 24) or 32 <= op <= 55:
            if ra == 13 and dol.r13 is not None:
                out[i] = (dol.r13 + simm) & 0xFFFFFFFF
            elif ra == 2 and dol.r2 is not None:
                out[i] = (dol.r2 + simm) & 0xFFFFFFFF
            elif ra in hi and ra != 0:
                out[i] = (hi[ra] | imm) if op == 24 else (hi[ra] + simm) & 0xFFFFFFFF
        if op in (14, 15, 24) and rd in hi and rd != ra:
            hi.pop(rd, None)
    return out


def main():
    resort, party = Dol(sys.argv[1]), Dol(sys.argv[2])
    print(f"Resort: {len(resort.starts)} functions, r13 {resort.r13:08X} r2 {resort.r2:08X}")
    print(f"Party:  {len(party.starts)} functions, r13 {party.r13:08X} r2 {party.r2:08X}")
    by_sig = defaultdict(list)
    for s in party.starts:
        by_sig[signature(party, s)].append(s)

    def match(rs):
        cands = by_sig.get(signature(resort, rs), [])
        return cands[0] if len(cands) == 1 else (None if not cands else cands)

    # Data: every Resort function that references the address, mapped through the matched function.
    data_users = defaultdict(list)
    wanted = []
    for line in open(sys.argv[3]):
        line = line.split("#")[0].split()
        if line:
            wanted.append((int(line[0], 16), line[1], line[2] if len(line) > 2 else "code"))
    data_targets = {a for a, _, k in wanted if k == "data"}
    if data_targets:
        for s in resort.starts:
            for i, addr in refs(resort, s).items():
                for t in data_targets:
                    # Exact uses first; a struct's fields are often reached from its base, so nearby bases count
                    # too, weighted far lower.
                    if 0 <= t - addr < 0x400 and (t - addr) % 4 == 0:
                        data_users[t].append((s, i, addr - t))

    for addr, name, kind in wanted:
        if kind in ("code", "label"):
            fs = resort.func_of(addr)
            m = match(fs)
            if isinstance(m, int):
                print(f"{addr:08X} {name:28} -> {m + (addr - fs):08X}" + (f"  (label +{addr - fs:X})" if addr != fs else ""))
            else:
                print(f"{addr:08X} {name:28} -> ?? " + ("no match" if m is None else
                      f"candidates {' '.join(f'{c + (addr - fs):08X}' for c in m)}"))
        else:
            votes = defaultdict(int)
            for s, i, delta in data_users[addr]:
                m = match(s)
                if isinstance(m, int):
                    r = refs(party, m).get(i)
                    if r is not None:
                        votes[(r - delta) & 0xFFFFFFFF] += 100 if delta == 0 else 1
            if votes:
                best = sorted(votes.items(), key=lambda kv: -kv[1])
                print(f"{addr:08X} {name:28} -> {best[0][0]:08X}  ({best[0][1]} uses agree"
                      + (f"; others {[(f'{a:08X}', n) for a, n in best[1:4]]}" if len(best) > 1 else "") + ")")
            else:
                print(f"{addr:08X} {name:28} -> ?? ({len(data_users[addr])} Resort uses, none matched)")


if __name__ == "__main__":
    main()
