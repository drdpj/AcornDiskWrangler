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
"""Independent check of ADFS old-map discs and DFS double-sided images - written from the sources below, not from
this program's code, so that it can catch the program and the program's tests agreeing on the same mistake.

  acorncheck.py <image> ... [--sequential <image> ...]

Images after --sequential are read as sequential files (all of side 0, then all of side 1; b-em's
SDF_SIDES_SEQUENTIAL). For an L disc, whose sectors are numbered side 0 first, that is simply sector s at s * 256.

ADFS (.adl/.adm/.ads, hard discs, the ADFS side of a DOS Plus 640K disc):
  * Free space map checksums (bytes &FF and &1FF), as Acorn's ADFS ROM computes them (routine at &9065 in ADFS 1.30,
    as disassembled in github.com/dominicbeesley/ADFS-multi-target, src/adfs.asm): A = &FF, carry clear, then
    ADC every byte from &FE down to &00, the carry running on from one add to the next; the final carry is dropped.
    The same as b-em's adfs_checksum() (src/sdf-geo.c) and the RISC OS Open description of the old map.
  * Sector numbering of an L (640K) floppy: Acorn's floppy driver (src/floppy.asm, after XA_DIV16_TO_YA) takes
    track = sector / 16; a track of 80 or more is side 1, track - 80. So the disc counts all of side 0, then side 1.
  * Layout of the image FILE: track by track, side 0 then side 1 of each cylinder (b-em, src/sdf-geo.c: ADFS-L is
    SDF_SIDES_INTERLEAVED; MAME's Acorn formats agree). So logical sector s of an L disc is at file offset
    ((track * 2 + side) * 16 + s % 16) * 256.
  * Then every directory is walked from the root (sector 2): "Hugo" at both ends, matching sequence numbers, entries
    and objects inside the disc, no overlaps, and the free space list neither overlaps an object nor runs off the disc.
DFS (.dsd): the side-1 catalogue is at file sector 10 (interleaved, b-em) and must be plausible.

Standard library only. Exit status 0 if every image passes.
"""
import os, sys

SECTOR = 256


def fsm_check(block):
    """Acorn ADFS ROM: LDY #&FF : TYA : CLC : loop ADC block-1,Y : DEY : BNE loop (bytes &FE..&00)."""
    a, c = 0xFF, 0
    for i in range(0xFE, -1, -1):
        s = a + block[i] + c
        a, c = s & 0xFF, s >> 8
    return a


class Disc:
    def __init__(self, data, adfs_l):
        self.data, self.l = data, adfs_l

    def sector(self, s):
        if self.l:                          # Acorn floppy driver + b-em file layout
            track, sec = divmod(s, 16)
            side = 0
            if track >= 80:
                track, side = track - 80, 1
            off = ((track * 2 + side) * 16 + sec) * SECTOR
        else:
            off = s * SECTOR
        return self.data[off:off + SECTOR]

    def read(self, s, n):
        return b''.join(self.sector(s + i) for i in range(n))


def u(b, n):
    return int.from_bytes(b[:n], 'little')


