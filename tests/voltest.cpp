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
        QVector<Dos::DEntry> L;
        QVector<dw::fs::Entry> l;
        fv->fat()->listDir(0, &L, &err);
        int real = 0;
        for (const Dos::DEntry &x : L) real += x.name != "." && x.name != ".." && (x.attr & 0x08) == 0;
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

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
