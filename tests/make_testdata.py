#!/usr/bin/env python3
# Acorn Disk Wrangler - view and edit Acorn disc images (ADFS, DFS, DOS Plus, PC FAT)
# Copyright (C) 2026 Daniel Jameson
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
"""Generate the test data the C++ tests need (no third-party packages).

  make_testdata.py all <outdir>          everything below, into <outdir> (host files go to /tmp/drop/proj)
  make_testdata.py dfs <outdir>          blank/truncated/sequential DFS images
  make_testdata.py host                  host files for guitest2 in /tmp/drop/proj
  make_testdata.py pc <outdir>           PC floppies via mkfs.fat (needs dosfstools)
  make_testdata.py tape <outdir>         t.uef: a BBC tape as MOS 1.20 saves it (GAME twice, a protected file, an empty
                                         one), for guitest2 and the golden cases (not in "all": detection's tests take
                                         every file of that folder)
  make_testdata.py mapname <image>       give a COPY of an ADFS image a disc name in its free space
                                         map ("OLDDISC", CR padded) for selftest's second argument

ADFS floppies come from the C++ tool:  tests/mkimg <outdir>
The Acorn 800K / 640K hybrid images come from:  tests/floptest acorn800|hybrid640 <out>
"""
import os, random, struct, subprocess, sys

def cat(sectors, title=b''):
    s0 = bytearray(256); s1 = bytearray(256)
    t = title.ljust(12, b'\0'); s0[0:8] = t[:8]; s1[0:4] = t[8:12]
    s1[6] = (sectors >> 8) & 3; s1[7] = sectors & 255
    return s0, s1

def ssd(n, title=b'', trunc=None):
    img = bytearray(n * 256); a, b = cat(n, title); img[:256] = a; img[256:512] = b
    return bytes(img if trunc is None else img[:trunc])

