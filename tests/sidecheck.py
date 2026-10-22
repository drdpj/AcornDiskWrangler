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
"""Independent check of the images tests/sidetest leaves behind (stdlib + dosfstools + mtools; no app code).

  sidecheck.py <dir>

For every t<type>_<il|seq>[_flip].<ext> image with a <image>.expect/ tree: undo the side order with this script's
own arithmetic, then read the disc with a separate parser (ADFS old map, DFS) or with fsck.fat + mtools (FAT,
including the Acorn 800K and 640K hybrid layouts wrapped in a BPB) and compare every file with the expected tree.
"""
import os, re, struct, subprocess, sys, tempfile

# type number (NewDisc::Type order) -> (family, track bytes, cylinders, double-sided)
TYPES = {0: ('adfs', 4096, 40, False), 1: ('adfs', 4096, 80, False), 2: ('adfs', 4096, 80, True),
         3: ('dfs', 2560, 40, False), 4: ('dfs', 2560, 80, False), 5: ('dfs', 2560, 40, True), 6: ('dfs', 2560, 80, True),
         7: ('a800', 5120, 80, True), 8: ('h640', 4096, 80, True),
         9: ('pc', 4608, 40, False), 10: ('pc', 4608, 80, False), 11: ('pc', 7680, 80, False), 12: ('pc', 9216, 80, False)}   # PC: always interleaved

