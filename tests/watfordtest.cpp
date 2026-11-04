/*
 * Acorn Disk Wrangler - view and edit Acorn disc images (ADFS, DFS, DOS Plus, PC FAT)
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// watfordtest - Watford DFS 62-file discs.
//   watfordtest <outdir> [<copy of a real Watford .dsd>]
// Makes new 62-file discs, fills them past 31 files, checks the split between the two catalogues, the limits, the
// guard entry, an ordinary disc that only looks like one, and (with a real disc) that a save with nothing changed
// leaves it byte-identical and that edits keep every other file and the disc's own conventions. The independent
// reader for these discs is tests/sidecheck.py (sidetest makes every Watford type too).
#include "adfs.h"
#include "dfs.h"
#include "diskformat.h"
#include "newdisc.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QMap>
#include <cstdio>
using namespace Adfs;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static QByteArray rnd(int n, int seed) { QByteArray b(n, 0); quint32 s = seed * 2654435761u + 1; for (int i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; b[i] = char(s >> 24); } return b; }
static QByteArray slurp(const QString &p) { QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
static void spit(const QString &p, const QByteArray &b) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(b); }

struct Opened { DiskInfo info; Image img; Dfs::Disc d[2]; bool ok = false; };
static bool openIt(Opened &o, const QString &path) {
    o.info = detectDisk(path);
    QString e;
    if (o.info.kind != DiskInfo::Dfs || !o.img.openRaw(path, o.info.fullSize, &e, o.info.sides)) { printf("  open: %s\n", qPrintable(e)); return false; }
    for (int s = 0; s < (o.info.dfs.doubleSided ? 2 : 1); ++s)
        if (!o.d[s].attach(&o.img, s, o.info.dfs, &e)) { printf("  side %d: %s\n", s, qPrintable(e)); return false; }
    return o.ok = true;
}
typedef QMap<QString, QByteArray> Files;
static Files contents(Opened &o) {
    Files m;
    for (int s = 0; s < 2; ++s) {
        if (!o.d[s].attached()) continue;
        for (const Dfs::File &f : o.d[s].files()) {
            QByteArray b; QString e; CHECK(o.d[s].readFile(f, &b, &e));
            m[QString("%1:%2 %3 %4 %5").arg(s).arg(f.fullName()).arg(f.load, 0, 16).arg(f.exec, 0, 16).arg(int(f.locked))] = b;
        }
    }
    return m;
}
// one side's track 0 (sectors 0-3) straight from the file, interleaved .dsd or .ssd
static QByteArray track0(const QString &path, int side) { return slurp(path).mid(side * 2560, 1024); }
static quint32 cat1Size(const QByteArray &t) { return (quint32(quint8(t[256 + 6]) & 3) << 8) | quint8(t[256 + 7]); }
static quint32 cat3Size(const QByteArray &t) { return (quint32(quint8(t[768 + 6]) & 3) << 8) | quint8(t[768 + 7]); }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { printf("usage: watfordtest <outdir> [<copy of a real Watford .dsd>]\n"); return 2; }
    QString out = argv[1];
    QString e;

    // ---- new discs of every Watford type
    for (auto t : {NewDisc::WatSs40, NewDisc::WatSs80, NewDisc::WatDs40, NewDisc::WatDs80}) {
        const NewDisc::TypeInfo &ti = NewDisc::info(t);
        QString p = out + QString("/w%1.%2").arg(int(t)).arg(ti.suffix);
        printf("== %s\n", ti.name);
        CHECK(NewDisc::create(p, t, "WATFORD", false, &e));
        Opened o; CHECK(openIt(o, p)); if (!o.ok) continue;
        printf("  %s\n", qPrintable(o.info.description));
        CHECK(o.info.description.contains("Watford"));
        quint32 sec = (t == NewDisc::WatSs40 || t == NewDisc::WatDs40) ? 400 : 800;
        int sides = ti.doubleSided ? 2 : 1;
        for (int s = 0; s < sides; ++s) {
            Dfs::Disc &d = o.d[s];
            CHECK(d.watford()); CHECK(d.maxFiles() == 62); CHECK(d.title() == "WATFORD"); CHECK(d.sectors() == sec);
            CHECK(d.freeSectors() == sec - 4);          // sectors 0-3 are catalogue
            for (int i = 0; i < 62; ++i)
                CHECK(d.addFile(i % 2 ? 'A' : '$', QString("F%1").arg(i), rnd(100 + 37 * i, 1000 * s + i), 0x1900 + i, 0x8023, i % 5 == 0, false, &e));
            CHECK(d.files().size() == 62);
            CHECK(!d.addFile('$', "ONEMORE", rnd(10, 1), 0, 0, false, false, &e)); printf("  63rd file: %s\n", qPrintable(e));
            CHECK(d.files().size() == 62);
            for (const Dfs::File &f : d.files()) CHECK(f.start >= 4);
            Report r = d.check(); for (auto &x : r.errors) printf("  ERR %s\n", qPrintable(x)); CHECK(r.ok() && r.warnings.isEmpty());
        }
        Files before = contents(o);
        CHECK(o.img.commit(&e, false, nullptr));
        o.img.close();
        for (int s = 0; s < sides; ++s) {
            QByteArray t0 = track0(p, s);
            CHECK(quint8(t0[256 + 5]) == 31 * 8); CHECK(quint8(t0[768 + 5]) == 31 * 8);
            CHECK(cat1Size(t0) == sec); CHECK(cat3Size(t0) == sec);           // not lowered on a new disc
            // the second catalogue holds the files highest on the disc
            quint32 low2 = 0xFFFF, high1 = 0;
            for (int i = 1; i <= 31; ++i) {
                quint32 s1 = (quint32(quint8(t0[256 + i * 8 + 6]) & 3) << 8) | quint8(t0[256 + i * 8 + 7]);
                quint32 s3 = (quint32(quint8(t0[768 + i * 8 + 6]) & 3) << 8) | quint8(t0[768 + i * 8 + 7]);
                high1 = qMax(high1, s1); low2 = qMin(low2, s3);
            }
            CHECK(high1 < low2);
        }
        Opened o2; CHECK(openIt(o2, p));
        if (o2.ok) {
            CHECK(contents(o2) == before);
            // delete from the second catalogue's range and the first's: the split follows
            CHECK(o2.d[0].remove('$', "F60", &e)); CHECK(o2.d[0].remove('$', "F0", &e));
            CHECK(o2.d[0].check().ok());
            CHECK(o2.img.commit(&e, false, nullptr)); o2.img.close();
            QByteArray t0 = track0(p, 0);
            CHECK(quint8(t0[256 + 5]) == 31 * 8); CHECK(quint8(t0[768 + 5]) == 29 * 8);
            Opened o3; CHECK(openIt(o3, p)); if (o3.ok) { CHECK(o3.d[0].files().size() == 60); CHECK(o3.d[0].check().ok()); }
        }
    }

    // ---- fewer than 32 files: all in the first catalogue, the second empty but still marked
    {
        QString p = out + "/few.ssd"; CHECK(NewDisc::create(p, NewDisc::WatSs80, "", false, &e));
        Opened o; CHECK(openIt(o, p));
        if (o.ok) {
            for (int i = 0; i < 5; ++i) CHECK(o.d[0].addFile('$', QString("G%1").arg(i), rnd(500, i), 0, 0, false, false, &e));
            CHECK(o.img.commit(&e, false, nullptr)); o.img.close();
            QByteArray t0 = track0(p, 0);
            CHECK(quint8(t0[256 + 5]) == 5 * 8); CHECK(quint8(t0[768 + 5]) == 0);
            CHECK(t0.mid(512, 8) == QByteArray(8, char(0xAA)));
            Opened o2; CHECK(openIt(o2, p)); if (o2.ok) CHECK(o2.d[0].watford() && o2.d[0].files().size() == 5);
        }
    }

    // ---- an ordinary DFS disc whose first file (at sector 2) starts with eight &AA bytes is not a Watford disc
    {
        QString p = out + "/notwatford.ssd"; CHECK(NewDisc::create(p, NewDisc::DfsSs80, "", false, &e));
        Opened o; CHECK(openIt(o, p));
        if (o.ok) {
            QByteArray data = QByteArray(8, char(0xAA)) + QByteArray(600, 0);
            CHECK(o.d[0].addFile('$', "AAAA", data, 0, 0, false, false, &e));
            CHECK(o.d[0].files()[0].start == 2);
            CHECK(o.img.commit(&e, false, nullptr)); o.img.close();
            Opened o2; CHECK(openIt(o2, p)); if (o2.ok) { CHECK(!o2.d[0].watford()); CHECK(o2.d[0].maxFiles() == 31); CHECK(o2.d[0].files().size() == 1); }
            CHECK(!detectDisk(p).description.contains("Watford"));
        }
    }

    // ---- Watford's guard entry ($!.!!!!!!!, sectors 2-3, in the first catalogue): hidden, kept, counts as a file
    {
        QString p = out + "/guard.ssd"; CHECK(NewDisc::create(p, NewDisc::WatSs80, "", false, &e));
        QByteArray img = slurp(p);
        char *c = img.data();
        memcpy(c + 8, "!!!!!!!!", 8);             // name "!!!!!!!", directory '!'
        c[256 + 5] = 8;
        c[256 + 8 + 4] = 0; c[256 + 8 + 5] = 2;   // length &200
        c[256 + 8 + 7] = 2;                       // start sector 2
        spit(p, img);
        Opened o; CHECK(openIt(o, p));
        if (o.ok) {
            CHECK(o.d[0].watford()); CHECK(o.d[0].files().isEmpty()); CHECK(o.d[0].maxFiles() == 61); CHECK(o.d[0].check().ok());
            int added = 0;
            for (int i = 0; i < 62; ++i) if (o.d[0].addFile('$', QString("H%1").arg(i), rnd(256, i), 0, 0, false, false, &e)) ++added;
            CHECK(added == 61);
            CHECK(o.img.commit(&e, false, nullptr)); o.img.close();
            QByteArray t0 = track0(p, 0);
            CHECK(quint8(t0[256 + 5]) == 31 * 8); CHECK(quint8(t0[768 + 5]) == 31 * 8);
            CHECK(t0.mid(8 + 30 * 8, 8) == "!!!!!!!!");               // still there, last in the first catalogue
            Opened o2; CHECK(openIt(o2, p)); if (o2.ok) { CHECK(o2.d[0].files().size() == 61); CHECK(o2.d[0].check().ok()); }
        }
    }

    // ---- a real disc
    if (argc > 2) {
        QString p = argv[2];
        printf("== real disc %s\n", qPrintable(p));
        QByteArray orig = slurp(p);
        Opened o; CHECK(openIt(o, p));
        if (o.ok) {
            printf("  %s\n", qPrintable(o.info.description));
            for (int s = 0; s < 2; ++s) if (o.d[s].attached()) {
                Report r = o.d[s].check();
                for (auto &x : r.errors) printf("  ERR %s\n", qPrintable(x));
                for (auto &x : r.warnings) printf("  WARN %s\n", qPrintable(x));
                for (auto &x : r.info) printf("  %s\n", qPrintable(x));
                CHECK(r.ok());
            }
            Files before = contents(o);
            CHECK(o.img.commit(&e, false, nullptr)); o.img.close();
            CHECK(slurp(p) == orig);                               // nothing changed: byte-identical
            Opened o2; CHECK(openIt(o2, p));
            if (o2.ok) {
                QByteArray t0 = track0(p, 0);
                quint32 was1 = cat1Size(t0), was3 = cat3Size(t0);
                // rename a file of each catalogue, then add one and delete it again
                const Dfs::File top = o2.d[0].files().first(), bottom = o2.d[0].files().last();
                CHECK(o2.d[0].modify(top.dir, top.name, top.dir, "RENAMED", top.locked, top.load, top.exec, &e));
                CHECK(o2.d[0].modify(bottom.dir, bottom.name, bottom.dir, "RENAME2", bottom.locked, bottom.load, bottom.exec, &e));
                CHECK(o2.d[0].modify(top.dir, "RENAMED", top.dir, top.name, top.locked, top.load, top.exec, &e));
                CHECK(o2.d[0].modify(bottom.dir, "RENAME2", bottom.dir, bottom.name, bottom.locked, bottom.load, bottom.exec, &e));
                CHECK(o2.img.commit(&e, false, nullptr)); o2.img.close();
                t0 = track0(p, 0);
                CHECK(cat1Size(t0) == was1); CHECK(cat3Size(t0) == was3);   // the disc's own size convention is kept
                QByteArray a = slurp(p), b = orig;
                // only the cycle numbers (sector 1 and 3 byte 4, each side) may differ
                for (int s = 0; s < 2; ++s) for (int k : {256 + 4, 768 + 4}) { a[s * 2560 + k] = 0; b[s * 2560 + k] = 0; }
                CHECK(a == b);
                Opened o3; CHECK(openIt(o3, p));
                if (o3.ok) {
                    CHECK(contents(o3) == before);
                    int n = o3.d[0].files().size();
                    if (n < o3.d[0].maxFiles()) {
                        CHECK(o3.d[0].addFile('$', "NEWONE", rnd(2000, 9), 0x1900, 0x1900, false, false, &e));
                        CHECK(o3.d[0].check().ok());
                        CHECK(o3.img.commit(&e, false, nullptr)); o3.img.close();
                        Opened o4; CHECK(openIt(o4, p));
                        if (o4.ok) {
                            CHECK(o4.d[0].files().size() == n + 1); CHECK(o4.d[0].check().ok());
                            Files now = contents(o4); now.remove(QString("0:$.NEWONE 1900 1900 0"));
                            CHECK(now == before);
                        }
                    }
                }
            }
        }
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
