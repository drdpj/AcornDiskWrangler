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
// voltest - Acorn Disk Wrangler's discs through DiskWranglerCore's filesystem interface (step 4): every kind of disc
// is detected, opened as a dw::fs::Disc (src/adfsvolume.h) and its mounts checked against the program's own classes;
// changes go through a Disc transaction (a failed one leaves the disc exactly as it was) and a checked save.
//   voltest <work dir> <ADFS L .adl> <hybrid .adl> <DFS .dsd> <PC 720K .img> <Acorn 800K> <hard disc scsi0.dat>
// The images are copied into the work dir first; the hard disc copy is deleted at the end.
#include "adfsvolume.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <diskwrangler/fs/dfs/dfsvolume.h>
#include <diskwrangler/fs/fat/fatvolume.h>

using namespace Adfs;
using dw::fs::Path;

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, qPrintable(QString(what))); ++fails; } \
                            else printf("  ok: %s\n", qPrintable(QString(what))); } while (0)

static QString copyIn(const QString &dir, const QString &src) {
    const QString to = dir + "/" + QFileInfo(src).fileName();
    QFile::remove(to);
    QFile::copy(src, to);
    QFile(to).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return to;
}

// Opened as the program opens it: detection by tiers, the best candidate's DiskInfo
static bool openIt(OpenedDisc &o, const QString &path, QString *err) {
    const dw::fs::Verdict v = detectCandidates(path);
    if (v.action != dw::fs::Verdict::Open) { *err = "detection says not to open it here"; return false; }
    return o.open(path, diskInfo(*v.bestCandidate()), err);
}

static QStringList labels(dw::fs::Disc &d) {
    QStringList l;
    for (const auto &m : d.mounts()) l << m.label + (m.readOnly ? "(ro)" : "") + ":" + m.vol->fsId();
    return l;
}

// The tree of a volume, every file's size and attributes, for comparing before and after
static QString tree(dw::fs::Volume *v, const Path &dir = Path(), int depth = 0) {
    QVector<dw::fs::Entry> l;
    QString err, out;
    if (depth > 12 || !v->list(dir, &l, &err)) return "?" + err;
    for (const dw::fs::Entry &e : l) {
        out += dir.child(e.name).toString() + QString(" %1").arg(e.size);
        for (auto it = e.attrs.begin(); it != e.attrs.end(); ++it) out += " " + it.key() + "=" + it.value().toString();
        out += "\n";
        if (e.isDir()) out += tree(v, dir.child(e.name), depth + 1);
    }
    return out;
}

// Step 6b: is the free space map Image keeps in memory exactly what reload() reads from the staging? The map's own
// numbers, and the bytes the next map write makes from it (a boot-option write: the free list, the size, the disc name
// and id, both checksums). Both are written, compared and put back; reload() is left done (the same state, if so).
static bool adfsFaithful(Image &img, QString *why) {
    const auto snap = img.container().snapshot();
    const quint32 freeM = img.freeSectors(), runM = img.largestFreeRun(), totalM = img.totalSectors();
    QString e;
    QByteArray a, b;
    const bool wa = img.setBootOption(img.bootOption(), &e) && img.linear().read(0, 512, &a, &e);
    img.container().restore(snap);
    if (!img.reload(&e)) { *why = "reload: " + e; return false; }
    if (img.freeSectors() != freeM || img.largestFreeRun() != runM || img.totalSectors() != totalM) {
        *why = QString("free %1/%2, largest %3/%4, total %5/%6").arg(freeM).arg(img.freeSectors()).arg(runM).arg(img.largestFreeRun())
                   .arg(totalM).arg(img.totalSectors());
        return false;
    }
    const bool wb = img.setBootOption(img.bootOption(), &e) && img.linear().read(0, 512, &b, &e);
    img.container().restore(snap);
    if (!wa || !wb || a != b) { *why = "the next map write differs" + (e.isEmpty() ? QString() : ": " + e); return false; }
    return true;
}

