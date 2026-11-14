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
// containertest <work dir> <raw image> <kind> [<raw image> <kind> ...]
//   kind: adfs | ssd | dsd | hybrid | dos800 | pc   (what the raw image is; it must be stored interleaved)
// Acorn discs in ImageDisk, DSK and TeleDisk files (DiskWranglerCore's containers). Each raw image is copied into an
// ImageDisk, an extended DSK and a standard DSK file and a TeleDisk file written here, then for every copy:
//  * detection gives what it gives for the raw image, plus "in an ImageDisk file" and so on;
//  * every file reads back the same as from the raw image (ADFS tree, both DFS sides, DOS tree);
//  * TeleDisk opens read-only; the side order of a container can't be changed;
//  * the same change made to the raw image and to the ImageDisk and DSK copies (add a file, delete one, the boot
//    option or title) saves to the same disc: the copy's tracks hold exactly the raw file's bytes afterwards (DOS:
//    the same files, as DOS stamps the time);
//  * in the main window: the copy opens with the raw image's panes, Tools > Side order does nothing, a change saves
//    (TeleDisk: refused, with the read-only reason).
// Also: an ImageDisk file whose tracks differ in shape is not taken for an Acorn disc and doesn't open.
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTabWidget>
#include <QTimer>
#include <cstdio>
#include <functional>

#include "adfs.h"
#include <diskwrangler/fs/dfs/dfs.h>
#include "diskformat.h"
#include <diskwrangler/fs/fat/fat.h>
#include "mainwindow.h"
#include "windowhelp.h"

using namespace Adfs;
using dw::device::Container;
using dw::device::Kind;

static int failures = 0;
static QString g_lastBox;                // the text of the last message box, which the guard below closes
#define CHECK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", qPrintable(QString(m))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(m))); } while (0)

