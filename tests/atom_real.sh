#!/bin/bash
# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# atom_real.sh <acorndw> <ATOM dir> <work dir>
# Acorn Atom DOS (series atom) on the owner's real data (testdata/ATOM, not in the repo): "System BASIC.ssd", an
# 80-track single-sided disc from an Acorn System, and three AtomInPC files (FORM40, FORMAT, INFALL: a 16-byte
# header, length 0). Checked by an independent reader here (python, from docs/disc-formats.md "Acorn Atom DOS"):
#  - the disc is read as Atom DOS, and every file acorndw gets (data, load, exec in the .inf) is what the catalogue says;
#  - the AtomInPC files put on a copy (--atominpc) are in the catalogue as the Atom's DOS writes it: directory a space,
#    16-bit load and exec from the header, the data after it, the first gap from the lowest sector up, the catalogue in
#    descending order, sector 1 bytes 4 and 6 as they were; and acorndw check passes.
# Ends "ALL OK" or "FAILED".
T=${1:?usage: atom_real.sh <acorndw> <ATOM dir> <work dir>}; A=${2:?}; W=${3:?}
T=$(readlink -f "$T"); A=$(cd "$A" && pwd)
[ -d "$W" ] && rm -r "$W"; mkdir -p "$W/out"; cd "$W" || exit 1
cp "$A/System BASIC.ssd" disc.ssd; cp "$A/System BASIC.ssd" orig.ssd
fail=0
say() { echo "$@"; }
"$T" info disc.ssd | tee info.txt | head -2
grep -q "^detection: open: Acorn Atom DOS single-sided 80-track (200K per side) (likely)" info.txt || { say "FAIL: not read as Atom DOS"; fail=1; }
"$T" get disc.ssd '$.*' out >/dev/null || { say "FAIL: get"; fail=1; }
"$T" put disc.ssd "$A/FORM40" "$A/FORMAT" "$A/INFALL" --atominpc / || { say "FAIL: put --atominpc"; fail=1; }
"$T" check disc.ssd >/dev/null || { say "FAIL: check"; fail=1; }
python3 -I - "$A" <<'PY' || fail=1
import os, struct, sys
A = sys.argv[1]
def catalogue(d):
    s0, s1 = d[0:256], d[256:512]
    out = []
    for i in range(1, s1[5] // 8 + 1):
        hi = s1[i*8+6]
        out.append(dict(name=bytes(s0[i*8:i*8+7]).rstrip(b' ').decode('latin1'), dir=s0[i*8+7] & 0x7F,
                        locked=bool(s0[i*8+7] & 0x80), load=s1[i*8] | s1[i*8+1] << 8, exec=s1[i*8+2] | s1[i*8+3] << 8,
                        length=s1[i*8+4] | s1[i*8+5] << 8 | (hi >> 4) << 16, start=s1[i*8+7] | (hi & 0x0F) << 8, hi=hi))
    return out
bad = []
orig = open('orig.ssd', 'rb').read()
new = open('disc.ssd', 'rb').read()
# 1. the files as they were, read back by acorndw get
for f in catalogue(orig):
    data = orig[f['start']*256:f['start']*256 + f['length']]
    got = open('out/' + f['name'], 'rb').read()
    inf = open('out/' + f['name'] + '.inf').read().split()
    if got != data: bad.append(f['name'] + ': data differs')
    if inf[0] != '$.' + f['name'] or int(inf[1], 16) != f['load'] or int(inf[2], 16) != f['exec']:
        bad.append(f['name'] + ': .inf ' + ' '.join(inf))
    if f['dir'] != 0x20: bad.append(f['name'] + ': not in the default directory')
# 2. the AtomInPC files as the Atom's DOS would have saved them
cat = catalogue(new)
names = {f['name']: f for f in cat}
for n in ('FORM40', 'FORMAT', 'INFALL'):
    h = open(os.path.join(A, n), 'rb').read()
    load, exe, length = struct.unpack('<HHI', h[:8])
    body = h[16:16 + length] if length else h[16:]
    f = names.get(n)
    if not f: bad.append(n + ': missing'); continue
    if f['dir'] != 0x20 or f['locked']: bad.append(n + ': directory byte')
    if (f['load'], f['exec'], f['length']) != (load, exe, len(body)): bad.append(n + ': load/exec/length %r' % ((f['load'], f['exec'], f['length']),))
    if f['hi'] & 0xF0: bad.append(n + ': entry byte 6 %02x' % f['hi'])
    if new[f['start']*256:f['start']*256 + f['length']] != body: bad.append(n + ': data differs')
starts = [f['start'] for f in cat]
if starts != sorted(starts, reverse=True): bad.append('catalogue not in descending order: %s' % starts)
# first fit from the lowest sector up, in the order put: each new file at the lowest gap big enough at the time
used = [(f['start'], f['start'] + (f['length'] + 255) // 256) for f in catalogue(orig)]
for n in ('FORM40', 'FORMAT', 'INFALL'):
    need = (names[n]['length'] + 255) // 256
    cur = 2
    for s, e in sorted(used):
        if s - cur >= need: break
        cur = max(cur, e)
    if names[n]['start'] != cur: bad.append('%s at %d, the first gap is at %d' % (n, names[n]['start'], cur))
    used.append((names[n]['start'], names[n]['start'] + need))
if new[256+4] != orig[256+4] or new[256+6] != orig[256+6] or new[256+7] != orig[256+7] or new[0:8] != orig[0:8] or new[256:260] != orig[256:260]:
    bad.append('sector 1 bytes 4, 6, 7 or the title changed')
for b in bad: print('FAIL: ' + b)
print('%d files read, %d added, checked' % (len(catalogue(orig)), 3))
sys.exit(1 if bad else 0)
PY
[ $fail = 0 ] && echo "ALL OK" || echo "FAILED"
exit $fail
