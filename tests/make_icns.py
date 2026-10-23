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
"""Make the macOS icon (icons/acorndiskwrangler.icns) from the PNGs that tests/mkicon writes.

  make_icns.py <dir from mkicon> <out.icns>

An .icns file is "icns" + total length, then chunks of (4-byte type, length, data); since macOS 10.7 each
chunk can simply be a PNG. Every chunk here is an exact rendering, none scaled. Standard library only.
"""
import struct, sys

src, out = sys.argv[1], sys.argv[2]
# chunk type -> pixel size (the @2x types hold the double-size rendering of a point size)
# Only the PNG chunk types Apple's iconutil also writes; the older 16/32/64-pixel types (icp4-icp6) are left out
# because not every reader accepts PNG in them, and macOS scales 16/32 down from the @2x chunks.
CHUNKS = [('ic11', 32), ('ic12', 64), ('ic07', 128), ('ic13', 256), ('ic08', 256), ('ic14', 512),
          ('ic09', 512), ('ic10', 1024)]
body = b''
for kind, px in CHUNKS:
    png = open('%s/acorndiskwrangler-%d.png' % (src, px), 'rb').read()
    assert png[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', png[16:24]) == (px, px), (kind, px)
    body += kind.encode() + struct.pack('>I', 8 + len(png)) + png
open(out, 'wb').write(b'icns' + struct.pack('>I', 8 + len(body)) + body)
print('%s: %d chunks, %d bytes' % (out, len(CHUNKS), 8 + len(body)))
