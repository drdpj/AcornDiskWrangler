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
#include "diskformat.h"

#include <QFile>
#include <QFileInfo>
#include <functional>

using namespace Adfs;

static QByteArray rdAt(QFile &f, quint64 off, int n) {
    if (!f.seek(qint64(off))) return QByteArray();
    QByteArray b = f.read(n);
    if (b.size() < n) b.append(QByteArray(n - b.size(), 0));
    return b;
}

static QString kb(quint64 bytes) {
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) return QString("%1M").arg(bytes / (1024 * 1024));
    if (bytes >= 8 * 1024 * 1024) return QString("%1M").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1K").arg(bytes / 1024);
}

QString sideOrderName(bool sequential) {
    return sequential ? "sequential (all of side 0, then side 1)" : "interleaved (sides alternate track by track)";
}

static SideLayout geometry(quint32 trackBytes, quint32 cylinders, bool sequential, bool sideFirst) {
    SideLayout s;
    s.trackBytes = trackBytes;
    s.cylinders = cylinders;
    s.sequential = sequential;
    s.sideFirst = sideFirst;
    return s;
}

// ---------------------------------------------------------------- side order detection
// Each check below reads the disc through one side order and asks whether structures that live OUTSIDE track 0
// line up: ADFS subdirectories, DOS subdirectory "." / ".." entries and FAT chains. Track 0 side 0 (boot sector,
// ADFS map, DFS catalogue of side 0) is in the same place in both orders, so it proves nothing.

static bool adfsConsistent(const QString &path, quint64 full, const SideLayout &sl) {
    Image img;
    QString e;
    return img.open(path, &e, full, sl) && img.openReport().ok();
}

// How well a FAT volume reads in one side order: -1 = inconsistent, otherwise the number of lost clusters (in use
// in the FAT but in no file). The FAT itself is on track 0 in every format, so in the wrong order it still says
// what is in use while the directories that should account for it read as garbage or as empty (a 1.44M disc's
// root directory is entirely on side 1 of cylinder 0). fmt < 0: geometry from the boot sector BPB.
static int dosScore(const QString &path, quint64 full, const SideLayout &sl, int fmt) {
    Image img;
    QString e;
    if (!img.openRaw(path, full, &e, sl)) return -1;
    Dos::Volume v;
    bool ok = fmt < 0 ? v.attach(&img, 0, quint32(full), &e) : v.attachFormat(&img, 0, quint32(full), Dos::Format(fmt), &e);
    if (!ok || !v.check().ok()) return -1;
    std::function<bool(quint32, int)> walk = [&](quint32 dir, int depth) -> bool {
        if (depth > 16) return true;
        QVector<Dos::DEntry> L;
        if (!v.listDir(dir, &L, &e)) return false;
        for (const Dos::DEntry &x : L) {
            if (!x.isDir() || x.name == "." || x.name == "..") continue;
            QVector<Dos::DEntry> C;
            if (!v.listDir(x.cluster, &C, &e)) return false;
            if (C.size() < 2 || C[0].name != "." || !C[0].isDir() || C[0].cluster != x.cluster ||
                C[1].name != ".." || !C[1].isDir() || C[1].cluster != dir)
                return false;
            if (!walk(x.cluster, depth + 1)) return false;
        }
        return true;
    };
    return walk(0, 0) ? int(qMin<quint32>(v.lostClusters(), 0x7FFFFFFF)) : -1;
}

// both orders read without errors: the one that accounts for more of the FAT wins; a tie can't be decided, and then
// the format's usual order (interleaved) is used
static void decideDos(DiskInfo &info, int il, int seq) {
    if (il >= 0 && seq >= 0 && il != seq) {
        info.sides.sequential = seq < il;
        info.sidesConfirmed = true;
        return;
    }
    info.sides.sequential = seq >= 0 && il < 0;
    info.sidesConfirmed = (il >= 0) != (seq >= 0);
}