def to_interleaved(data, tb, cyl):
    out = bytearray(data)
    for t in range(2 * cyl):
        s = ((t % 2) * cyl + t // 2) * tb          # interleaved track t sits here in a sequential file
        out[t * tb:(t + 1) * tb] = data[s:s + tb]
    return bytes(out)

def adfs_files(img, side_first=False):
    """ADFS old map. side_first: an ADFS L disc, which numbers its sectors all of side 0 then side 1, inside an
    interleaved image (img), so sector s is in file track 2t (t < 80) or 2(t - 80) + 1, t = s // 16."""
    def sec_read(s, n):
        if not side_first: return img[s * 256:(s + n) * 256]
        out = b''
        for k in range(s, s + n):
            t, r = divmod(k, 16)
            f = 2 * t if t < 80 else 2 * (t - 80) + 1
            out += img[f * 4096 + r * 256:f * 4096 + r * 256 + 256]
        return out
    files = {}
    def name_of(b):
        n = ''
        for c in b[:10]:
            c &= 0x7F
            if c < 0x21: break
            n += chr(c)
        return n
    def walk(sec, path, depth):
        d = sec_read(sec, 5)
        assert d[1:5] == b'Hugo' and d[0x4FB:0x4FF] == b'Hugo', 'bad directory at sector %d' % sec
        for i in range(47):
            e = d[5 + 26 * i:31 + 26 * i]
            if e[0] == 0: break
            n = name_of(e)
            load, exe, ln = struct.unpack('<III', e[10:22])
            sin = e[22] | e[23] << 8 | e[24] << 16
            if e[3] & 0x80: walk(sin, path + '.' + n, depth + 1)
            else: files['adfs/' + path + '.' + n] = sec_read(sin, (ln + 255) // 256)[:ln]
    walk(2, '$', 0)
    return files

def dfs_files(img, sides):
    files = {}
    for s in range(sides):
        def sector(n):
            p = ((n // 10) * 2 + s) * 10 + n % 10 if sides == 2 else n
            return img[p * 256:(p + 1) * 256]
        s0, s1 = sector(0), sector(1)
        for i in range(s1[5] // 8):
            nm = s0[8 + 8 * i:15 + 8 * i].decode('latin-1').rstrip(' \0')
            d = chr(s0[15 + 8 * i] & 0x7F)
            e = s1[8 + 8 * i:16 + 8 * i]
            ln = e[4] | e[5] << 8 | ((e[6] >> 4) & 3) << 16
            st = e[7] | (e[6] & 3) << 8
            data = b''.join(sector(st + k) for k in range((ln + 255) // 256))[:ln]
            files['dfs%d/%s.%s' % (s, d, nm)] = data
    return files

def bpb(bps, spc, res, nfats, root, total, media, spf, spt, rootdir=b''):
    """A boot sector for a volume that has none. The volume label is copied from the root directory (fsck.fat
    insists that the two agree)."""
    b = bytearray(bps)
    b[0:3] = b'\xEB\x3C\x90'; b[3:11] = b'CHECKER '
    struct.pack_into('<HBHBHHBHHH', b, 11, bps, spc, res, nfats, root, total, media, spf, spt, 2)
    label = b'NO NAME    '
    for i in range(0, len(rootdir), 32):
        e = rootdir[i:i + 32]
        if e[0] == 0: break
        if e[0] != 0xE5 and e[11] & 0x08 and (e[11] & 0x0F) != 0x0F: label = e[:11]
    b[0x26] = 0x29; b[0x2B:0x36] = label; b[0x36:0x3E] = b'FAT12   '
    b[510:512] = b'\x55\xAA'
    return bytes(b)

def fat_files(vol):
    env = dict(os.environ, MTOOLSRC='/dev/null', MTOOLS_SKIP_CHECK='1', MTOOLS_NO_VFAT='1')
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 'v.img'); open(p, 'wb').write(vol)
        r = subprocess.run(['fsck.fat', '-n', p], capture_output=True, text=True)
        assert r.returncode == 0, 'fsck.fat: ' + r.stdout + r.stderr
        od = os.path.join(td, 'x'); os.mkdir(od)
        r = subprocess.run(['mcopy', '-s', '-n', '-i', p, '::/*', od], capture_output=True, text=True, env=env)
        assert r.returncode == 0, 'mcopy: ' + r.stderr
        files = {}
        for root, _, fs in os.walk(od):
            for f in fs:
                rel = os.path.relpath(os.path.join(root, f), od).replace(os.sep, '/')
                files['dos/' + rel.upper()] = open(os.path.join(root, f), 'rb').read()
        return files

def expected(d):
    out = {}
    for root, _, fs in os.walk(d):
        for f in fs:
            out[os.path.relpath(os.path.join(root, f), d).replace(os.sep, '/')] = open(os.path.join(root, f), 'rb').read()
    return out

def check(path):
    m = re.match(r't(\d+)_(il|seq)(_flip)?(_\w+)?\.', os.path.basename(path))
    fam, tb, cyl, ds = TYPES[int(m.group(1))]
    seq = (m.group(2) == 'seq') != bool(m.group(3))
    data = open(path, 'rb').read()
    img = to_interleaved(data, tb, cyl) if (ds and seq) else data
    if fam == 'adfs': got = adfs_files(img, side_first=ds)        # ADFS L numbers side 0 first
    elif fam == 'dfs': got = dfs_files(img, 2 if ds else 1)
    elif fam == 'pc': got = fat_files(img)
    elif fam == 'a800':    # no boot record: put a BPB sector in front (FAT then starts at sector 1)
        got = fat_files(bpb(1024, 1, 1, 1, 192, 801, 0xFD, 2, 5, img[2048:8192]) + img)
    else:                  # 640K hybrid: DOS numbers side 0 first; regroup into 512-byte sectors, BPB over the ADFS map
        logical = b''.join(img[(2 * t if t < 80 else 2 * (t - 80) + 1) * 4096:][:4096] for t in range(160))
        got = fat_files(bpb(512, 4, 8, 1, 112, 1280, 0xFF, 1, 8, logical[18 * 256:32 * 256]) + logical[512:])
        got.update(adfs_files(img, side_first=True))      # the ADFS side: just DOSBOOT, as on Acorn's discs
        m0, m1 = img[0:256], img[256:512]
        assert (m0[0xFC] | m0[0xFD] << 8 | m0[0xFE] << 16) == 2720 and m1[0xFE] == 0, 'hybrid map should say 2720 sectors, none free'
    want = expected(path + '.expect')
    if got != want:
        return 'MISMATCH: missing %s, extra %s, differ %s' % (sorted(set(want) - set(got)), sorted(set(got) - set(want)),
                                                             sorted(k for k in want if k in got and got[k] != want[k]))
    return 'ok (%d files, %s)' % (len(got), 'sequential' if seq else 'interleaved' if ds else 'single-sided')

if __name__ == '__main__':
    d = sys.argv[1]
    bad = 0
    for f in sorted(os.listdir(d)):
        if re.match(r't\d+_(il|seq)(_flip)?(_\w+)?\.[a-z]+$', f) and os.path.isdir(os.path.join(d, f + '.expect')):
            try: res = check(os.path.join(d, f))
            except AssertionError as e: res = 'FAIL: %s' % e
            if not res.startswith('ok'): bad += 1
            print('%-22s %s' % (f, res))
    print('ALL OK' if not bad else 'FAILED (%d)' % bad)
    sys.exit(1 if bad else 0)