def check_adfs(path, data, sequential=False):
    problems = []
    bad = problems.append
    l = len(data) == 655360 and not sequential
    d = Disc(data, l)
    s0, s1 = d.sector(0), d.sector(1)
    for name, blk in (('sector 0', s0), ('sector 1', s1)):
        if fsm_check(blk) != blk[0xFF]:
            bad('%s: map checksum &%02X, stored &%02X' % (name, fsm_check(blk), blk[0xFF]))
    total = u(s0[0xFC:], 3)
    end = s1[0xFE]
    if end % 3 or end > 0xF6:
        bad('free space end pointer &%02X' % end)
    free = [(u(s0[i:], 3), u(s1[i:], 3)) for i in range(0, end, 3)]
    used = [(0, 7)]                         # map + root directory
    if total == 0 or total * SECTOR > max(len(data), 2720 * SECTOR):
        bad('disc size %d sectors' % total)
    stats = {'dirs': 0, 'files': 0, 'basic': 0}

    def walk(sin, path, depth, parent):
        if depth > 32:
            bad('%s: too deep' % path); return
        blk = d.read(sin, 5)
        if blk[1:5] != b'Hugo' or blk[0x4FB:0x4FF] != b'Hugo':
            bad('%s (sector &%X): no Hugo at both ends' % (path, sin)); return
        if blk[0] != blk[0x4FA]:
            bad('%s: start/end sequence numbers differ' % path)
        if depth and u(blk[0x4D6:], 3) != parent:
            bad('%s: parent pointer &%X, expected &%X' % (path, u(blk[0x4D6:], 3), parent))
        stats['dirs'] += 1
        for e in range(47):
            ent = blk[5 + e * 26: 5 + (e + 1) * 26]
            if ent[0] == 0:
                break
            name = bytes(c & 0x7F for c in ent[:10]).split(b'\r')[0].rstrip(b'\x00 ').decode('latin-1')
            isdir = bool(ent[3] & 0x80)
            length, start = u(ent[0x12:], 4), u(ent[0x16:], 3)
            n = 5 if isdir else (length + SECTOR - 1) // SECTOR
            if start + n > total:
                bad('%s.%s: runs off the disc' % (path, name)); continue
            if n:
                used.append((start, n))
            if isdir:
                walk(start, path + '.' + name, depth + 1, sin)
            else:
                stats['files'] += 1
                body = d.read(start, n)[:length]
                if u(ent[0x0E:], 2) in (0x8023, 0x802B) and len(body) > 1 and body[0] == 0x0D:   # a BASIC program
                    if basic_intact(body):
                        stats['basic'] += 1
                    else:
                        bad('%s.%s: BBC BASIC program does not read intact (data in the wrong place?)' % (path, name))
    walk(2, '$', 0, 2)
    spans = sorted(used + [(a, n) for a, n in free])
    for (a, n), (b, m) in zip(spans, spans[1:]):
        if a + n > b:
            bad('overlap: sectors &%X+%d and &%X+%d (objects or free space)' % (a, n, b, m))
    if free and free[-1][0] + free[-1][1] > total:
        bad('free space runs off the disc')
    kind = 'ADFS L, interleaved file' if l else ('ADFS L, sequential file' if sequential else 'ADFS %d sectors' % total)
    return problems, '%s: %d dirs, %d files (%d BASIC programs intact), map checksums ok' % (
        kind, stats['dirs'], stats['files'], stats['basic'])


def basic_intact(b):
    """A tokenised BBC BASIC program is a chain of lines: &0D, line number high, low, line length; it ends &0D &FF.
    Following the chain from the start to the end marker shows every sector of the file is where it should be."""
    i = 0
    while i + 1 < len(b):
        if b[i] != 0x0D:
            return False
        if b[i + 1] == 0xFF:
            return True
        if i + 3 >= len(b) or b[i + 3] < 4:
            return False
        i += b[i + 3]
    return False


def plausible_cat(s0, s1):
    cnt = s1[5]
    if cnt % 8 or cnt > 31 * 8:
        return False
    sec = ((s1[6] & 3) << 8) | s1[7]
    if not 2 <= sec <= 800:
        return False
    return all(0x20 <= (c & 0x7F) <= 0x7E for i in range(1, cnt // 8 + 1) for c in s0[i * 8:i * 8 + 8])


def check_dsd(path, data, sequential=False):
    s = lambda n: data[n * SECTOR:(n + 1) * SECTOR]
    problems = []
    side1 = len(data) // SECTOR // 2 if sequential else 10
    if not plausible_cat(s(0), s(1)):
        problems.append('side 0 catalogue is not plausible')
    if not plausible_cat(s(side1), s(side1 + 1)):
        problems.append('side 1 catalogue (file sector %d) is not plausible' % side1)
    return problems, 'DFS double-sided, %s: both catalogues plausible' % ('sequential' if sequential else 'interleaved')


def main(paths):
    failed, seq = 0, False
    for p in paths:
        if p == '--sequential':
            seq = True; continue
        data = open(p, 'rb').read()
        is_adfs = len(data) >= 7 * SECTOR and data[0x201:0x205] == b'Hugo' or \
            (len(data) == 655360 and Disc(data, True).read(2, 1)[1:5] == b'Hugo')
        if p.lower().endswith('.dsd'):
            problems, what = check_dsd(p, data, seq)
        elif is_adfs:
            problems, what = check_adfs(p, data, seq)
        else:
            print('%-50s skipped (not ADFS or .dsd)' % os.path.basename(p)); continue
        if problems:
            failed += 1
            print('%-50s FAIL' % os.path.basename(p))
            for x in problems[:10]:
                print('    ' + x)
        else:
            print('%-50s ok   %s' % (os.path.basename(p), what))
    print('ALL OK' if not failed else 'FAILED (%d)' % failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