static QByteArray slurp(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
static void spit(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

struct Geometry { int seclen, sectrk, cyls, heads, firstId; };

static Geometry geometryOf(const QString &kind, qint64 size) {
    if (kind == "adfs" || kind == "hybrid") {
        if (size == 655360) return {256, 16, 80, 2, 0};
        return {256, 16, int(size / 4096), 1, 0};            // S, M: one side
    }
    if (kind == "ssd") return {256, 10, int(size / 2560), 1, 0};
    if (kind == "dsd") return {256, 10, int(size / 5120), 2, 0};
    if (kind == "dos800") return {1024, 5, 80, 2, 0};
    int spt = size == 368640 ? 9 : size == 737280 ? 9 : size == 1228800 ? 15 : 18;   // pc
    return {512, spt, int(size / (512 * spt * 2)), 2, 1};
}

// An uncompressed TeleDisk file ("TD", data stored as is): enough for a reader, no CRCs checked by it.
static QByteArray teleDisk(const QByteArray &raw, const Geometry &g) {
    QByteArray o("TD\0\0\x15\x02\x03\x00\x00", 9);
    o += char(g.heads); o += '\0'; o += '\0';
    int n = 0;
    while ((128 << n) < g.seclen) ++n;
    qint64 off = 0;
    for (int c = 0; c < g.cyls; ++c)
        for (int h = 0; h < g.heads; ++h) {
            o += char(g.sectrk); o += char(c); o += char(h); o += '\0';
            for (int s = 0; s < g.sectrk; ++s) {
                o += char(c); o += char(h); o += char(g.firstId + s); o += char(n); o += '\0'; o += '\0';
                const int len = g.seclen + 1;
                o += char(len & 0xFF); o += char(len >> 8); o += '\0';
                o += raw.mid(int(off), g.seclen);
                off += g.seclen;
            }
        }
    o += char(0xFF);
    return o;
}

// Opens an image the way the main window does.
static bool openLike(Image &img, const QString &path, DiskInfo *info, QString *err) {
    *info = detectDisk(path);
    bool raw = info->kind == DiskInfo::Dfs || info->kind == DiskInfo::DosPc || info->kind == DiskInfo::DosAcorn800;
    bool ok = raw ? img.openRaw(path, info->fullSize, err, info->sides)
                  : img.open(path, err, info->kind == DiskInfo::AdfsDosHybrid ? info->fullSize : 0, info->sides);
    if (ok && info->kind == DiskInfo::AdfsDosHybrid) img.setAdfsLocked(true);
    return ok;
}

static bool attachDos(dw::fat::Volume &v, Image &img, const DiskInfo &info, QString *err) {
    const quint32 len = quint32(img.fullSize());
    switch (info.kind) {
    case DiskInfo::AdfsDosHybrid: return v.attachFormat(&img, 0, len, dw::fat::FmtAcorn640, err);
    case DiskInfo::DosAcorn800: return v.attachFormat(&img, 0, len, dw::fat::FmtAcorn800, err);
    case DiskInfo::DosPc: return info.dosFromTable ? v.attachFormat(&img, 0, len, info.dosFormat, err) : v.attach(&img, 0, len, err);
    default: return false;
    }
}

// Every file on the disc, as "where/name length md5-ish" lines plus contents, in a fixed order.
static QStringList listing(Image &img, const DiskInfo &info) {
    QStringList out;
    QString err;
    auto sum = [](const QByteArray &b) { return QString(QCryptographicHash::hash(b, QCryptographicHash::Md5).toHex()) + ":" + QString::number(b.size()); };
    if (img.isAdfs()) {
        std::function<void(quint32, const QString &)> walk = [&](quint32 sec, const QString &where) {
            Dir d;
            if (!img.readDir(sec, &d, &err)) { out << where + " unreadable"; return; }
            out << where + " title " + d.title();
            for (const Entry &e : d.entries) {
                if (e.isDir()) { walk(e.sin, where + "." + e.name()); continue; }
                QByteArray b;
                img.readFile(e, &b, &err);
                out << QString("%1.%2 %3 %4 %5 %6").arg(where, e.name(), e.attrString()).arg(e.load, 8, 16, QChar('0')).arg(e.exec, 8, 16, QChar('0')).arg(sum(b));
            }
        };
        walk(RootSector, "$");
        out << QString("boot %1").arg(img.bootOption());
    }
    if (info.kind == DiskInfo::Dfs)
        for (int s = 0; s < (info.dfs.doubleSided ? 2 : 1); ++s) {
            dw::dfs::Disc d;
            if (!d.attach(&img, s, info.dfs, &err)) { out << QString("drive %1: %2").arg(s * 2).arg(err); continue; }
            out << QString("drive %1 title %2 boot %3").arg(s * 2).arg(d.title()).arg(d.bootOption());
            for (const dw::dfs::File &f : d.files()) {
                QByteArray b;
                d.readFile(f, &b, &err);
                out << QString("%1:%2 %3 %4").arg(s * 2).arg(f.fullName()).arg(f.load, 6, 16, QChar('0')).arg(sum(b));
            }
        }
    dw::fat::Volume v;
    if (attachDos(v, img, info, &err)) {
        out << "dos label " + v.label();
        std::function<void(quint32, const QString &, int)> walk = [&](quint32 dir, const QString &where, int depth) {
            QVector<dw::fat::DEntry> L;
            if (depth > 8 || !v.listDir(dir, &L, &err)) return;
            for (const dw::fat::DEntry &e : L) {
                if (e.name == "." || e.name == "..") continue;
                if (e.isDir()) { out << where + "\\" + e.name + "\\"; walk(e.cluster, where + "\\" + e.name, depth + 1); continue; }
                QByteArray b;
                v.readFile(e, &b, &err);
                out << QString("%1\\%2 %3").arg(where, e.name, sum(b));
            }
        };
        walk(0, "C:", 0);
    }
    return out;
}

static QByteArray stored(const QString &path) {
    Container c;
    QString e;
    QByteArray b;
    if (c.open(path, &e)) c.readStored(0, c.storedSize(), &b, &e);
    return b;
}

// The same change on any copy: returns false (and why) if it couldn't be made or saved.
static bool change(const QString &path, QString *err) {
    Image img;
    DiskInfo info;
    if (!openLike(img, path, &info, err)) return false;
    const QByteArray data = QByteArray("container test ").repeated(70);
    if (info.kind == DiskInfo::Adfs) {
        Dir root;
        if (!img.readDir(RootSector, &root, err)) return false;
        if (!img.addFile(RootSector, "CTEST", data, 0x1900, 0x8023, FlagR | FlagW, true, err)) return false;
        if (!img.setBootOption(2, err) || !img.setDiscTitle("CONTAINED", err)) return false;
        if (!root.entries.isEmpty() && !root.entries.first().isDir() && !img.deleteEntry(RootSector, root.entries.first().name(), err)) return false;
    } else if (info.kind == DiskInfo::Dfs) {
        for (int s = 0; s < (info.dfs.doubleSided ? 2 : 1); ++s) {
            dw::dfs::Disc d;
            if (!d.attach(&img, s, info.dfs, err)) return false;
            if (!d.addFile('C', QString("SIDE%1").arg(s), data, 0x1900, 0x8023, false, true, err)) return false;
            if (!d.setBootOption(3, err)) return false;
        }
    } else {
        dw::fat::Volume v;
        if (!attachDos(v, img, info, err)) return false;
        if (!v.addFile(0, "CTEST.TXT", data, true, err)) return false;
    }
    Report post;
    if (!img.commit(err, false, &post)) return false;
    return true;
}

static void oneImage(const QString &work, const QString &rawPath, const QString &kind) {
    const QString base = QFileInfo(rawPath).completeBaseName();
    printf("%s (%s)\n", qPrintable(base), qPrintable(kind));
    QString err;
    const QByteArray raw = slurp(rawPath);
    const Geometry g = geometryOf(kind, raw.size());
    const QString r = work + "/" + base + ".raw";
    spit(r, raw);
    DiskInfo ri = detectDisk(r);
    CHECK(ri.kind != DiskInfo::Unknown && !(ri.sides.doubleSided() && ri.sides.sequential), "raw image: " + ri.description);

    // the copies
    Container src;
    dw::device::RawLayout l;
    l.seclen = g.seclen; l.sectrk = g.sectrk; l.cyls = g.cyls; l.heads = g.heads; l.firstId = g.firstId; l.mode = kind == "ssd" || kind == "dsd" ? 2 : 5;
    CHECK(src.open(r, &err) && src.setRawLayout(l, &err), "raw image cut into tracks: " + err);
    struct Copy { QString path; Kind kind; QString container; };
    const QVector<Copy> copies = {{work + "/" + base + ".imd", Kind::Imd, "in an ImageDisk file"},
                                  {work + "/" + base + ".edsk", Kind::Edsk, "in an extended DSK file"},
                                  {work + "/" + base + ".sdsk", Kind::Dsk, "in a DSK file"}};
    for (const Copy &c : copies) {
        Container x = src;
        CHECK(x.saveAs(c.path, c.kind, &err), "write " + dw::device::kindName(c.kind) + ": " + err);
    }
    spit(work + "/" + base + ".td0", teleDisk(raw, g));

    Image ref;
    DiskInfo refInfo;
    CHECK(openLike(ref, r, &refInfo, &err), "open raw: " + err);
    const QStringList want = listing(ref, refInfo);
    CHECK(!want.isEmpty(), QString("raw image lists %1 line(s)").arg(want.size()));
    ref.close();

    QVector<Copy> all = copies;
    all.append({work + "/" + base + ".td0", Kind::Td0, "in a TeleDisk file (read-only)"});
    for (const Copy &c : all) {
        DiskInfo ci = detectDisk(c.path);
        QString rawDesc = ri.description;
        CHECK(ci.kind == ri.kind && ci.fullSize == quint64(raw.size()) && ci.description == rawDesc + ", " + c.container,
              QString("%1 detected: %2").arg(dw::device::kindName(c.kind), ci.description));
        CHECK(ci.sidesConfirmed && !ci.sidesForced && !(ci.sides.doubleSided() && ci.sides.sequential), "  its side order needs no detecting");
        Image img;
        DiskInfo oi;
        CHECK(openLike(img, c.path, &oi, &err), "  opens: " + err);
        CHECK(listing(img, oi) == want, "  every file reads as from the raw image");
        CHECK(img.containerKind() == c.kind && !img.isRawFile() && !img.needsPadding(), "  container kind");
        if (img.sideLayout().doubleSided()) CHECK(!img.setConvertOnSave(true, &err), "  side order can't be converted: " + err);
        if (c.kind == Kind::Td0) {
            CHECK(!img.editable(), "  TeleDisk opens read-only");
            if (img.isAdfs() && !img.adfsLocked()) CHECK(!img.addFile(RootSector, "X", "x", 0, 0, FlagR, false, &err) && err.contains("read-only"), "  and refuses changes: " + err);
        } else {
            CHECK(img.editable(), "  editable");
        }
    }

    // in the main window (before the change below, while the raw image is as it was)
    auto tabsOf = [](MainWindow &w) {
        QStringList t;
        for (int i = 0; i < w.tabWidget()->count(); ++i) t << w.tabWidget()->tabText(i);
        return t;
    };
    QStringList rawTabs;
    {
        MainWindow w;
        CHECK(w.openFile(r), "main window opens the raw image");
        rawTabs = tabsOf(w);
    }
    for (const Copy &c0 : all) {
        Copy c = c0;                                     // a copy of its own: the comparisons below need the original
        c.path = work + "/gui-" + QFileInfo(c0.path).fileName();
        QFile::remove(c.path);
        QFile::copy(c0.path, c.path);
        MainWindow w;
        CHECK(w.openFile(c.path) && w.document()->image().containerKind() == c.kind, "main window opens the " + dw::device::kindName(c.kind));
        CHECK(tabsOf(w) == rawTabs, "  with the same panes: " + tabsOf(w).join(", "));
        CHECK(!w.readWithSideOrder(true) && !w.convertSideOrder(true, false) && !w.document()->image().dirty(), "  Tools > Side order does nothing");
        const bool dos = w.document()->info().kind == DiskInfo::AdfsDosHybrid || w.document()->info().kind == DiskInfo::DosPc || w.document()->info().kind == DiskInfo::DosAcorn800;
        if (dos) continue;                               // DOS discs have no boot option to change here
        auto toDfs = [](MainWindow &m) { if (m.document()->info().kind == DiskInfo::Dfs) { m.tabWidget()->setCurrentWidget(wt::dfsTree(m)); wt::dfsTree(m)->setCurrentItem(m.dfsRoot(0)); } };
        toDfs(w);
        if (c.kind == Kind::Td0) {
            g_lastBox.clear();
            CHECK(!w.setBootOption(1) && g_lastBox.contains("read-only"), "  TeleDisk: a change is refused: " + g_lastBox);
            continue;
        }
        const int was = w.bootOption(), to = was == 1 ? 2 : 1;
        CHECK(w.setBootOption(to) && w.document()->image().dirty() && w.save() && !w.document()->image().dirty(), "  a change saves");
        MainWindow again;
        again.openFile(c.path);
        toDfs(again);
        CHECK(again.bootOption() == to && again.document()->image().containerKind() == c.kind, "  and is there when opened again");
    }

    // the same change everywhere
    err.clear();
    CHECK(change(r, &err), "change the raw image: " + err);
    const QByteArray after = slurp(r);
    Image ar;
    DiskInfo ai;
    openLike(ar, r, &ai, &err);
    const QStringList wantAfter = listing(ar, ai);
    CHECK(wantAfter != want, "the change shows in the listing");
    for (const Copy &c : copies) {
        err.clear();
        CHECK(change(c.path, &err), "change the " + dw::device::kindName(c.kind) + " copy: " + err);
        Image img;
        DiskInfo oi;
        openLike(img, c.path, &oi, &err);
        if (ai.kind == DiskInfo::Adfs || ai.kind == DiskInfo::Dfs)
            CHECK(stored(c.path) == after, "  its tracks hold exactly the raw file's bytes");
        CHECK(listing(img, oi) == wantAfter, "  and it lists the same");
        CHECK(Container().open(c.path, &err) && detectDisk(c.path).description.endsWith(c.container), "  still " + dw::device::kindName(c.kind));
    }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTimer guard;                        // message boxes are answered (and remembered) so that nothing waits for a person
    QObject::connect(&guard, &QTimer::timeout, [] {
        if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            g_lastBox = mb->text();
            mb->accept();
        }
    });
    guard.start(20);
    if (argc < 4 || argc % 2) { printf("usage: containertest <work dir> <raw image> <kind> [<raw image> <kind> ...]\n"); return 2; }
    const QString work = argv[1];
    QDir().mkpath(work);
    for (int i = 2; i + 1 < argc; i += 2) oneImage(work, argv[i], argv[i + 1]);

    printf("an ImageDisk file with tracks of different shapes\n");
    {
        Container odd;
        dw::device::RawLayout l;
        l.seclen = 256; l.sectrk = 16; l.cyls = 80; l.heads = 2; l.firstId = 0; l.mode = 5;
        odd.createBlank(Kind::Imd, l, QString());
        QString err;
        odd.reshapeTrack(0, 0, 10, 256, 0, 2, &err);
        CHECK(odd.saveAs(work + "/odd.imd", Kind::Imd, &err), "written: " + err);
        DiskInfo d = detectDisk(work + "/odd.imd");
        CHECK(d.kind == DiskInfo::Unknown && !d.notes.isEmpty(), "not an Acorn disc: " + d.notes.join(" "));
        Image img;
        CHECK(!img.open(work + "/odd.imd", &err) && err.contains("whole disc"), "doesn't open: " + err);
    }
    printf(failures ? "%d FAILED\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}
