"""Unpack Nintendo LZ11 files (Wii Party's files/rel/*.rel.lz) into plain .rel files.

usage: python unlz11.py <in_dir> <out_dir>
"""
import os
import sys


def unlz11(data: bytes) -> bytes:
    if data[0] != 0x11:
        raise ValueError("not LZ11")
    size = data[1] | data[2] << 8 | data[3] << 16
    pos = 4
    if size == 0:
        size = int.from_bytes(data[4:8], "little")
        pos = 8
    out = bytearray()
    while len(out) < size:
        flags = data[pos]
        pos += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if not flags & (1 << bit):
                out.append(data[pos])
                pos += 1
                continue
            b0 = data[pos]
            ind = b0 >> 4
            if ind == 0:
                b1, b2 = data[pos + 1], data[pos + 2]
                length = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11
                disp = ((b1 & 0xF) << 8 | b2) + 1
                pos += 3
            elif ind == 1:
                b1, b2, b3 = data[pos + 1], data[pos + 2], data[pos + 3]
                length = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111
                disp = ((b2 & 0xF) << 8 | b3) + 1
                pos += 4
            else:
                b1 = data[pos + 1]
                length = ind + 1
                disp = ((b0 & 0xF) << 8 | b1) + 1
                pos += 2
            start = len(out) - disp
            for i in range(length):
                out.append(out[start + i])
    return bytes(out[:size])


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    total = 0
    for name in sorted(os.listdir(src)):
        if not name.endswith(".lz"):
            continue
        with open(os.path.join(src, name), "rb") as f:
            raw = unlz11(f.read())
        with open(os.path.join(dst, name[:-3]), "wb") as f:
            f.write(raw)
        total += len(raw)
    print(f"unpacked to {dst}: {total / 1048576:.1f} MB")


if __name__ == "__main__":
    main()
