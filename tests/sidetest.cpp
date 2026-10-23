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
// Side order (interleaved / sequential) and blank image tests.
//   sidetest <outdir> [<copy of a real 640K hybrid> [<copy of a real 800K DOS Plus disc> [<copy of a real ADFS L disc> ...]]]
// (real ADFS L discs: name the copies t2_il_<name>.adl in <outdir> so sidecheck.py also reads them independently)
// For every format NewDisc can make, in both side orders where it has two sides: create it, check detection, fill
// it with files and directories that reach well beyond track 0, check detection again (now it must be certain),
// convert it to the other order and back (the file must be exactly re-laid-out, then byte-identical), and read
// it in the WRONG order to make sure that does not look right. Each image is left in <outdir> in both orders,
// with the files it should contain under <image>.expect/, for tests/sidecheck.py to verify independently.
// The real discs (optional) are converted to sequential and back, comparing every file; the real hybrid is also
// the template for new DOS Plus 640K discs (made from it, and from its DOSBOOT file, compared with it). Without it
// a made-up DOSBOOT file is used. Also drives MainWindow (offscreen): the usual order for a blank disc (nothing is
// remembered), Read as, Convert + Save, and PC floppies having no side order.
#include "mainwindow.h"
#include "adfs.h"
#include "dfs.h"
#include "diskformat.h"
#include "dosfs.h"
#include "newdisc.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QSettings>
#include <QTimer>
#include <cstdio>
#include <functional>
using namespace Adfs;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)
static QByteArray rnd(int n,int seed){ QByteArray b(n,0); quint32 s=seed*2654435761u+1; for(int i=0;i<n;i++){ s=s*1664525u+1013904223u; b[i]=char(s>>24);} return b; }
static QByteArray slurp(const QString &p){ QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
static void spit(const QString &p, const QByteArray &d){ QDir().mkpath(QFileInfo(p).absolutePath()); QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

typedef QMap<QString, QByteArray> Files;

struct Opened {
    Image img; DiskInfo info; Dos::Volume dos; Dfs::Disc dfs[2]; bool ok = false; QString err;
};
// open the way MainWindow does; force = -1 detect, 0/1 = read as interleaved/sequential
static bool openAs(Opened &o, const QString &path, int force = -1) {
    o.info = detectDisk(path, force);
    bool raw = o.info.kind == DiskInfo::Dfs || o.info.kind == DiskInfo::DosPc || o.info.kind == DiskInfo::DosAcorn800;
    o.ok = raw ? o.img.openRaw(path, o.info.fullSize, &o.err, o.info.sides)
               : o.img.open(path, &o.err, o.info.kind == DiskInfo::AdfsDosHybrid ? o.info.fullSize : 0, o.info.sides);
    if (!o.ok) return false;
    quint32 len = quint32(o.info.fullSize);
    switch (o.info.kind) {
    case DiskInfo::AdfsDosHybrid: o.img.setAdfsLocked(true); o.ok = o.dos.attachFormat(&o.img, 0, len, Dos::FmtAcorn640, &o.err); break;
    case DiskInfo::DosAcorn800: o.ok = o.dos.attachFormat(&o.img, 0, len, Dos::FmtAcorn800, &o.err); break;
    case DiskInfo::DosPc: o.ok = o.info.dosFromTable ? o.dos.attachFormat(&o.img, 0, len, o.info.dosFormat, &o.err) : o.dos.attach(&o.img, 0, len, &o.err); break;
    case DiskInfo::Dfs:
        for (int s = 0; s < (o.info.dfs.doubleSided ? 2 : 1); ++s) o.ok = o.ok && o.dfs[s].attach(&o.img, s, o.info.dfs, &o.err);
        break;
    default: break;
    }
    return o.ok;
}

// every file on the disc, keyed by a path ("adfs:$.SUB.F", "dos:\\SUB\\F", "dfs0:$.F"); false if anything is unreadable
static bool readAll(Opened &o, Files *out) {
    QString e;
    bool good = true;
    if (o.img.isAdfs() && o.info.kind != DiskInfo::AdfsDosHybrid) {
        std::function<void(quint32, QString, int)> walk = [&](quint32 sec, QString path, int depth) {
            Dir d; if (depth > 10 || !o.img.readDir(sec, &d, &e)) { good = false; return; }
            for (const Entry &x : d.entries) {
                if (x.isDir()) { walk(x.sin, path + "." + x.name(), depth + 1); continue; }
                QByteArray b; if (!o.img.readFile(x, &b, &e)) { good = false; continue; }
                (*out)["adfs:" + path + "." + x.name()] = b;
            }
        };
        walk(RootSector, "$", 0);
    }
    if (o.dos.attached()) {
        std::function<void(quint32, QString, int)> walk = [&](quint32 cl, QString path, int depth) {
            QVector<Dos::DEntry> L; if (depth > 10 || !o.dos.listDir(cl, &L, &e)) { good = false; return; }
            for (const Dos::DEntry &x : L) {
                if (x.name == "." || x.name == "..") continue;
                if (x.isDir()) { walk(x.cluster, path + "\\" + x.name, depth + 1); continue; }
                QByteArray b; if (!o.dos.readFile(x, &b, &e)) { good = false; continue; }
                (*out)["dos:" + path + "\\" + x.name] = b;
            }
        };
        walk(0, "", 0);
    }
    for (int s = 0; s < 2; ++s)
        if (o.dfs[s].attached())
            for (const Dfs::File &f : o.dfs[s].files()) {
                QByteArray b; if (!o.dfs[s].readFile(f, &b, &e)) { good = false; continue; }
                (*out)[QString("dfs%1:").arg(s) + f.fullName()] = b;
            }
    return good;
}
static bool readAllPath(const QString &path, Files *out, int force = -1) { Opened o; if (!openAs(o, path, force)) return false; return readAll(o, out); }

// put files and directories on a freshly made disc (opened in its true order); returns what should be there
static Files fill(const QString &path, bool seq, int seed) {
    Files want;
    Opened o; CHECK(openAs(o, path, seq ? 1 : 0)); if (!o.ok) { printf("  open: %s\n", qPrintable(o.err)); return want; }
    QString e;
    if (o.info.kind == DiskInfo::Adfs) {
        quint32 cap = o.img.totalSectors() * 256;
        QByteArray a = rnd(int(cap / 3), seed), b = rnd(int(cap / 6), seed + 1), c = rnd(300, seed + 2);
        quint32 sub = 0;
        CHECK(o.img.addFile(RootSector, "BIG", a, 0x1900, 0x8023, FlagR | FlagW, false, &e));
        CHECK(o.img.makeDir(RootSector, "SUB", &e, &sub));
        CHECK(o.img.addFile(sub, "INSIDE", b, 0, 0, FlagR | FlagW, false, &e));
        quint32 deep = 0;
        CHECK(o.img.makeDir(sub, "DEEP", &e, &deep));
        CHECK(o.img.addFile(deep, "TINY", c, 0, 0, FlagR, false, &e));
        want["adfs:$.BIG"] = a; want["adfs:$.SUB.INSIDE"] = b; want["adfs:$.SUB.DEEP.TINY"] = c;
    } else if (o.info.kind == DiskInfo::Dfs) {
        for (int s = 0; s < (o.info.dfs.doubleSided ? 2 : 1); ++s) {
            quint32 cap = o.dfs[s].sectors() * 256;
            QByteArray a = rnd(int(cap / 2), seed + 10 * s), b = rnd(5000, seed + 10 * s + 1), c = rnd(77, seed + 10 * s + 2);
            CHECK(o.dfs[s].addFile('$', "BIG", a, 0x1900, 0x8023, false, false, &e));
            CHECK(o.dfs[s].addFile('A', "MID", b, 0x3000, 0x3000, true, false, &e));
            CHECK(o.dfs[s].addFile('$', "!BOOT", c, 0, 0, false, false, &e));
            QString k = QString("dfs%1:").arg(s);
            want[k + "$.BIG"] = a; want[k + "A.MID"] = b; want[k + "$.!BOOT"] = c;
        }
    } else {     // FAT: PC, 800K, hybrid
        quint32 cap = o.dos.totalClusters() * o.dos.clusterBytes();
        QByteArray a = rnd(int(cap / 3), seed), b = rnd(int(cap / 5), seed + 1), c = rnd(1000, seed + 2);
        quint32 sub = 0, deep = 0;
        CHECK(o.dos.addFile(0, "BIG.BIN", a, false, &e));
        CHECK(o.dos.makeDir(0, "SUB", &e, &sub));
        CHECK(o.dos.addFile(sub, "INSIDE.DAT", b, false, &e));
        CHECK(o.dos.makeDir(sub, "DEEP", &e, &deep));
        CHECK(o.dos.addFile(deep, "TINY.TXT", c, false, &e));
        want["dos:\\BIG.BIN"] = a; want["dos:\\SUB\\INSIDE.DAT"] = b; want["dos:\\SUB\\DEEP\\TINY.TXT"] = c;
    }
    if (!e.isEmpty() && fails) printf("  last error: %s\n", qPrintable(e));
    Report post;
    CHECK(o.img.commit(&e, false, &post));
    return want;
}

static bool convert(const QString &path, bool toSeq) {
    Opened o; if (!openAs(o, path)) return false;
    QString e;
    if (!o.img.setConvertOnSave(toSeq, &e)) { printf("  convert: %s\n", qPrintable(e)); return false; }
    if (!o.img.dirty()) return false;
    if (!o.img.commit(&e, false, nullptr)) { printf("  convert commit: %s\n", qPrintable(e)); return false; }
    return o.img.sideLayout().sequential == toSeq;
}

static void writeExpect(const QString &dir, const Files &f) {
    QDir(dir).removeRecursively();
    for (auto it = f.begin(); it != f.end(); ++it) {
        QString k = it.key();
        k.replace(':', '/').replace('\\', '/');
        spit(dir + "/" + k, it.value());
    }
}

static void roundTrip(const QString &path, const Files &want, const char *what) {
    DiskInfo d0 = detectDisk(path);
    bool seq = d0.sides.sequential;
    QByteArray before = slurp(path);
    CHECK(convert(path, !seq));
    QByteArray after = slurp(path);
    CHECK(after.size() == before.size());
    CHECK(after == (seq ? reorderSides(before, d0.sides, false) : reorderSides(before, d0.sides, true)));
    DiskInfo d1 = detectDisk(path);
    printf("  %s converted: %s | sides %s%s\n", what, qPrintable(d1.description), d1.sides.sequential ? "sequential" : "interleaved",
           d1.sidesConfirmed ? "" : " (unconfirmed)");
    CHECK(d1.kind == d0.kind); CHECK(d1.sides.sequential == !seq); CHECK(d1.sidesConfirmed);
    Files got; CHECK(readAllPath(path, &got)); CHECK(got == want);
    CHECK(convert(path, seq));
    CHECK(slurp(path) == before);              // and back: byte-identical
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("acorndiskwrangler-sidetest"); QApplication::setApplicationName("t");
    QSettings().clear();
    if (argc < 2) { printf("usage: sidetest <outdir> [<real hybrid copy> [<real 800K copy> [<real ADFS L copy> ...]]]\n"); return 2; }
    QString out = argv[1];
    QDir().mkpath(out);

    // ---- reorderSides is a pure permutation and its own inverse
    { SideLayout l; l.trackBytes = 512; l.cylinders = 4; QByteArray x = rnd(4096, 99);
      QByteArray s = reorderSides(x, l, true); CHECK(s != x); CHECK(reorderSides(s, l, false) == x);
      CHECK(s.mid(0, 512) == x.mid(0, 512)); CHECK(s.mid(4 * 512, 512) == x.mid(512, 512)); }   // side 1 cyl 0 moves to file track 4

    // DOSBOOT for new 640K discs: from the real hybrid if given, else made up
    QString realHyb = argc > 2 ? QString(argv[2]) : QString();
    QByteArray dosboot = rnd(2303, 77);
    if (!realHyb.isEmpty()) {
        Opened o; CHECK(openAs(o, realHyb)); Dir r; QString e; CHECK(o.img.readDir(RootSector, &r, &e));
        int i = r.indexOf("dosboot"); CHECK(i >= 0); if (i >= 0) CHECK(o.img.readFile(r.entries[i], &dosboot, &e));
    }
    QString dosbootFile = out + "/dosboot";
    spit(dosbootFile, dosboot);
    { QString e; CHECK(!NewDisc::create(out + "/nb.adl", NewDisc::Acorn640, "", false, &e)); printf("  640K without DOSBOOT: %s\n", qPrintable(e)); }

    int seed = 1;
    for (const NewDisc::TypeInfo &t : NewDisc::types()) {
        for (int sq = 0; sq < (t.doubleSided ? 2 : 1); ++sq, seed += 7) {
            QString name = QString("t%1_%2").arg(int(t.type)).arg(sq ? "seq" : "il");
            QString path = out + "/" + name + "." + t.suffix;
            printf("== %s  [%s]\n", t.name, sq ? "sequential" : "interleaved");
            QString e;
            bool made = NewDisc::create(path, t.type, "TESTDISC", sq, &e, dosbootFile);
            if (!made) printf("  create: %s\n", qPrintable(e));
            CHECK(made); if (!made) continue;
            DiskInfo d = detectDisk(path);
            printf("  blank: %s | sides %s%s\n", qPrintable(d.description), !d.sides.doubleSided() ? "-" : d.sides.sequential ? "sequential" : "interleaved",
                   d.sides.doubleSided() && !d.sidesConfirmed ? " (unconfirmed)" : "");
            CHECK(d.sides.doubleSided() == t.doubleSided);
            if (d.sidesConfirmed) CHECK(d.sides.sequential == bool(sq));
            { Opened o; CHECK(openAs(o, path, sq)); CHECK(o.img.openReport().ok());
              if (o.dos.attached()) { CHECK(o.dos.check().ok()); CHECK(o.dos.label() == "TESTDISC"); }
              for (int s = 0; s < 2; ++s) if (o.dfs[s].attached()) { CHECK(o.dfs[s].check().ok()); CHECK(o.dfs[s].title() == "TESTDISC"); }
              if (o.info.kind == DiskInfo::Adfs) { Dir r; CHECK(o.img.readDir(RootSector, &r, &e)); CHECK(r.title() == "TESTDISC"); } }

            Files want = fill(path, sq, seed);
            Files expectWant = want;                         // what sidecheck.py should find (incl. the hybrid's ADFS side)
            if (t.type == NewDisc::Acorn640) expectWant["adfs:$.dosboot"] = dosboot;
            DiskInfo d2 = detectDisk(path);
            printf("  filled: %s | sides %s%s\n", qPrintable(d2.description), !d2.sides.doubleSided() ? "-" : d2.sides.sequential ? "sequential" : "interleaved",
                   d2.sides.doubleSided() && !d2.sidesConfirmed ? " (unconfirmed)" : "");
            CHECK(d2.kind == d.kind);
            if (t.doubleSided) { CHECK(d2.sidesConfirmed); CHECK(d2.sides.sequential == bool(sq)); }
            Files got; CHECK(readAllPath(path, &got)); CHECK(got == want); CHECK(got.size() >= 3);
            if (t.doubleSided) {
                Files wrong; bool r = readAllPath(path, &wrong, sq ? 0 : 1);
                CHECK(!r || wrong != want);                                     // the wrong order must not read back right
                roundTrip(path, want, "image");
                // leave both orders for sidecheck.py
                QString other = out + "/" + name + "_flip." + t.suffix;
                QFile::remove(other); QFile::copy(path, other); CHECK(convert(other, !sq));
                writeExpect(other + ".expect", expectWant);
            }
            writeExpect(path + ".expect", expectWant);
        }
    }

    // ---- weak evidence: a few files in the root directory only. Detection may say it can't tell, but it must
    // never be sure of the wrong order.
    for (const NewDisc::TypeInfo &t : NewDisc::types()) {
        if (!t.doubleSided) continue;
        for (int sq = 0; sq < 2; ++sq) {
            QString path = out + QString("/root_%1_%2.%3").arg(int(t.type)).arg(sq).arg(t.suffix);
            QString e; CHECK(NewDisc::create(path, t.type, "", sq, &e, dosbootFile));
            Opened o; CHECK(openAs(o, path, sq));
            for (int i = 0; i < 3; ++i) {
                QByteArray d = rnd(3000 + 9000 * i, 500 + i);
                QString n = QString("F%1").arg(i);
                if (o.info.kind == DiskInfo::Adfs) CHECK(o.img.addFile(RootSector, n, d, 0, 0, FlagR | FlagW, false, &e));
                else if (o.info.kind == DiskInfo::Dfs) CHECK(o.dfs[0].addFile('$', n, d, 0, 0, false, false, &e));
                else CHECK(o.dos.addFile(0, n + ".BIN", d, false, &e));
            }
            CHECK(o.img.commit(&e, false, nullptr));
            DiskInfo d = detectDisk(path);
            printf("  root-only %-48s %s: detected %s%s\n", t.name, sq ? "seq" : "il ", d.sides.sequential ? "sequential" : "interleaved",
                   d.sidesConfirmed ? "" : " (unconfirmed)");
            CHECK(!d.sidesConfirmed || d.sides.sequential == bool(sq));
        }
    }

    // ---- real discs: convert to sequential and back, every file identical
    for (int i = 2; i < argc; ++i) {
        printf("== real disc %s\n", argv[i]);
        DiskInfo d = detectDisk(argv[i]);
        printf("  %s | sides %s%s\n", qPrintable(d.description), d.sides.sequential ? "sequential" : "interleaved", d.sidesConfirmed ? "" : " (unconfirmed)");
        CHECK(d.sides.doubleSided()); CHECK(!d.sides.sequential); CHECK(d.sidesConfirmed);
        Files want; CHECK(readAllPath(argv[i], &want)); CHECK(want.size() > 10);
        printf("  %d files\n", want.size());
        if (d.kind == DiskInfo::Adfs) writeExpect(QString(argv[i]) + ".expect", want);   // for sidecheck.py
        roundTrip(argv[i], want, "real disc");
        Files wrong; bool r = readAllPath(argv[i], &wrong, 1); CHECK(!r || wrong != want);
    }

    // ---- new DOS Plus 640K discs against the real one: the boot track copied exactly, or built the same way
    if (!realHyb.isEmpty()) {
        QByteArray realTrack = slurp(realHyb).left(4096);
        QString a = out + "/hyb_from_disc.adl", b = out + "/hyb_from_file.adl", e;
        CHECK(NewDisc::create(a, NewDisc::Acorn640, "NEWDISC", false, &e, realHyb));
        CHECK(slurp(a).left(4096) == realTrack);                    // same boot track, byte for byte
        CHECK(NewDisc::create(b, NewDisc::Acorn640, "NEWDISC", true, &e, dosbootFile));
        QByteArray tb = slurp(b).left(4096);
        FsMap mr, mb; QStringList x, y;
        CHECK(FsMap::parse(realTrack.left(512), &mr, &x, &y)); CHECK(FsMap::parse(tb.left(512), &mb, &x, &y));
        CHECK(mb.total == mr.total); CHECK(mb.total == 2720); CHECK(mb.runs.isEmpty() && mr.runs.isEmpty());
        Dir dr, db; CHECK(Dir::parse(RootSector, realTrack.mid(512, DirBytes), &dr, &e)); CHECK(Dir::parse(RootSector, tb.mid(512, DirBytes), &db, &e));
        CHECK(db.title() == dr.title()); CHECK(db.entries.size() == 1 && dr.entries.size() == 1);
        if (db.entries.size() == 1 && dr.entries.size() == 1) {
            const Entry &p = db.entries[0], &q = dr.entries[0];
            CHECK(p.name() == q.name()); CHECK(p.attrString() == q.attrString()); CHECK(p.load == q.load); CHECK(p.exec == q.exec);
            CHECK(p.length == q.length); CHECK(p.sin == q.sin);
        }
        CHECK(tb.mid(7 * 256, dosboot.size()) == realTrack.mid(7 * 256, dosboot.size()));   // DOSBOOT identical (not the slack after it)
        printf("  new 640K discs: boot track copied exactly / built to match ('%s', %s, %u bytes)\n",
               qPrintable(db.title()), db.entries.isEmpty() ? "-" : qPrintable(db.entries[0].name()), db.entries.isEmpty() ? 0u : db.entries[0].length);
        for (const QString &p : {a, b}) {
            DiskInfo d = detectDisk(p); CHECK(d.kind == DiskInfo::AdfsDosHybrid); CHECK(d.sidesConfirmed);
            Opened o; CHECK(openAs(o, p)); CHECK(o.img.openReport().ok()); CHECK(o.dos.check().ok()); CHECK(o.dos.label() == "NEWDISC");
            QVector<Dos::DEntry> L; CHECK(o.dos.listDir(0, &L, &e)); CHECK(L.isEmpty());             // no DOS files
        }
        CHECK(detectDisk(b).sides.sequential);
    }

    // ---- File > New image: the save dialog suggests a file name from the title (or "untitled")
    for (const QString &title : {QString("Games Disc"), QString("A/B:C"), QString()}) {
        MainWindow w;
        QString suggested;
        QTimer::singleShot(200, [&] {                                 // the New image dialog: pick ADFS L, type the title
            QDialog *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!d) { printf("  (no New image dialog)\n"); ++fails; return; }
            QComboBox *type = d->findChild<QComboBox *>();
            for (int i = 0; type && i < type->count(); ++i) if (type->itemData(i).toInt() == int(NewDisc::AdfsL)) type->setCurrentIndex(i);
            for (QLineEdit *le : d->findChildren<QLineEdit *>()) if (le->maxLength() == 10) le->setText(title);
            QTimer::singleShot(300, [&] {                             // the save dialog: read its suggestion, cancel
                QFileDialog *fd = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                if (fd) { suggested = fd->selectedFiles().value(0); fd->reject(); } else { printf("  (no save dialog)\n"); ++fails; }
            });
            d->accept();
        });
        QMetaObject::invokeMethod(&w, "newImageDialog", Qt::DirectConnection);
        QString want = title.isEmpty() ? "untitled.adl" : title == "Games Disc" ? "Games Disc.adl" : "A_B_C.adl";
        printf("  New image titled '%s': save dialog suggests '%s'\n", qPrintable(title), qPrintable(QFileInfo(suggested).fileName()));
        CHECK(QFileInfo(suggested).fileName() == want);
    }

    // ---- MainWindow: the usual order for a blank Acorn disc (nothing remembered), Read as, Convert + Save; PC: no side order
    {
        QString p = out + "/gui_blank800.img";
        QString e; CHECK(NewDisc::create(p, NewDisc::Acorn800, "", true, &e));
        CHECK(!detectDisk(p).sidesConfirmed);                    // blank FAT: can't be told
        MainWindow w; CHECK(w.openImage(p)); CHECK(!w.image().sideLayout().sequential);      // usual order: interleaved
        CHECK(w.readWithSideOrder(true)); CHECK(w.image().sideLayout().sequential);
        { MainWindow again; CHECK(again.openImage(p)); CHECK(!again.image().sideLayout().sequential); }   // not remembered
        QByteArray before = slurp(p);
        CHECK(w.convertSideOrder(false, false)); CHECK(w.image().dirty()); CHECK(w.image().convertPending());
        w.image().revert(); CHECK(!w.image().dirty());            // revert drops it
        CHECK(w.convertSideOrder(false, false)); CHECK(w.save());
        CHECK(!w.image().dirty()); CHECK(!w.image().sideLayout().sequential);
        CHECK(slurp(p) == reorderSides(before, w.image().sideLayout(), false));
        // a filled sequential ADFS L is detected as sequential
        QString q = out + "/gui_l.adl";
        CHECK(NewDisc::create(q, NewDisc::AdfsL, "GUI", true, &e)); fill(q, true, 4242);
        MainWindow x; CHECK(x.openImage(q)); CHECK(x.image().sideLayout().sequential); CHECK(x.image().openReport().ok());
        CHECK(x.tree()->topLevelItemCount() == 1);
        // PC floppies are always interleaved: no side order, nothing to convert, a "sequential" request is ignored
        QString pc = out + "/gui_pc.img";
        CHECK(NewDisc::create(pc, NewDisc::Pc1440, "", true, &e));
        CHECK(!detectDisk(pc).sides.doubleSided());
        MainWindow y; CHECK(y.openImage(pc)); CHECK(!y.image().sideLayout().doubleSided());
        CHECK(!y.convertSideOrder(true, false)); CHECK(!y.readWithSideOrder(true)); CHECK(!y.image().dirty());
    }
    QSettings().clear();
    printf(fails ? "FAILED (%d failures)\n" : "ALL OK (%d failures)\n", fails);
    return fails ? 1 : 0;
}