// An ImageDisk file with every sector of one track recorded as "data unavailable" (IMD record type 0): writes to that
// track fail, as on a real damaged disc.
static bool imdTrackUnavailable(const QString &path, int cyl, int head, QString *why) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *why = "can't read " + path; return false; }
    const QByteArray in = f.readAll();
    f.close();
    int at = in.indexOf('\x1a');
    if (at < 0) { *why = "no IMD header"; return false; }
    QByteArray out = in.left(++at);
    bool found = false;
    while (at + 5 <= in.size()) {
        const int c = quint8(in[at + 1]), hf = quint8(in[at + 2]), n = quint8(in[at + 3]), sz = 128 << quint8(in[at + 4]);
        int q = at + 5 + n + ((hf & 0x80) ? n : 0) + ((hf & 0x40) ? n : 0);
        const bool target = c == cyl && (hf & 0x0F) == head;
        out += in.mid(at, q - at);
        for (int k = 0; k < n; ++k) {
            const int t = quint8(in[q]);
            const int len = t == 0 ? 0 : (t % 2 ? sz : 1);
            if (target) out += char(0);
            else out += in.mid(q, 1 + len);
            q += 1 + len;
        }
        found |= target;
        at = q;
    }
    if (!found) { *why = "no such track"; return false; }
    if (!f.open(QIODevice::WriteOnly)) { *why = "can't write " + path; return false; }
    f.write(out);
    return true;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 8) { printf("usage: voltest <work dir> <adfs L> <hybrid> <dsd> <pc720> <acorn800> <scsi0.dat>\n"); return 2; }
    const QString W = argv[1];
    QDir().mkpath(W);
    QString err;

    printf("ADFS L floppy\n");
    {
        const QString p = copyIn(W, argv[2]);
        OpenedDisc o;
        CHECK(openIt(o, p, &err), "open: " + err);
        CHECK(labels(o.disc()) == QStringList({"ADFS:adfs"}), labels(o.disc()).join(", "));
        auto *v = static_cast<AdfsVolume *>(o.disc().volume(0));
        Dir root;
        o.image().readDir(RootSector, &root, &err);
        QVector<dw::fs::Entry> l;
        CHECK(v->list(Path(), &l, &err) && l.size() == root.entries.size(), "the root lists what Adfs::Image reads");
        CHECK(v->caps().dirs && v->caps().maxEntriesPerDir == 47 && v->info().total == qint64(o.image().totalSectors()) * 256, "caps and info");
        const QString before = tree(v);
        const auto snap = o.image().container().snapshot();
        CHECK(!o.disc().transact([&](QString *e) {
                  return v->mkdir(Path(), "Games", e) &&
                         v->write(Path({"Games"}), "Elite", QByteArray(3000, 'e'), {{"load", 0x1900u}, {"exec", 0x8023u}, {"locked", true}}, QDateTime(), false, e) &&
                         v->write(Path(), "Huge", QByteArray(700 * 1024, 'h'), {}, QDateTime(), false, e);
              }, &err), "a transaction that runs out of room fails: " + err);
        CHECK(o.image().container().snapshot() == snap && tree(v) == before && o.image().validate().ok(),
              "and leaves the disc exactly as it was (the map read again)");
        CHECK(o.disc().transact([&](QString *e) {
                  return v->mkdir(Path(), "Games", e) &&
                         v->write(Path({"Games"}), "Elite", QByteArray(3000, 'e'), {{"load", 0x1900u}, {"exec", 0x8023u}, {"locked", true}}, QDateTime(), false, e);
              }, &err), "mkdir and write in a transaction: " + err);
        dw::fs::Entry e;
        CHECK(v->stat(Path({"games", "elite"}), &e, &err) && e.size == 3000 && e.attrs["locked"].toBool() && e.attrs["read"].toBool() &&
                  e.attrs["load"].toUInt() == 0x1900u, "stat (any case)");
        QJsonObject a = v->toRetro(e)["acorn"];
        CHECK(a["access"].toString() == "LWR" && a["load"].toString() == "00001900", "toRetro as the drag-out writes it");
        CHECK(v->setAttrs(Path({"Games", "Elite"}), {{"locked", false}, {"execute", true}}, &err) && v->stat(Path({"Games", "Elite"}), &e, &err) &&
                  !e.attrs["locked"].toBool() && e.attrs["execute"].toBool() && e.attrs["exec"].toUInt() == 0x8023u, "setAttrs: only those keys");
        CHECK(v->rename(Path({"Games", "Elite"}), Path({"Games", "Elite2"}), &err), "rename: " + err);
        CHECK(v->setVolumeAttrs({{"title", "VOLTEST"}, {"boot", 2}}, &err) && v->volumeValues()["title"] == "VOLTEST" &&
                  v->volumeValues()["boot"].toInt() == 2, "title and boot option");
        // DFS files dropped on $.Games: letters become subdirectories, ^ can't be a name
        dw::TransferItem t;
        t.sourceFs = "dfs";
        t.name = "A.PROG";
        dw::fs::Placement pl = v->placeIncoming(t, Path({"Games"}));
        CHECK(pl.dir == Path({"Games", "A"}) && pl.name == "PROG" && pl.warning.isEmpty(), "A.PROG -> Games.A.PROG");
        t.name = "$.BOOT";
        pl = v->placeIncoming(t, Path({"Games"}));
        CHECK(pl.dir == Path({"Games"}) && pl.name == "BOOT", "$.BOOT -> Games.BOOT");
        t.name = "^.X";
        pl = v->placeIncoming(t, Path({"Games"}));
        CHECK(pl.dir == Path({"Games"}) && pl.name == "X_X" && !pl.warning.isEmpty(), "^.X -> Games.X_X, with a warning");
        v->setFlattenLetters(true);
        t.name = "A.PROG";
        pl = v->placeIncoming(t, Path({"Games"}));
        CHECK(pl.dir == Path({"Games"}) && pl.name == "PROG", "flattened: A.PROG -> Games.PROG");
        const QString after = tree(v);
        CHECK(o.save(&err), "checked save: " + err);
        OpenedDisc o2;
        CHECK(openIt(o2, p, &err) && tree(o2.disc().volume(0)) == after && o2.image().validate().ok(), "saved and read back the same");
        QByteArray d;
        CHECK(o2.disc().volume(0)->read(Path({"Games", "Elite2"}), &d, &err) && d == QByteArray(3000, 'e'), "file contents");
        CHECK(o2.disc().volume(0)->remove(Path({"Games"}), &err) && o2.image().validate().ok() &&
                  o2.disc().volume(0)->info().free == qint64(o2.image().freeSectors()) * 256, "remove a directory with its files");
    }

    // Step 6b: Adfs::Image keeps no snapshot of its own. A change that fails half way leaves what it did; the document's
    // Disc::transact puts the staging back and has AdfsVolume reload, and Image::reload() gives exactly the map the
    // changes left in memory.
    printf("Step 6b: ADFS: Disc::transact is the only rollback, and reload() is faithful\n");
    {
        err.clear();
        const QString p = copyIn(W, argv[2]);
        QFile::rename(p, W + "/staging.adl");
        const QString q = W + "/staging.adl";
        OpenedDisc o;
        CHECK(openIt(o, q, &err), "open: " + err);
        Image &img = o.image();
        auto *v = static_cast<AdfsVolume *>(o.disc().volume(0));
        QString why;
        quint32 sub = 0, deep = 0;
        CHECK(img.addFile(RootSector, "Big", QByteArray(20000, 'b'), 0x1900, 0x8023, FlagR | FlagW, false, &err) &&
                  img.makeDir(RootSector, "Sub", &err, &sub) && img.makeDir(sub, "Deep", &err, &deep) &&
                  img.addFile(deep, "Tiny", "t", 0, 0, FlagR, false, &err) && adfsFaithful(img, &why), "adds and directories: " + err + why);
        bool added = true;
        for (int i = 0; i < 30 && added; ++i)
            added = img.addFile(sub, QString("F%1").arg(i), QByteArray(300 + 700 * (i % 5), char('a' + i % 26)), 0, 0, FlagR | FlagW, false, &err);
        CHECK(added && adfsFaithful(img, &why), "30 files: " + err + why);
        for (int i = 0; i < 30 && added; i += 3) added = img.deleteEntry(sub, QString("F%1").arg(i), &err);
        CHECK(added && adfsFaithful(img, &why), "every third deleted: the free list in fragments: " + err + why);
        CHECK(img.addFile(sub, "F1", QByteArray(9000, 'r'), 0, 0, FlagR, true, &err) && adfsFaithful(img, &why), "a replace, larger: " + err + why);
        CHECK(img.addFile(RootSector, "Big", QByteArray(500, 's'), 0, 0, FlagR, true, &err) && adfsFaithful(img, &why), "a replace, smaller: " + err + why);
        Dir root;
        CHECK(img.readDir(RootSector, &root, &err), "root: " + err);
        Entry moved = root.entries[root.indexOf("Sub")];
        moved.setName("Moved");
        CHECK(img.replaceEntry(RootSector, "Sub", moved, &err) && img.setDiscTitle("STAGED", &err) && img.setBootOption(2, &err) &&
                  adfsFaithful(img, &why), "a directory renamed, the title, the boot option: " + err + why);
        CHECK(img.deleteEntry(RootSector, "Moved", &err) && adfsFaithful(img, &why), "a directory deleted with its files: " + err + why);
        CHECK(img.addFile(RootSector, "Big", QByteArray(20000, 'B'), 0x1900, 0x8023, FlagR | FlagW, true, &err), "Big again: " + err);
        // a replace that doesn't fit: the old file's sectors are freed in the map in memory, then there is no room
        const auto snap = img.container().snapshot();
        const QString before = tree(v);
        const quint32 freeBefore = img.freeSectors();
        const QByteArray huge(int(freeBefore + 200) * 256, 'h');
        CHECK(!o.disc().transact([&](QString *e) { return v->write(Path(), "Big", huge, {}, QDateTime(), true, e); }, &err),
              "a replace that doesn't fit fails: " + err);
        QByteArray back;
        CHECK(img.container().snapshot() == snap && tree(v) == before && img.freeSectors() == freeBefore &&
                  v->read(Path({"Big"}), &back, &err) && back == QByteArray(20000, 'B') && img.validate().ok(),
              "the transaction put back the staging and the map: Big is there as it was");
        QString e2;
        CHECK(!img.addFile(RootSector, "Big", huge, 0, 0, FlagR, true, &e2) && img.freeSectors() > freeBefore,
              "without a transaction the map in memory has Big's sectors freed (" + e2 + ")");
        img.container().restore(snap);
        CHECK(img.reload(&err) && img.freeSectors() == freeBefore && tree(v) == before, "the staging put back by hand and reloaded: " + err);
        CHECK(!o.disc().transact([&](QString *e) {
                  return v->remove(Path({"Big"}), e) && v->mkdir(Path(), "NewDir", e) && v->setVolumeAttrs({{"title", "CHANGED"}}, e) &&
                         v->rename(Path({"NoSuch"}), Path({"X"}), e);
              }, &err) && img.container().snapshot() == snap && tree(v) == before && img.freeSectors() == freeBefore,
              "a remove, a directory, a title, then a failure: all put back: " + err);
        CHECK(o.save(&err), "checked save: " + err);
        OpenedDisc o2;
        CHECK(openIt(o2, q, &err) && tree(o2.disc().volume(0)) == before && o2.image().freeSectors() == freeBefore, "read back the same");

        // A staged write that fails half way (an ImageDisk file whose last track is unreadable): the file's data reaches
        // the bad track only after the free space map in memory has given it its sectors.
        const QString imd = W + "/staging-bad.imd";
        QFile::remove(imd);
        CHECK(o2.writeCopy(imd, dw::device::Kind::Imd, QString(), false, &err), "ImageDisk copy: " + err);
        const int lastTrack = int((o2.image().totalSectors() - 1) / 16);          // ADFS L: 16 sectors a track, interleaved
        CHECK(imdTrackUnavailable(imd, lastTrack / 2, lastTrack % 2, &why), "its last track made unreadable: " + why);
        OpenedDisc o3;
        CHECK(openIt(o3, imd, &err) && o3.image().editable(), "the damaged copy opens, editable: " + err);
        auto *v3 = static_cast<AdfsVolume *>(o3.disc().volume(0));
        const quint32 run = o3.image().largestFreeRun();
        const auto snap3 = o3.image().container().snapshot();
        const QString before3 = tree(v3);
        CHECK(!o3.disc().transact([&](QString *e) { return v3->write(Path(), "Fill", QByteArray(int(run) * 256, 'f'), {}, QDateTime(), false, e); }, &err),
              "a file reaching the unreadable track fails: " + err);
        CHECK(o3.image().container().snapshot() == snap3 && tree(v3) == before3 && o3.image().largestFreeRun() == run && o3.image().validate().ok(),
              "and is put back: the staging, the directory and the map");
        CHECK(!o3.image().addFile(RootSector, "Fill", QByteArray(int(run) * 256, 'f'), 0, 0, FlagR, false, &e2) &&
                  o3.image().largestFreeRun() != run, "without a transaction the map in memory keeps the sectors it gave (" + e2 + ")");
        o3.image().container().restore(snap3);
        CHECK(o3.image().reload(&err) && o3.image().largestFreeRun() == run && tree(v3) == before3, "the staging put back by hand and reloaded: " + err);
    }

    printf("640K hybrid\n");
    {
        const QString p = copyIn(W, argv[3]);
        OpenedDisc o;
        CHECK(openIt(o, p, &err), "open: " + err);
        CHECK(labels(o.disc()) == QStringList({"ADFS(ro):adfs", "DOS:fat"}), labels(o.disc()).join(", "));
        auto *dos = o.disc().volume(1);
        CHECK(dos->write(Path(), "HELLO.TXT", "hello", {}, QDateTime::currentDateTime(), false, &err), "write on the DOS side: " + err);
        CHECK(!o.disc().volume(0)->mkdir(Path(), "NOPE", &err), "the ADFS side is read-only: " + err);
        CHECK(o.disc().validateAll().ok() && o.save(&err), "save: " + err);
        OpenedDisc o2;
        QByteArray d;
        CHECK(openIt(o2, p, &err) && o2.disc().volume(1)->read(Path({"HELLO.TXT"}), &d, &err) && d == "hello", "read back");
    }

    printf("DFS double-sided\n");
    {
        const QString p = copyIn(W, argv[4]);
        OpenedDisc o;
        CHECK(openIt(o, p, &err), "open: " + err);
        CHECK(labels(o.disc()) == QStringList({"drive 0:dfs", "drive 2:dfs"}), labels(o.disc()).join(", "));
        CHECK(o.disc().volume(1)->write(Path(), "B.SIDE2", "two", {{"load", 0x31900u}}, QDateTime(), false, &err), "write on drive 2");
        CHECK(o.save(&err), "save: " + err);
        OpenedDisc o2;
        dw::fs::Entry e;
        CHECK(openIt(o2, p, &err) && o2.disc().volume(1)->stat(Path({"B.SIDE2"}), &e, &err) && e.attrs["load"].toUInt() == 0x31900u,
              "read back on drive 2");
    }

    printf("PC 720K and Acorn 800K\n");
    for (int i : {5, 6}) {
        const QString p = copyIn(W, argv[i]);
        OpenedDisc o;
        CHECK(openIt(o, p, &err), QString(argv[i]) + ": " + err);
        CHECK(labels(o.disc()) == QStringList({"DOS:fat"}), labels(o.disc()).join(", "));
        CHECK(o.disc().volume(0)->mkdir(Path(), "VOLTEST", &err) && o.disc().volume(0)->write(Path({"VOLTEST"}), "A.TXT", "a", {}, QDateTime(), false, &err) &&
                  o.save(&err), "write and save: " + err);
        OpenedDisc o2;
        QByteArray d;
        CHECK(openIt(o2, p, &err) && o2.disc().volume(0)->read(Path({"VOLTEST", "A.TXT"}), &d, &err) && d == "a", "read back");
    }

    printf("hard disc with DOS Plus\n");
    {
        const QString p = copyIn(W, argv[7]);
        OpenedDisc o;
        CHECK(openIt(o, p, &err), "open: " + err);
        CHECK(labels(o.disc()) == QStringList({"ADFS:adfs", "DOS:fat"}), labels(o.disc()).join(", "));
        auto *fv = static_cast<dw::fat::FsVolume *>(o.disc().volume(1));
        QVector<dw::fat::DEntry> L;
        QVector<dw::fs::Entry> l;
        fv->fat()->listDir(0, &L, &err);
        int real = 0;
        for (const dw::fat::DEntry &x : L) real += x.name != "." && x.name != ".." && (x.attr & 0x08) == 0;
        CHECK(fv->list(Path(), &l, &err) && l.size() == real, QString("DRIVE_C's root: %1 entries").arg(l.size()));
        CHECK(o.disc().validateAll().ok(), "every mount checks out");
        const QString before = tree(o.disc().volume(0)) + tree(fv);
        CHECK(!o.disc().transact([&](QString *e) {
                  return fv->write(Path(), "NEW.TXT", "new", {}, QDateTime(), false, e) &&
                         o.disc().volume(0)->write(Path({"NOSUCHDIR"}), "X", "x", {}, QDateTime(), false, e);
              }, &err), "a transaction across both mounts fails: " + err);
        CHECK(tree(o.disc().volume(0)) + tree(fv) == before, "both put back");
        QFile::remove(p);
    }

    printf("the window's rules: only a volume the changes touch vetoes a save (step 5i)\n");
    {
        err.clear();
        const QString p = copyIn(W, argv[7]);
        {                                   // a file in DRIVE_C's root, for the test
            OpenedDisc o;
            CHECK(openIt(o, p, &err) && o.disc().transact([&](QString *e) {
                      return o.disc().volume(1)->write(Path(), "BROKEN.TXT", "xyz", {}, QDateTime(), false, e); }, &err) && o.save(&err),
                  "BROKEN.TXT written into DRIVE_C: " + err);
        }
        {                                   // its directory entry broken on the disc: a size but no cluster
            Image img;
            Dir root, dosDir;
            QFile f(p);
            CHECK(img.open(p, &err) && img.readDir(RootSector, &root, &err) &&
                      img.readDir(root.entries[root.indexOf("DOS")].sin, &dosDir, &err), "find $.DOS.DRIVE_C: " + err);
            const Entry c = dosDir.entries[dosDir.indexOf("DRIVE_C")];
            img.close();
            CHECK(f.open(QIODevice::ReadWrite), "open the copy");
            f.seek(qint64(c.sin) * SectorSize);
            const QByteArray part = f.read(qMin<qint64>(c.length, 4 * 1024 * 1024));
            const int at = part.indexOf("BROKEN  TXT");
            CHECK(at >= 0 && at % 32 == 0, "its directory entry");
            for (int k : {20, 21, 26, 27}) { f.seek(qint64(c.sin) * SectorSize + at + k); f.write(QByteArray(1, 0)); }
            f.close();
        }
        auto openWindow = [&](OpenedDisc &o) {
            o.setWindowRules(true);
            return openIt(o, p, &err);
        };
        {
            OpenedDisc o;
            CHECK(openWindow(o) && labels(o.disc()) == QStringList({"ADFS:adfs", "DOS:fat"}), "opened with the window's rules: " + err);
            const dw::fs::Report dr = o.dos().check();
            CHECK(!dr.ok() && dr.errors.join("|").contains("file has a size but no clusters"), "the DOS partition fails its check: " + dr.errors.join("; "));
            o.setDosOk(dr.ok());
            CHECK(o.readOnlyWhy(o.dosMount()) == "the DOS volume has errors (Tools > Check disc)" && o.readOnlyWhy(o.adfsMount()).isEmpty(),
                  "the window's read-only reasons: " + o.readOnlyWhy(o.dosMount()));
            CHECK(o.disc().transact([&](QString *e) { return o.disc().volume(0)->mkdir(Path(), "WINRULES", e); }, &err) &&
                      !o.disc().mounts()[1].touched(), "an ADFS change, outside DRIVE_C: " + err);
            CHECK(o.save(&err), "it saves: the untouched DOS partition doesn't veto it (" + err + ")");
            OpenedDisc o2;
            dw::fs::Entry e;
            CHECK(openWindow(o2) && o2.disc().volume(0)->stat(Path({"WINRULES"}), &e, &err), "read back");
        }
        {
            OpenedDisc o;                   // the command line's rules: every mount checked
            CHECK(openIt(o, p, &err) && o.disc().transact([&](QString *e) { return o.disc().volume(0)->mkdir(Path(), "CLIRULES", e); }, &err),
                  "the same change, command line: " + err);
            CHECK(!o.save(&err) && err.startsWith("The disc failed its check, so NOTHING was written."), "refused: " + err.section('\n', 0, 0));
        }
        {
            OpenedDisc o;                   // a change inside DRIVE_C touches it: its errors veto the save
            CHECK(openWindow(o) && o.disc().transact([&](QString *e) {
                      return o.disc().volume(1)->write(Path(), "NEW.TXT", "new", {}, QDateTime(), false, e); }, &err) &&
                      o.disc().mounts()[1].touched(), "a DOS change: " + err);
            CHECK(!o.save(&err) && err.startsWith("A consistency check failed. NOTHING was written.") &&
                      err.contains("DOS partition: C:\\BROKEN.TXT: file has a size but no clusters"), "refused: " + err.simplified());
        }
        {
            OpenedDisc o;                   // $.DOS.DRIVE_C is followed: deleted, the DOS mount goes; reverted, it is back
            CHECK(openWindow(o) && o.disc().transact([&](QString *e) { return o.disc().volume(0)->remove(Path({"DOS", "DRIVE_C"}), e); }, &err),
                  "delete $.DOS.DRIVE_C: " + err);
            CHECK(o.followDriveC() && labels(o.disc()) == QStringList({"ADFS:adfs"}) && !o.dos().attached(), "no DOS mount: " + labels(o.disc()).join(", "));
            o.revert();
            CHECK(o.followDriveC() && labels(o.disc()) == QStringList({"ADFS:adfs", "DOS:fat"}) && o.dos().attached() && !o.followDriveC(),
                  "back after Revert: " + labels(o.disc()).join(", "));
        }
        QFile::remove(p);
    }

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