def dsd(n, t0, t1, interleaved=True, trunc=None):
    img = bytearray(2 * n * 256)
    for side, t in ((0, t0), (1, t1)):
        a, b = cat(n, t)
        for k, dat in ((0, a), (1, b)):
            p = ((k // 10) * 2 + side) * 10 + k % 10 if interleaved else side * n + k
            img[p * 256:(p + 1) * 256] = dat
    return bytes(img if trunc is None else img[:trunc])

def make_dfs(out):
    os.makedirs(out, exist_ok=True)
    w = lambda name, data: open(os.path.join(out, name), 'wb').write(data)
    w('blank40.ssd', ssd(400, b'BLANK40')); w('blank80.ssd', ssd(800, b'BLANK80'))
    w('blank40.dsd', dsd(400, b'SIDE0', b'SIDE2')); w('blank80.dsd', dsd(800, b'SIDE0_80', b'SIDE2_80'))
    w('trunc40.ssd', ssd(400, b'TRUNC', trunc=6000))            # blank tail trimmed off
    w('trunc80.dsd', dsd(800, b'T0', b'T2', trunc=30000))
    w('seq40.dsd', dsd(400, b'SEQ0', b'SEQ2', interleaved=False))
    w('noext_dfs.img', ssd(400, b'NOEXT'))                       # detected by content, not suffix

def make_host(root='/tmp/drop/proj'):
    os.makedirs(root + '/sub/nested', exist_ok=True)
    # The same bytes every time: random bytes now and then look like a DFS catalogue (1 in 400 sets of 5000 bytes
    # tried did), and the window then treats prog.bas as a disc image, so guitest and guitest2 failed now and then.
    # This seed's files are checked to be no disc image (detecttest says Unknown for each).
    rnd = random.Random(6)
    w = lambda p, d: open(os.path.join(root, p), 'wb').write(d)
    w('hello.txt', b'Hello from the host\n')
    w('hello.txt.inf', b'hello    FF1900 FF8023 00000014 0B\n')      # R W L ; 6-digit addresses are sign-extended
    w('prog.bas', rnd.randbytes(5000)); w('prog.bas.inf', b'prog 00001900 00001900 00001388 03\n')
    # 10 PRINT "HELLO" / 20 GOTO 10  (tokenised)
    w('test.bas', bytes.fromhex('0D000A0DF1202248454C4C4F22' + '0D00140AE5208D544A40' + '0DFF'))
    w('sub/a.bin', rnd.randbytes(256)); w('sub/b.bin', rnd.randbytes(257))
    w('sub/nested/c.bin', b''); w('sub/nested/d.bin', rnd.randbytes(70000))
    w('long_filename_example1.dat', b'one' * 100); w('long_filename_example2.dat', b'two' * 100)

def crc16(data):                       # MOS 1.20's CRC (&F7B0): CRC-16, polynomial &1021, from 0, high bit first
    c = 0
    for x in data:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c

def uef_chunk(cid, body):
    return struct.pack('<HI', cid, len(body)) + body

def cfs_file(name, data, load, exec_, flag=0):
    """A file as the MOS saves it: carrier, the dummy byte, then 256-byte blocks, each after its carrier, the last flagged."""
    out = uef_chunk(0x0110, struct.pack('<H', 6000)) + uef_chunk(0x0100, b'\xaa')
    n = max(1, (len(data) + 255) // 256)
    for k in range(n):
        d = data[k * 256:(k + 1) * 256]
        h = name + b'\0' + struct.pack('<IIHHB', load, exec_, k, len(d), flag | (0x80 if k == n - 1 else 0)) + b'\0' * 4
        b = b'*' + h + struct.pack('>H', crc16(h))
        if d: b += d + struct.pack('>H', crc16(d))
        out += uef_chunk(0x0110, struct.pack('<H', 1200 if k == 0 else 600)) + uef_chunk(0x0100, b)
    return out + uef_chunk(0x0110, struct.pack('<H', 6000)) + uef_chunk(0x0112, struct.pack('<H', 2400))

def make_tape(out):
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(7)
    body = b'UEF File!\0' + bytes([10, 0]) + uef_chunk(0x0000, b'make_testdata.py\0')
    body += cfs_file(b'GAME', rnd.randbytes(600), 0x1900, 0x8023)
    body += cfs_file(b'LOADER', rnd.randbytes(300), 0xFFFF0E00, 0xFFFF801F, flag=1)       # protected
    body += cfs_file(b'GAME', rnd.randbytes(100), 0x3000, 0x3000)                          # the same name again
    body += cfs_file(b'EMPTY', b'', 0, 0)
    open(os.path.join(out, 't.uef'), 'wb').write(body)     # not compressed: the same bytes whatever zlib this machine has

def make_pc(out):
    os.makedirs(out, exist_ok=True)
    for k in (360, 720, 1200, 1440):
        subprocess.check_call(['mkfs.fat', '-C', os.path.join(out, 'pc%d.img' % k), str(k)], stdout=subprocess.DEVNULL)

def checksum(s):                       # ADFS free space map checksum as Acorn's ROM does it: start &FF, ADC bytes &FE..&00
    sm, c = 0xFF, 0
    for i in range(254, -1, -1):
        t = sm + s[i] + c; c = 1 if t > 255 else 0; sm = t & 255
    return sm

def set_map_name(path, name=b'OLDDISC'):
    with open(path, 'r+b') as f:
        d = bytearray(f.read(512)); nm = name + b'\r' * (10 - len(name))
        for i, ch in enumerate(nm): d[(256 if i % 2 else 0) + 0xF6 + i // 2] = ch
        d[255] = checksum(d[:256]); d[511] = checksum(d[256:])
        f.seek(0); f.write(d)

if __name__ == '__main__':
    a = sys.argv[1:]
    if not a: sys.exit(__doc__)
    if a[0] == 'all': make_dfs(a[1]); make_host(); make_pc(a[1])
    elif a[0] == 'dfs': make_dfs(a[1])
    elif a[0] == 'host': make_host()
    elif a[0] == 'pc': make_pc(a[1])
    elif a[0] == 'tape': make_tape(a[1])
    elif a[0] == 'mapname': set_map_name(a[1])
    else: sys.exit(__doc__)
