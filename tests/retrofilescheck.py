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
"""Check a retro-files drag (docs/retro-files-format.md) against the spec - independently of any program's code.

  retrofilescheck.py <dir> ...      each <dir> holds list.json (the JSON from the drag; its "base" points at the
                                    files) and optionally uri-list.txt (the drag's text/uri-list, one path per
                                    line, relative to base). tests/dragtest leaves such dumps as <outdir>/drag-*.

Checks: format/version/required keys; every item path is safe, exists, has the stated type and size, and comes
after its parent; DFS names are D.NAME; attribute namespaces are well-formed (acorn, fat, cpm; no file-type key - it lives in load/exec); "modified" is well-formed and
matches the host file's time; files in the base folder that aren't items are only Acorn .inf sidecars, and
each sidecar agrees with its item's acorn attributes; text/uri-list holds exactly the top-level items (plus
their sidecars). Standard library only.
"""
import datetime, json, os, re, sys
from urllib.parse import urlparse, unquote

HEX8 = re.compile(r'^[0-9A-F]{8}$')
STAMP = re.compile(r'^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$')

def check(d):
    problems = []
    bad = problems.append
    doc = json.load(open(os.path.join(d, 'list.json'), encoding='utf-8'))
    if doc.get('format') != 'retro-files': bad('format is not "retro-files"')
    if doc.get('version') != 1: bad('version is not 1')
    fs = doc.get('source', {}).get('filesystem')
    if not isinstance(fs, str) or not re.match(r'^[a-z0-9]+$', fs or ''): bad('source.filesystem missing or not a short lower-case name')
    u = urlparse(doc.get('base', ''))
    base = unquote(u.path) if u.scheme == 'file' else None
    if not base or not os.path.isdir(base): return problems + ['base is not a file: URL of a folder']
    items = doc.get('items')
    if not isinstance(items, list) or not items: return problems + ['items missing or empty']

    seen = {}
    for it in items:
        p = it.get('path', '')
        parts = p.split('/')
        if not p or p.startswith('/') or '..' in parts or '.' in parts or '' in parts or '\\' in p:
            bad('%r: unsafe path' % p); continue
        if p in seen: bad('%r listed twice' % p)
        parent = '/'.join(parts[:-1])
        if parent in seen and seen[parent] != 'directory': bad('%r: parent is not a directory' % p)
        if parent and parent not in seen and any(x.get('path') == parent for x in items): bad('%r: listed before its parent' % p)
        t = it.get('type')
        seen[p] = t
        full = os.path.join(base, *parts)
        if os.path.realpath(full) != os.path.join(os.path.realpath(base), *parts): bad('%r: leaves the base folder' % p)
        if not isinstance(it.get('name'), str) or not it['name']: bad('%r: no name' % p)
        elif fs == 'dfs' and not re.match(r'^[!-~]\.[^.]{1,7}$', it['name']): bad('%r: DFS name %r is not D.NAME' % (p, it['name']))
        if t == 'directory':
            if not os.path.isdir(full): bad('%r: not a folder on the host' % p)
        elif t == 'file':
            if not os.path.isfile(full): bad('%r: missing' % p); continue
            if it.get('size') != os.path.getsize(full): bad('%r: size %r but the file has %d bytes' % (p, it.get('size'), os.path.getsize(full)))
        else:
            bad('%r: type %r' % (p, t))
        if 'modified' in it:
            m = it['modified']
            if not isinstance(m, str) or not STAMP.match(m): bad('%r: modified %r is not YYYY-MM-DDThh:mm:ss' % (p, m))
            elif t == 'file':
                host = datetime.datetime.fromtimestamp(os.path.getmtime(full)).strftime('%Y-%m-%dT%H:%M:%S')
                if host != m: bad('%r: modified %s but the host file says %s' % (p, m, host))
        a = it.get('attributes', {})
        if not isinstance(a, dict): bad('%r: attributes is not an object' % p); continue
        ac = a.get('acorn')
        if ac is not None:
            for k in ('load', 'exec'):
                if k in ac and not (isinstance(ac[k], str) and HEX8.match(ac[k])): bad('%r: acorn.%s %r is not 8 hex digits' % (p, k, ac[k]))
            acc_ok = isinstance(ac.get('access', ''), str) and all(c in 'RWELrwel' for c in ac.get('access', '')) and len(set(ac.get('access', ''))) == len(ac.get('access', ''))
            if 'access' in ac and not acc_ok: bad('%r: acorn.access %r is not Stardot access letters (RWEL rwel, each once)' % (p, ac['access']))
            if t == 'file' and not ('load' in ac and 'exec' in ac): bad('%r: acorn file without load/exec' % p)
            if 'filetype' in ac: bad('%r: acorn.filetype is not part of the format (the type is in load)' % p)
        if 'dfs' in a: bad('%r: there is no dfs namespace (the directory is part of the name)' % p)
        for ns, keys in (('fat', ('readonly', 'hidden', 'system', 'archive')), ('cpm', ('readonly', 'system', 'archive'))):
            o = a.get(ns)
            if o is not None:
                for k in keys:
                    if k in o and not isinstance(o[k], bool): bad('%r: %s.%s is not true/false' % (p, ns, k))
        if 'cpm' in a and 'user' in a['cpm'] and not (isinstance(a['cpm']['user'], int) and 0 <= a['cpm']['user'] <= 31): bad('%r: cpm.user' % p)

        # an Acorn .inf sidecar next to the file must agree with the JSON
        inf = full + '.inf'
        if t == 'file' and ac is not None and os.path.isfile(inf):
            f = open(inf, encoding='latin-1').read().split()
            want_name = it['name']
            if f[0] != want_name: bad('%r: .inf name %r, expected %r' % (p, f[0], want_name))
            if (f[1], f[2]) != (ac['load'], ac['exec']): bad('%r: .inf addresses %s %s differ from the JSON' % (p, f[1], f[2]))
            if int(f[3], 16) != it['size']: bad('%r: .inf length differs' % p)
            bits = int(f[4], 16)
            acc = set(c for c, b in (('R', 1), ('W', 2), ('E', 4), ('L', 8), ('r', 16), ('w', 32), ('e', 64), ('l', 128)) if bits & b)
            if acc != set(ac['access']): bad('%r: .inf access %s, JSON %s' % (p, ''.join(sorted(acc)), ac['access']))

    # everything in the base folder is an item or a sidecar
    listed = set(seen)
    for root, dirs, files in os.walk(base):
        for n in dirs + files:
            rel = os.path.relpath(os.path.join(root, n), base).replace(os.sep, '/')
            if rel in listed: continue
            if n.lower().endswith('.inf') and rel[:-4] in listed: continue
            bad('%r is in the base folder but not listed' % rel)

    # text/uri-list: exactly the top-level items, plus sidecars
    ul = os.path.join(d, 'uri-list.txt')
    if os.path.isfile(ul):
        uris = [l.strip() for l in open(ul, encoding='utf-8') if l.strip()]
        tops = {p for p in listed if '/' not in p}
        got = {x for x in uris if not (x.lower().endswith('.inf') and x[:-4] in tops)}
        if got != tops: bad('text/uri-list has %s, the top-level items are %s' % (sorted(got), sorted(tops)))
    return problems

if __name__ == '__main__':
    failed = 0
    for d in sys.argv[1:]:
        try: problems = check(d)
        except (OSError, ValueError, KeyError, IndexError) as e: problems = ['cannot read: %s' % e]
        n = 0 if problems else len(json.load(open(os.path.join(d, 'list.json'), encoding='utf-8'))['items'])
        print('%-40s %s' % (os.path.basename(d.rstrip('/')), 'FAIL' if problems else 'ok (%d items)' % n))
        for p in problems: print('   ', p)
        failed += bool(problems)
    print('ALL OK' if not failed else 'FAILED (%d)' % failed)
    sys.exit(1 if failed else 0)
