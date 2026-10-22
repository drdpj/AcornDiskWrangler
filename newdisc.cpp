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
#include "newdisc.h"

#include <QFile>
#include <QSaveFile>
#include <QDateTime>

#include "adfs.h"
#include "dfs.h"
#include "diskformat.h"
#include "dosfs.h"

using namespace Adfs;

namespace NewDisc {

const QVector<TypeInfo> &types() {
    static const QVector<TypeInfo> t = {
        {AdfsS, "ADFS S - 160K, 40 track, single-sided", "ads", false, 10, "Disc name"},
        {AdfsM, "ADFS M - 320K, 80 track, single-sided", "adm", false, 10, "Disc name"},
        {AdfsL, "ADFS L - 640K, 80 track, double-sided", "adl", true, 10, "Disc name"},
        {DfsSs40, "DFS - 100K, 40 track, single-sided (.ssd)", "ssd", false, 12, "Disc title"},
        {DfsSs80, "DFS - 200K, 80 track, single-sided (.ssd)", "ssd", false, 12, "Disc title"},
        {DfsDs40, "DFS - 2 x 100K, 40 track, double-sided (.dsd)", "dsd", true, 12, "Disc title"},
        {DfsDs80, "DFS - 2 x 200K, 80 track, double-sided (.dsd)", "dsd", true, 12, "Disc title"},
        {Acorn800, "Acorn DOS Plus 800K", "img", true, 11, "Volume label"},
        {Acorn640, "Acorn DOS Plus 640K (ADFS-compatible, with DOSBOOT)", "adl", true, 11, "Volume label"},
        {Pc360, "PC 360K - 40 track, double-sided", "img", false, 11, "Volume label"},
        {Pc720, "PC 720K - 80 track, double-sided", "img", false, 11, "Volume label"},
        {Pc1200, "PC 1.2M - 80 track, double-sided, high density", "img", false, 11, "Volume label"},
        {Pc1440, "PC 1.44M - 80 track, double-sided, high density", "img", false, 11, "Volume label"},
    };
    return t;
}

const TypeInfo &info(Type t) {
    for (const TypeInfo &i : types())
        if (i.type == t) return i;
    return types().first();
}

static void put16(char *p, quint32 v) { p[0] = char(v & 0xFF); p[1] = char((v >> 8) & 0xFF); }

// A DOS boot sector with a full BPB for a PC floppy; not bootable (the code just asks the BIOS to try the next
// boot device).
static QByteArray pcBootSector(const Dos::Params &p, quint32 spt, quint32 heads) {
    QByteArray b(512, 0);
    char *s = b.data();
    s[0] = char(0xEB); s[1] = 0x3C; s[2] = char(0x90);              // jmp 0x3E
    memcpy(s + 3, "MSDOS5.0", 8);                                    // the OEM name most DOS versions expect
    put16(s + 0x0B, p.bps);
    s[0x0D] = char(p.spc);
    put16(s + 0x0E, p.res);
    s[0x10] = char(p.nfats);
    put16(s + 0x11, p.rootEnt);
    put16(s + 0x13, p.total);
    s[0x15] = char(p.media);
    put16(s + 0x16, p.spf);
    put16(s + 0x18, spt);
    put16(s + 0x1A, heads);
    s[0x24] = 0x00;                                                  // drive 0
    s[0x26] = 0x29;                                                  // extended BPB follows
    quint32 serial = quint32(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
    for (int i = 0; i < 4; ++i) s[0x27 + i] = char((serial >> (8 * i)) & 0xFF);
    memcpy(s + 0x2B, "NO NAME    ", 11);
    memcpy(s + 0x36, "FAT12   ", 8);
    s[0x3E] = char(0xCD); s[0x3F] = 0x18;                            // int 18h: no bootable disc here
    s[0x40] = char(0xEB); s[0x41] = char(0xFE);                      // jmp $
    s[0x1FE] = 0x55; s[0x1FF] = char(0xAA);
    return b;
}

// Empty FAT(s): media byte, then the two reserved FAT12 entries.
static void putFats(QByteArray &img, const Dos::Params &p, quint64 fatOff) {
    for (quint32 i = 0; i < p.nfats; ++i) {
        char *f = img.data() + fatOff + quint64(i) * p.spf * p.bps;
        f[0] = char(p.media); f[1] = char(0xFF); f[2] = char(0xFF);
    }
}

static bool printable(const QString &t, QString *err) {
    for (QChar c : t)
        if (c.unicode() < 0x20 || c.unicode() > 0x7E) {
            *err = QString("character '%1' cannot be used in the title").arg(c);
            return false;
        }
    return true;
}

// Track 0 side 0 of a DOS Plus 640K disc (16 x 256 bytes, the same place in either side order): free space map
// (2720 sectors, nothing free, so ADFS never allocates over the DOS data), root directory "Acorn DOS Boot Disc"
// holding DOSBOOT (WR, load/exec &04000000), and DOSBOOT itself at sectors 7-15 - as on Acorn's own discs.
static bool hybridBootTrack(const QString &src, QByteArray *track, QString *err) {
    QFile f(src);
    if (src.isEmpty() || !f.open(QIODevice::ReadOnly)) {
        *err = "choose a DOS Plus 640K disc image, or a DOSBOOT file, to copy the boot code from";
        return false;
    }
    qint64 size = f.size();
    DiskInfo di = detectDisk(src);
    if (di.kind == DiskInfo::AdfsDosHybrid) {
        *track = f.read(4096);
        Dir d;
        QString e;
        if (track->size() != 4096 || !Dir::parse(RootSector, track->mid(2 * 256, DirBytes), &d, &e) || d.indexOf("dosboot") < 0) {
            *err = src + " has no DOSBOOT file in its ADFS catalogue";
            return false;
        }
        return true;
    }
    if (size < 1 || size > MaxDosBoot || di.kind != DiskInfo::Unknown) {
        *err = QString("%1 is neither a DOS Plus 640K disc image nor a DOSBOOT file (at most %2 bytes)").arg(src).arg(MaxDosBoot);
        return false;
    }
    QByteArray boot = f.readAll();
    QByteArray img = makeBlankImage(2720, QString(), false).left(4096);
    Dir d;
    QString e;
    if (!Dir::parse(RootSector, img.mid(2 * 256, DirBytes), &d, &e)) { *err = "internal error: " + e; return false; }
    QByteArray title = QByteArray("Acorn DOS Boot Disc");               // fills the 19-byte title field exactly
    memcpy(d.tail.data() + 14, title.constData(), 19);
    Entry x;
    x.setName("dosboot");
    x.setFlag(Entry::R, true);
    x.setFlag(Entry::W, true);
    x.load = x.exec = 0x04000000;
    x.length = quint32(boot.size());
    x.sin = FirstDataSector;                                          // sector 7
    x.seq = d.seq;
    d.entries.append(x);
    QByteArray dir = d.serialize();
    memcpy(img.data() + 2 * 256, dir.constData(), size_t(dir.size()));
    memcpy(img.data() + FirstDataSector * 256, boot.constData(), size_t(boot.size()));
    *track = img;
    return true;
}

bool create(const QString &path, Type t, const QString &titleIn, bool sequential, QString *err, const QString &bootSource) {
    const TypeInfo &ti = info(t);
    QString title = titleIn.trimmed();
    if (title.size() > ti.titleMax) { *err = QString("the %1 can be at most %2 characters").arg(QString(ti.titleWhat).toLower()).arg(ti.titleMax); return false; }
    if (!ti.doubleSided) sequential = false;

    QByteArray img;                 // the disc in its own sector numbering
    SideLayout sides;
    DiskInfo::Kind kind = DiskInfo::Unknown;
    Dos::Format fmt = Dos::FmtPc720;
    bool dosLabel = false;
    switch (t) {
    case AdfsS: case AdfsM: case AdfsL:
        if (!title.isEmpty() && !printable(title, err)) return false;
        img = makeBlankImage(t == AdfsS ? 640 : t == AdfsM ? 1280 : 2560, title);
        if (t == AdfsL) { sides.trackBytes = 4096; sides.cylinders = 80; sides.sideFirst = true; }   // ADFS L counts side 0 first
        kind = DiskInfo::Adfs;
        break;
    case DfsSs40: case DfsSs80: case DfsDs40: case DfsDs80: {
        bool ds = t == DfsDs40 || t == DfsDs80;
        quint32 tracks = (t == DfsSs40 || t == DfsDs40) ? 40 : 80, sec = tracks * 10;
        img = QByteArray(int(sec * 256 * (ds ? 2 : 1)), 0);
        for (int side = 0; side < (ds ? 2 : 1); ++side) {
            char *cat = img.data() + (side ? 10 * 256 : 0);      // each side's catalogue is on its own track 0
            cat[256 + 6] = char((sec >> 8) & 3);                 // boot option 0, sector count
            cat[256 + 7] = char(sec & 0xFF);
        }
        if (ds) { sides.trackBytes = 2560; sides.cylinders = tracks; }
        kind = DiskInfo::Dfs;
        break;
    }
    case Acorn800: {
        Dos::Params p = Dos::Volume::standard(Dos::FmtAcorn800);
        img = QByteArray(int(p.total * p.bps), 0);
        putFats(img, p, 0);
        sides.trackBytes = 5 * 1024; sides.cylinders = 80;
        kind = DiskInfo::DosAcorn800; fmt = Dos::FmtAcorn800; dosLabel = true;
        break;
    }
    case Acorn640: {
        // boot track (ADFS catalogue + DOSBOOT) + an empty DOS Plus FAT at sector 16 (side 0, cylinder 1: DOS Plus
        // and ADFS both number this disc side 0 first)
        QByteArray track;
        if (!hybridBootTrack(bootSource, &track, err)) return false;
        img = QByteArray(655360, 0);
        memcpy(img.data(), track.constData(), 4096);
        Dos::Params p = Dos::Volume::standard(Dos::FmtAcorn640);
        putFats(img, p, quint64(p.res) * p.bps);
        sides.trackBytes = 4096; sides.cylinders = 80; sides.sideFirst = true;
        kind = DiskInfo::AdfsDosHybrid; fmt = Dos::FmtAcorn640; dosLabel = true;
        break;
    }
    case Pc360: case Pc720: case Pc1200: case Pc1440: {
        fmt = t == Pc360 ? Dos::FmtPc360 : t == Pc720 ? Dos::FmtPc720 : t == Pc1200 ? Dos::FmtPc1200 : Dos::FmtPc1440;
        Dos::Params p = Dos::Volume::standard(fmt);
        quint32 spt = t == Pc1200 ? 15 : t == Pc1440 ? 18 : 9;
        img = QByteArray(int(p.total * p.bps), 0);
        QByteArray boot = pcBootSector(p, spt, 2);
        memcpy(img.data(), boot.constData(), 512);
        putFats(img, p, quint64(p.res) * p.bps);
        kind = DiskInfo::DosPc; dosLabel = true;          // always interleaved: no side layout
        break;
    }
    }
    sides.sequential = sequential;
    QByteArray file = layoutFile(img, sides);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(file) != file.size() || !out.commit()) {
        *err = "could not write " + path + ": " + out.errorString();
        return false;
    }
    auto fail = [&](const QString &why) { QFile::remove(path); *err = why; return false; };

    // titles that live in a catalogue or directory are set with the normal (staged, validated) code
    if (!title.isEmpty() && (kind == DiskInfo::Dfs || dosLabel)) {
        Image im;
        QString e;
        bool ok = kind == DiskInfo::AdfsDosHybrid ? im.open(path, &e, 655360, sides) : im.openRaw(path, quint64(file.size()), &e, sides);
        if (!ok) return fail(e);
        if (kind == DiskInfo::Dfs) {
            Dfs::Layout lay;
            lay.doubleSided = sides.doubleSided();
            lay.interleaved = true;
            lay.sectorsPerSide = (t == DfsSs40 || t == DfsDs40) ? 400 : 800;
            for (int side = 0; side < (lay.doubleSided ? 2 : 1); ++side) {
                Dfs::Disc d;
                if (!d.attach(&im, side, lay, &e) || !d.setTitle(title, &e)) return fail(e);
            }
        } else {
            Dos::Volume v;
            bool att = kind == DiskInfo::DosPc ? v.attach(&im, 0, quint32(file.size()), &e) : v.attachFormat(&im, 0, quint32(file.size()), fmt, &e);
            if (!att || !v.setLabel(title, &e)) return fail(e);
        }
        if (!im.commit(&e, false, nullptr)) return fail(e);
    }

    // read it back the way Open will: the format and side order must come out as made
    DiskInfo di = detectDisk(path);
    if (di.kind != kind)
        return fail(QString("internal error: the new image was detected as \"%1\"").arg(di.description));
    // (a blank ADFS or FAT disc reads the same in either side order, so detection can only confirm what it can see)
    if (di.sides.doubleSided() != sides.doubleSided() ||
        (sides.doubleSided() && di.sidesConfirmed && di.sides.sequential != sequential))
        return fail("internal error: the new image's side order was not detected correctly");
    return true;
}

} // namespace NewDisc
