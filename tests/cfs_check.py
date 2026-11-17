#!/usr/bin/env python3
# Part of Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
#
# cfs_check.py: an independent reader of the BBC Micro's cassette filing system on UEF tapes. It shares no code with
# the program (src/cfs.cpp) or the core: the UEF chunks are read here from the UEF 0.10 specification (gzip is
# Python's own), the blocks from what MOS 1.20 does (docs/disc-formats.md, "Tapes"). It prints a tape the way
# `cfstest --dump` does, and compares the two.
#
#   python3 -I cfs_check.py --dump FILE                 this reader's dump
#   python3 -I cfs_check.py --get FILE INDEX OUT        file INDEX (from 1) of the tape, its data written to OUT
#   python3 -I cfs_check.py CFSTEST [FILE|DIR ...]      every tape given, dumped by both; any difference fails.
#                                                       Exit 0: all agree.
import gzip
import hashlib
import os
import struct
import subprocess
import sys

UEF_MAGIC = b"UEF File!\0"


def h16(b):
    return hashlib.sha1(b).hexdigest()[:16]


# ----- the UEF file: a list of (id, body)

def read_uef(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    if raw[:10] != UEF_MAGIC:
        raise ValueError("not a UEF file")
    chunks = []
    at = 12
    while at < len(raw):
        if at + 6 > len(raw):
            chunks.append((-1, raw[at:]))
            break
        cid, n = struct.unpack_from("<HI", raw, at)
        if at + 6 + n > len(raw):
            chunks.append((-1, raw[at:]))
            break
        chunks.append((cid, raw[at + 6:at + 6 + n]))
        at += 6 + n
    return chunks


# ----- the signal: a stream of events, each byte with the chunk it came from
#
# ("byte", value, chunk, readable)   readable: 8N1 at 300 or 1200 baud, which the MOS's serial ULA reads
# ("tone", chunk)                     carrier: what the MOS waits for before a block's &2A
# ("gap", chunk)                      silence

def explicit_bytes(body):
    """&0102: the stored bits, framed into bytes by start and stop bits as the cassette hardware does."""
    if not body:
        return []
    nbits = min((len(body) - 1) * 8, len(body) * 8 - body[0])
    bit = lambda i: (body[1 + i // 8] >> (i % 8)) & 1
    out, i = [], 0
    while i < nbits:
        if bit(i):
            i += 1
            continue
        if i + 10 > nbits or not bit(i + 9):
            break
        out.append(sum(bit(i + 1 + k) << k for k in range(8)))
        i += 10
    return out


def events(chunks):
    ev = []
    for n, (cid, b) in enumerate(chunks):
        if cid == 0x0100:
            ev += [("byte", x, n, True) for x in b]
        elif cid == 0x0102:
            ev += [("byte", x, n, True) for x in explicit_bytes(b)]
        elif cid == 0x0104 and len(b) >= 3:
            # a negative stop count (an extra short wave after the stop bits) isn't what the serial ULA reads either
            ok = b[0] == 8 and chr(b[1]) == "N" and b[2] == 1
            ev += [("byte", x, n, ok) for x in b[3:]]
        elif cid == 0x0105:
            ev += [("byte", x, n, False) for x in b]    # the Atom's framing
        elif cid in (0x0110, 0x0111, 0x0114):
            ev.append(("tone", n))
        elif cid in (0x0112, 0x0116):
            ev.append(("gap", n))
    return ev


# ----- the blocks, as the MOS reads them

def crc16(data):
    """MOS 1.20 &F7B0: each byte into the high half, eight shifts, &1021 when bit 15 falls out. Starts at 0."""
    c = 0
    for x in data:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


class Block:
    pass


def blocks(ev):
    """Blocks in tape order, and the indices of the bytes no block took."""
    out, spare = [], []
    armed = True                                    # the start of the tape counts as after a tone
    i = 0
    while i < len(ev):
        e = ev[i]
        if e[0] == "tone":
            armed = True
            i += 1
            continue
        if e[0] == "gap":
            armed = False
            i += 1
            continue
        if not (armed and e[3] and e[1] == 0x2A):
            spare.append(i)
            armed = False                           # anything but &2A after the carrier: wait for carrier again
            i += 1
            continue
        # a block: its bytes run on through tones (high tone is the line idling); a gap or unreadable byte ends it
        start = i
        got = []
        j = i + 1

        def take(k):
            nonlocal j
            while len(got) < k:
                while j < len(ev) and ev[j][0] == "tone":
                    j += 1
                if j >= len(ev) or ev[j][0] != "byte" or not ev[j][3]:
                    return False
                got.append(j)
                j += 1
            return True

        b = Block()
        b.first = ev[start][2]
        b.problem = ""
        name = []
        ok = True
        while len(name) < 11:
            if not take(len(got) + 1):
                ok = False
                break
            v = ev[got[-1]][1]
            name.append(v)
            if v == 0:
                break
        if ok and not take(len(got) + 19):
            ok = False
        if not ok:
            # cut short: the &2A and what followed are unrecognised
            spare.append(start)
            spare += got
            armed = False
            i = j
            continue
        hdr = [ev[k][1] for k in got]
        b.name = bytes(name[:10]).split(b"\0")[0]
        f = hdr[len(name):]
        b.load = struct.unpack_from("<I", bytes(f), 0)[0]
        b.exec = struct.unpack_from("<I", bytes(f), 4)[0]
        b.num = struct.unpack_from("<H", bytes(f), 8)[0]
        b.length = struct.unpack_from("<H", bytes(f), 10)[0]
        b.flag = f[12]
        b.hcrc_ok = crc16(hdr) == 0
        n = 0 if b.length == 0 else (b.length & 0xFF or 256)
        b.data = b""
        b.dcrc_ok = True
        if n:
            k0 = len(got)
            if not take(k0 + n + 2):
                b.problem = "cut short"
                b.data = bytes(ev[k][1] for k in got[k0:k0 + n])
                b.dcrc_ok = False
            else:
                d = [ev[k][1] for k in got[k0:]]
                b.data = bytes(d[:n])
                b.dcrc_ok = crc16(d) == 0
        b.last = ev[got[-1]][2]
        out.append(b)
        armed = False
        i = j
    return out, spare


# ----- files

def files(chunks):
    ev = events(chunks)
    bl, spare = blocks(ev)
    items = []                                      # ("file", dict) or ("data", first, last, nbytes), in tape order
    cur = None

    def close(f):
        if not f["done"]:
            f["problems"].append("no last block")

    for b in bl:
        problems = []
        if not b.hcrc_ok:
            problems.append("block %d header CRC" % b.num)
        if b.problem:
            problems.append("block %d %s" % (b.num, b.problem))
        elif not b.dcrc_ok:
            problems.append("block %d data CRC" % b.num)
        if cur and not cur["done"] and b.name == cur["name"] and b.num == cur["next"]:
            f = cur
        else:
            if cur:
                close(cur)
            f = {"name": b.name, "load": b.load, "exec": b.exec, "flag": b.flag, "data": b"", "blocks": 0,
                 "first": b.first, "done": False, "problems": [], "next": 0}
            if b.num != 0:
                f["problems"].append("starts at block %d" % b.num)
            items.append(("file", f))
            cur = f
        f["problems"] += problems
        if not (b.flag & 0x80) and len(b.data) != 256 and not b.problem:
            f["problems"].append("block %d is %d bytes" % (b.num, len(b.data)))
        f["data"] += b.data
        f["blocks"] += 1
        f["last"] = b.last
        f["next"] = b.num + 1
        if b.flag & 0x80:
            f["done"] = True
    if cur:
        close(cur)

    # bytes no block took: those in a block's own chunks (the MOS sends more after the CRC) and a chunk's only byte
    # (the dummy byte, noise before a file) are normal; the rest, between two files, is one row of unrecognised data
    block_chunks = set()
    for b in bl:
        block_chunks.update(range(b.first, b.last + 1))
    per_chunk = {}
    for e in ev:
        if e[0] == "byte":
            per_chunk[e[2]] = per_chunk.get(e[2], 0) + 1
    runs = []
    for k in spare:
        n = ev[k][2]
        if n in block_chunks or per_chunk[n] == 1:
            continue
        if runs and not any(it[0] == "file" and runs[-1][1] < it[1]["first"] <= n for it in items):
            runs[-1][1] = n
            runs[-1][2] += 1
        else:
            runs.append([n, n, 1])
    for first, last, nb in runs:
        items.append(("data", first, last, nb))
    items.sort(key=lambda it: it[1]["first"] if it[0] == "file" else it[1])
    return items


def name_text(b):
    return "".join(chr(x) if 0x21 <= x < 0x7F and x != 0x5C else "\\x%02x" % x for x in b)


def dump(path):
    chunks = read_uef(path)
    out = []
    n = 0
    for it in files(chunks):
        if it[0] == "file":
            f = it[1]
            n += 1
            out.append("file %d %s load %08X exec %08X length %d blocks %d flag %02X chunks %d-%d data %s%s" % (
                n, name_text(f["name"]), f["load"], f["exec"], len(f["data"]), f["blocks"], f["flag"],
                f["first"], f["last"], h16(f["data"]), "".join(" problem " + p for p in f["problems"])))
        else:
            out.append("unrecognised chunks %d-%d bytes %d" % (it[1], it[2], it[3]))
    return out


def file_data(path, index):
    n = 0
    for it in files(read_uef(path)):
        if it[0] == "file":
            n += 1
            if n == index:
                return it[1]["data"]
    raise IndexError("no file %d" % index)


def tapes_in(arg):
    if os.path.isdir(arg):
        return sorted(os.path.join(arg, f) for f in os.listdir(arg) if f.lower().endswith(".uef"))
    return [arg]


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--dump":
        print("\n".join(dump(a[1])))
        return 0
    if len(a) == 4 and a[0] == "--get":
        open(a[3], "wb").write(file_data(a[1], int(a[2])))
        return 0
    if len(a) < 2:
        print(__doc__ if __doc__ else "usage: cfs_check.py CFSTEST FILE|DIR ...")
        return 2
    tool = a[0]
    tapes = [t for arg in a[1:] for t in tapes_in(arg)]
    bad = 0
    for t in tapes:
        mine = dump(t)
        r = subprocess.run([tool, "--dump", t], capture_output=True, text=True)
        theirs = r.stdout.splitlines()
        if r.returncode != 0 or mine != theirs:
            bad += 1
            print("DIFFER: %s" % t)
            for k in range(max(len(mine), len(theirs))):
                x = mine[k] if k < len(mine) else "<none>"
                y = theirs[k] if k < len(theirs) else "<none>"
                if x != y:
                    print("  python:  %s\n  program: %s" % (x, y))
        else:
            print("ok: %s (%d rows)" % (os.path.basename(t), len(mine)))
    print("%d of %d tapes read the same by both readers" % (len(tapes) - bad, len(tapes)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
