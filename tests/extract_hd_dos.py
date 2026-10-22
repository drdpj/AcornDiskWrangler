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
"""Ground truth for the Acorn 640K hybrid tests: dump the files of the DOS Plus partition ($.DOS.DRIVE_C) from a
hard-disc image (scsi0.dat) into a directory.  Many of the same files (COMMAND.COM, DOSPLUS.SYS, HELP.HLP ...) are
on a real hybrid floppy, so `floptest real640 <copy of M512_1.ADL> <this directory>` can compare real file CONTENT.
usage: extract_hd_dos.py scsi0.dat OUTDIR [ADFS_SECTOR_OF_DRIVE_C=151]
(The partition is FAT16: 1024-byte sectors, 4 per cluster, 1 reserved, 1 FAT of 15 sectors, 512 root entries.)"""
import struct, sys, os
img, out = sys.argv[1], sys.argv[2]
base = (int(sys.argv[3]) if len(sys.argv) > 3 else 151) * 256
BPS, SPC, RES, NF, SPF, ROOT = 1024, 4, 1, 1, 15, 512
os.makedirs(out, exist_ok=True)
f = open(img, 'rb')
def rd(o, n): f.seek(o); return f.read(n)
fat = rd(base + RES * BPS, SPF * BPS)
root = rd(base + (RES + NF * SPF) * BPS, ROOT * 32)
data = base + (RES + NF * SPF) * BPS + ROOT * 32
n = 0
for i in range(ROOT):
    e = root[i*32:i*32+32]
    if e[0] in (0, 0xE5) or e[11] & 0x18: continue
    nm = (e[:8].decode('latin1').strip() + '.' + e[8:11].decode('latin1').strip()).rstrip('.')
    cl = struct.unpack('<H', e[26:28])[0]; sz = struct.unpack('<I', e[28:32])[0]
    b = b''
    while 2 <= cl < 0xFFF0 and len(b) < sz:
        b += rd(data + (cl - 2) * SPC * BPS, SPC * BPS); cl = struct.unpack('<H', fat[2*cl:2*cl+2])[0]
    open(os.path.join(out, nm), 'wb').write(b[:sz]); n += 1
print(n, 'files written to', out)