// Settles info.sides.sequential from which orders gave a consistent disc. When both do (typically a blank disc, or
// one with no directories beyond track 0) the contents can't tell: it says so and uses the format's usual order,
// which for every Acorn double-sided format (ADFS L, DSD, DOS Plus 800K and 640K) is interleaved.
static void decide(DiskInfo &info, bool okIl, bool okSeq) {
    info.sides.sequential = okSeq && !okIl;
    info.sidesConfirmed = okIl != okSeq;
}

static void describeSides(DiskInfo &info) {
    if (info.sides.doubleSided() && info.sides.sequential) info.description += ", sequential sides";
}

DiskInfo detectDisk(const QString &path, int force) {
    DiskInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return info;
    quint64 size = quint64(f.size());
    QString suffix = QFileInfo(path).suffix().toLower();
    if (size < 512) return info;
    info.fullSize = size;
    info.sidesForced = force >= 0;

    // ---- ADFS old map (S/M/L floppies, hard discs, and the Acorn 640K hybrid)
    QByteArray s01 = rdAt(f, 0, 512);
    FsMap m;
    QStringList e, w;
    bool mapOk = FsMap::parse(s01, &m, &e, &w) && m.total >= 8;
    Dir d;
    QString perr;
    bool rootOk = Dir::parse(RootSector, rdAt(f, RootSector * SectorSize, DirBytes), &d, &perr);
    if (!mapOk && rootOk) {
        // A damaged free space map (it opens read-only) on a disc whose root directory is fine: still ADFS, so that
        // it is read with the right geometry. Take the size the map claims, or failing that the file's.
        m.total = quint8(s01[0xFC]) | (quint8(s01[0xFD]) << 8) | (quint8(s01[0xFE]) << 16);
        if (m.total < 8 || quint64(m.total) * SectorSize > size + 655360) m.total = quint32(size / SectorSize);
    }
    if ((mapOk || rootOk) && m.total >= 8) {
        if (rootOk) {
            quint64 want = quint64(m.total) * SectorSize;
            // pad a short file only up to a standard floppy size; never trust an odd map size to grow a real image
            info.fullSize = (want == 163840 || want == 327680 || want == 655360) ? qMax(size, want) : size;
            info.kind = DiskInfo::Adfs;
            if (m.total == 640) info.description = "ADFS S (160K floppy)";
            else if (m.total == 1280) info.description = "ADFS M (320K floppy)";
            else if (m.total == 2560) info.description = "ADFS L (640K floppy)";
            else info.description = QString("ADFS hard disc image (%1)").arg(kb(want));

            // A 640K disc (ADFS L, or the hybrid whose map claims 2720 sectors) is double-sided: 80 cylinders of
            // 16 x 256 bytes. ADFS (and DOS Plus on the hybrid) number its sectors side 0 first - all of side 0,
            // then side 1 - while the file is normally interleaved, so sector numbers are always mapped. (Checked
            // against real .adl discs: Master Welcome, Acorn User Tau85, ARM Evaluation disc 4.)
            bool l640 = (m.total == 2560 && size <= 655360) || size == 655360;
            if (l640) info.sides = geometry(4096, 80, false, true);

            // hybrid?  ADFS catalogue + DOS Plus FAT sharing one 640K disc. (A real one reports 2720 sectors in its ADFS
            // map, so don't test that.) The FAT must start with its media signature: an all-zero area would otherwise
            // "validate" as an empty DOS volume and turn every blank ADFS L floppy into a hybrid. DOS Plus puts its FAT
            // at logical sector 16 = side 0, cylinder 1: file track 2 if interleaved, file track 1 if sequential.
            Dos::Params hp = Dos::Volume::standard(Dos::FmtAcorn640);
            auto fatSig = [&](quint64 off) {
                QByteArray s = rdAt(f, off, 3);
                return quint8(s[0]) == hp.media && quint8(s[1]) == 0xFF && quint8(s[2]) == 0xFF;
            };
            bool hyb[2] = {false, false};
            if (size == 655360)
                for (int sq = 0; sq < 2; ++sq)
                    if ((force < 0 || force == sq) && fatSig(sq ? 4096 : 8192))
                        hyb[sq] = dosScore(path, 655360, geometry(4096, 80, sq, true), Dos::FmtAcorn640) >= 0;
            if (hyb[0] || hyb[1]) {
                info.kind = DiskInfo::AdfsDosHybrid;
                info.description = "Acorn 640K hybrid (ADFS catalogue + DOS Plus FAT)";
                info.dosFormat = Dos::FmtAcorn640;
                info.fullSize = 655360;
                if (force >= 0) info.sides.sequential = force == 1;
                else decide(info, hyb[0], hyb[1]);
            } else if (l640) {
                if (force >= 0) {
                    info.sides.sequential = force == 1;
                } else {
                    decide(info, adfsConsistent(path, 0, geometry(4096, 80, false, true)),
                           adfsConsistent(path, 0, geometry(4096, 80, true, true)));
                }
            }
            describeSides(info);
            if (size < info.fullSize) info.notes << QString("the image file is %1 bytes shorter than the disc; it will be padded with zeros when saved").arg(info.fullSize - size);
            return info;
        }
    }

    // ---- ADFS D/E (new map): recognised, not supported
    if (size == 819200 || size == 1638400) {
        for (quint64 off : {0x201ull, 0x401ull, 0x801ull}) {
            QByteArray h = rdAt(f, off, 4);
            if (h == "Hugo" || h == "Nick") {
                info.kind = DiskInfo::AdfsNewMap;
                info.description = "ADFS D/E (new map) floppy";
                return info;
            }
        }
    }

    // Side order of an Acorn FAT floppy: try both unless forced
    auto dosSides = [&](quint64 full, int fmt) {
        if (!info.sides.doubleSided()) return;
        if (force >= 0) { info.sides.sequential = force == 1; return; }
        SideLayout il = info.sides, sq = info.sides;
        il.sequential = false;
        sq.sequential = true;
        decideDos(info, dosScore(path, full, il, fmt), dosScore(path, full, sq, fmt));
    };
    struct Std { quint64 size; Dos::Format fmt; const char *name; };
    static const Std stds[] = {{368640, Dos::FmtPc360, "PC 360K FAT floppy"}, {737280, Dos::FmtPc720, "PC 720K FAT floppy"},
                               {1228800, Dos::FmtPc1200, "PC 1.2M FAT floppy"}, {1474560, Dos::FmtPc1440, "PC 1.44M FAT floppy"}};

    // ---- PC FAT: boot sector BPB
    Dos::Params p;
    if (Dos::Volume::readBpb(s01, &p) && Dos::Volume::plausible(p) && quint64(p.total) * p.bps <= 64ull * 1024 * 1024 &&
        quint8(s01[0]) != 0) {
        info.kind = DiskInfo::DosPc;
        info.fullSize = qMax(size, quint64(p.total) * p.bps);
        quint64 v = quint64(p.total) * p.bps;
        info.description = (v == 368640 || v == 737280 || v == 1228800 || v == 1474560)
                               ? QString("PC %1 FAT floppy").arg(v == 368640 ? "360K" : v == 737280 ? "720K" : v == 1228800 ? "1.2M" : "1.44M")
                               : QString("FAT12/16 volume (%1)").arg(kb(v));
        // PC floppies are always stored interleaved, so they have no side order to detect or change
        if (size < v) info.notes << QString("the image file is %1 bytes shorter than the volume; it will be padded with zeros when saved").arg(v - size);
        return info;
    }
    // ---- PC FAT without a BPB (old DOS 1.x/2.0 images): match by size and media byte
    for (const Std &s : stds) {
        if (size < s.size) continue;
        QByteArray fat = rdAt(f, 512, 3);
        if (size == s.size && quint8(fat[0]) == Dos::Volume::standard(s.fmt).media && quint8(fat[1]) == 0xFF && quint8(fat[2]) == 0xFF) {
            info.kind = DiskInfo::DosPc;
            info.dosFormat = s.fmt;
            info.dosFromTable = true;
            info.description = QString(s.name) + " (no BPB)";
            return info;
        }
    }

    // ---- Acorn DOS Plus 800K: FAT starts at byte 0, media byte FD. 80 cylinders x 2 sides x 5 x 1024 bytes;
    // DOS Plus numbers these sectors in interleaved order, so an interleaved file needs no mapping.
    if (size == 819200) {
        QByteArray fat = rdAt(f, 0, 3);
        if (quint8(fat[0]) == 0xFD && quint8(fat[1]) == 0xFF && quint8(fat[2]) == 0xFF) {
            info.kind = DiskInfo::DosAcorn800;
            info.dosFormat = Dos::FmtAcorn800;
            info.dosFromTable = true;
            info.description = "Acorn DOS Plus 800K floppy";
            info.sides = geometry(5120, 80, false, false);      // numbered sector, side, track
            dosSides(819200, int(Dos::FmtAcorn800));
            describeSides(info);
            return info;
        }
    }

    // ---- DFS (SSD / DSD). Each side has its own catalogue on its own track 0, so the second catalogue's position
    // (file sector 10 if interleaved, file sector <sectors per side> if sequential) settles the side order.
    quint32 sec = 0;
    if (Dfs::plausibleCatalogue(rdAt(f, 0, 256), rdAt(f, 256, 256), &sec)) {
        // Acorn's CP/M writes a catalogue holding one locked file, $.CPMDISC, from sector 2 to the end of side 0, so
        // that DFS leaves the disc alone (seen on a real Acorn Z80 disc). The disc is CP/M: editing it as DFS (or
        // writing on side 1, which has no catalogue) would destroy it, so it is not opened here.
        QByteArray s0 = rdAt(f, 0, 256), s1 = rdAt(f, 256, 256);
        if (quint8(s1[5]) == 8 && s0.mid(8, 7) == "CPMDISC" && (quint8(s0[15]) & 0x7F) == '$') {
            info.kind = DiskInfo::AcornCpm;
            info.description = "Acorn Z80 CP/M disc";
            return info;
        }
        int hint = suffix == "dsd" ? 2 : (suffix == "ssd" ? 1 : 0);
        bool il = Dfs::plausibleCatalogue(rdAt(f, 10 * 256, 256), rdAt(f, 11 * 256, 256), nullptr);
        bool seq = size >= quint64(sec) * 256 + 512 &&
                   Dfs::plausibleCatalogue(rdAt(f, quint64(sec) * 256, 256), rdAt(f, quint64(sec) * 256 + 256, 256), nullptr);
        bool dsd = hint == 2 || (hint == 0 && (il || seq) && size > quint64(sec) * 256);
        info.kind = DiskInfo::Dfs;
        info.dfs.sectorsPerSide = sec;
        info.dfs.doubleSided = dsd;
        info.dfs.interleaved = true;           // the Image does the side translation
        if (dsd) {
            info.sides = geometry(10 * 256, (sec + 9) / 10, false, false);
            info.sides.sequential = force >= 0 ? force == 1 : (!il && seq);
        }
        info.fullSize = qMax(size, quint64(sec) * 256 * (dsd ? 2 : 1));
        info.description = QString("DFS %1 %2-track (%3 per side)")
                               .arg(dsd ? "double-sided" : "single-sided").arg(sec / 10).arg(kb(quint64(sec) * 256));
        describeSides(info);
        if (size < info.fullSize)
            info.notes << QString("the image file is %1 bytes shorter than a full disc (blank tracks were trimmed); "
                                  "it will be padded with zeros when saved").arg(info.fullSize - size);
        return info;
    }
    return info;     // Unknown
}
