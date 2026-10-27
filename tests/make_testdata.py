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
  make_testdata.py mapname <image>       give a COPY of an ADFS image a disc name in its free space
                                         map ("OLDDISC", CR padded) for selftest's second argument

ADFS floppies come from the C++ tool:  tests/mkimg <outdir>
The Acorn 800K / 640K hybrid images come from:  tests/floptest acorn800|hybrid640 <out>
"""
import os, subprocess, sys

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
    w = lambda p, d: open(os.path.join(root, p), 'wb').write(d)
    w('hello.txt', b'Hello from the host\n')
    w('hello.txt.inf', b'hello    FF1900 FF8023 00000014 0B\n')      # R W L ; 6-digit addresses are sign-extended
    w('prog.bas', os.urandom(5000)); w('prog.bas.inf', b'prog 00001900 00001900 00001388 03\n')
    # 10 PRINT "HELLO" / 20 GOTO 10  (tokenised)
    w('test.bas', bytes.fromhex('0D000A0DF1202248454C4C4F22' + '0D00140AE5208D544A40' + '0DFF'))
    w('sub/a.bin', os.urandom(256)); w('sub/b.bin', os.urandom(257))
    w('sub/nested/c.bin', b''); w('sub/nested/d.bin', os.urandom(70000))
    w('long_filename_example1.dat', b'one' * 100); w('long_filename_example2.dat', b'two' * 100)

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
    elif a[0] == 'mapname': set_map_name(a[1])
    else: sys.exit(__doc__)
