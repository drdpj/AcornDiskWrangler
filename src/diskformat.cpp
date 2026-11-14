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

#include <QFileInfo>

#include <diskwrangler/fs/dfs/dfsprobe.h>
#include <diskwrangler/fs/fat/fatprobe.h>

using namespace Adfs;
using dw::fs::Candidate;
using dw::fs::ProbeContext;
using dw::fs::Tier;

static dw::device::SideOrder geometry(quint32 trackBytes, quint32 cylinders, bool sequential, bool sideFirst) {
    dw::device::SideOrder s;
    s.trackBytes = trackBytes;
    s.cylinders = cylinders;
    s.sequential = sequential;
    s.sideFirst = sideFirst;
    return s;
}

// What every candidate starts from: the disc is the file's size, the side order forced or not.
static DiskInfo baseInfo(const ProbeContext &ctx) {
    DiskInfo info;
    info.fullSize = quint64(ctx.size());
    info.sidesForced = ctx.forceSequential >= 0;
    return info;
}

static Candidate candidate(const QString &fsId, const QString &name, Tier tier, const DiskInfo &info) {
    Candidate c;
    c.fsId = fsId;
    c.name = name;
    c.tier = tier;
    c.description = info.description;
    c.notes = info.notes;
    c.data = info;
    return c;
}

// ---------------------------------------------------------------- side order detection
// Each check reads the disc through one side order and asks whether structures that live OUTSIDE track 0 line up:
// ADFS subdirectories, DOS subdirectory "." / ".." entries and FAT chains (dw::fat::sideScore). Track 0 side 0 (boot
// sector, ADFS map, DFS catalogue of side 0) is in the same place in both orders, so it proves nothing.

static bool adfsConsistent(const QString &path, quint64 full, const dw::device::SideOrder &sl) {
    Image img;
    QString e;
    return img.open(path, &e, full, sl) && img.openReport().ok();
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

// ---------------------------------------------------------------- the probes

// Old-map ADFS (S/M/L floppies, hard discs and the Acorn 640K hybrid): Certain when the root directory is where ADFS
// keeps it (even if the free space map is damaged: it then opens read-only). ADFS D/E (new map) is recognised, Likely,
// and not supported.
class AdfsProbe : public dw::fs::Probe {
public:
    QString id() const override { return "adfs"; }
    QVector<Candidate> probe(const ProbeContext &ctx) const override {
        const quint64 size = quint64(ctx.size());
        const int force = ctx.forceSequential;
        DiskInfo info = baseInfo(ctx);
        QByteArray s01 = ctx.stored(0, 512);
        FsMap m;
        QStringList e, w;
        bool mapOk = FsMap::parse(s01, &m, &e, &w) && m.total >= 8;
        Dir d;
        QString perr;
        bool rootOk = Dir::parse(RootSector, ctx.stored(RootSector * SectorSize, DirBytes), &d, &perr);
        if (!mapOk && rootOk) {
            // A damaged free space map (it opens read-only) on a disc whose root directory is fine: still ADFS, so
            // that it is read with the right geometry. Take the size the map claims, or failing that the file's.
            m.total = quint8(s01[0xFC]) | (quint8(s01[0xFD]) << 8) | (quint8(s01[0xFE]) << 16);
            if (m.total < 8 || quint64(m.total) * SectorSize > size + 655360) m.total = quint32(size / SectorSize);
        }
        if (rootOk && m.total >= 8) {
            quint64 want = quint64(m.total) * SectorSize;
            // pad a short file only up to a standard floppy size; never trust an odd map size to grow a real image
            info.fullSize = (want == 163840 || want == 327680 || want == 655360) ? qMax(size, want) : size;
            info.kind = DiskInfo::Adfs;
            if (m.total == 640) info.description = "ADFS S (160K floppy)";
            else if (m.total == 1280) info.description = "ADFS M (320K floppy)";
            else if (m.total == 2560) info.description = "ADFS L (640K floppy)";
            else info.description = QString("ADFS hard disc image (%1)").arg(dw::dfs::sizeText(want));

            // A 640K disc (ADFS L, or the hybrid whose map claims 2720 sectors) is double-sided: 80 cylinders of
            // 16 x 256 bytes. ADFS (and DOS Plus on the hybrid) number its sectors side 0 first - all of side 0,
            // then side 1 - while the file is normally interleaved, so sector numbers are always mapped. (Checked
            // against real .adl discs: Master Welcome, Acorn User Tau85, ARM Evaluation disc 4.)
            bool l640 = (m.total == 2560 && size <= 655360) || size == 655360;
            if (l640) info.sides = geometry(4096, 80, false, true);

            // hybrid?  ADFS catalogue + DOS Plus FAT sharing one 640K disc. (A real one reports 2720 sectors in its
            // ADFS map, so don't test that.) The FAT must start with its media signature: an all-zero area would
            // otherwise "validate" as an empty DOS volume and turn every blank ADFS L floppy into a hybrid. DOS Plus
            // puts its FAT at logical sector 16 = side 0, cylinder 1: file track 2 if interleaved, 1 if sequential.
            dw::fat::Params hp = dw::fat::Volume::standard(dw::fat::FmtAcorn640);
            auto fatSig = [&](quint64 off) {
                QByteArray s = ctx.stored(qint64(off), 3);
                return quint8(s[0]) == hp.media && quint8(s[1]) == 0xFF && quint8(s[2]) == 0xFF;
            };
            bool hyb[2] = {false, false};
            if (size == 655360)
                for (int sq = 0; sq < 2; ++sq)
                    if ((force < 0 || force == sq) && fatSig(sq ? 4096 : 8192))
                        hyb[sq] = dw::fat::sideScore(*ctx.container, 655360, geometry(4096, 80, sq, true), dw::fat::FmtAcorn640) >= 0;
            QString name = "ADFS";
            if (hyb[0] || hyb[1]) {
                info.kind = DiskInfo::AdfsDosHybrid;
                info.description = "Acorn 640K hybrid (ADFS catalogue + DOS Plus FAT)";
                info.dosFormat = dw::fat::FmtAcorn640;
                info.fullSize = 655360;
                name = "ADFS + DOS Plus";
                if (force >= 0) info.sides.sequential = force == 1;
                else decide(info, hyb[0], hyb[1]);
            } else if (l640) {
                if (force >= 0) {
                    info.sides.sequential = force == 1;
                } else {
                    decide(info, adfsConsistent(ctx.path, 0, geometry(4096, 80, false, true)),
                           adfsConsistent(ctx.path, 0, geometry(4096, 80, true, true)));
                }
            }
            describeSides(info);
            if (size < info.fullSize) info.notes << QString("the image file is %1 bytes shorter than the disc; it will be padded with zeros when saved").arg(info.fullSize - size);
            Candidate c = candidate("adfs", name, Tier::Certain, info);
            c.evidence << "the root directory is where ADFS keeps it" << (mapOk ? "a valid free space map" : "a damaged free space map");
            return {c};
        }
        // ADFS D/E (new map): recognised, not supported
        if (size == 819200 || size == 1638400)
            for (qint64 off : {0x201, 0x401, 0x801}) {
                QByteArray h = ctx.stored(off, 4);
                if (h == "Hugo" || h == "Nick") {
                    info.kind = DiskInfo::AdfsNewMap;
                    info.description = "ADFS D/E (new map) floppy";
                    Candidate c = candidate("adfs-newmap", "ADFS D/E", Tier::Likely, info);
                    c.supported = false;
                    return {c};
                }
            }
        return {};
    }
};

// FAT (PC floppies, any FAT12/16 volume, Acorn DOS Plus 800K): DiskWranglerCore's FAT module. A boot sector with a
// usable BPB is Certain (it is too particular to happen by chance); a floppy recognised by its size and the FAT's
// media byte is Likely.
class FatProbe : public dw::fs::Probe {
public:
    QString id() const override { return "fat"; }
    QVector<Candidate> probe(const ProbeContext &ctx) const override {
        QVector<Candidate> out;
        for (Candidate c : m_fat.probe(ctx)) {
            const dw::fat::Found f = std::any_cast<dw::fat::Found>(c.data);
            DiskInfo info = baseInfo(ctx);
            info.kind = f.acorn800 ? DiskInfo::DosAcorn800 : DiskInfo::DosPc;
            info.description = c.description;
            info.dosFormat = f.format;
            info.dosFromTable = f.fromTable;
            info.notes = f.notes;
            if (!f.acorn800 && !f.fromTable) info.fullSize = f.fullSize;
            if (f.acorn800) { info.sides = f.sides; info.sidesConfirmed = f.sidesConfirmed; }
            Candidate a = candidate("fat", c.name, f.fromTable ? Tier::Likely : Tier::Certain, info);
            a.evidence = c.evidence;
            out << a;
        }
        return out;
    }
private:
    dw::fat::Probe m_fat;
};

// Torch CP/N (Torch Z80 second processor): 80 x 2 x 10 x 256, cylinder by cylinder. Cylinder 0 side 1 (file offset
// 2560) holds the allocation map in sectors 6-7 - every word has its top six bits set, tracks 0 and 1 are all in use,
// words 160-255 are FC00 or FFFF - and in sectors 8-9 the bytes Torch's formatter writes (the same on every disc seen
// but for one byte). CP/M Disk Wrangler reads and writes these discs (its torchfs.cpp has the details): Certain, and
// handed to it.
class TorchProbe : public dw::fs::Probe {
public:
    QString id() const override { return "torch"; }
    QVector<Candidate> probe(const ProbeContext &ctx) const override {
        if (ctx.size() != 409600) return {};
        QByteArray map = ctx.stored(2560 + 6 * 256, 512), fmt = ctx.stored(2560 + 8 * 256, 512);
        auto w = [&](int i) { return quint8(map[2 * i]) | quint8(map[2 * i + 1]) << 8; };
        bool mapOk = map.size() == 512 && w(0) == 0xFFFF && w(1) == 0xFFFF;
        for (int i = 0; mapOk && i < 256; ++i) mapOk = (w(i) & 0xFC00) == 0xFC00 && (i < 160 || w(i) == 0xFC00 || w(i) == 0xFFFF);
        bool formatter = fmt.size() == 512;
        for (int i = 0; formatter && i < 512; ++i)
            formatter = i == 256 + 254 || quint8(fmt[i]) == (i < 256 ? quint8(0xD6 + i) : 0);   // byte 510 varies (85, 80, 47 seen)
        if (!mapOk || !formatter) return {};
        DiskInfo info = baseInfo(ctx);
        info.kind = DiskInfo::TorchCpn;
        info.description = "Torch CP/N disc";
        Candidate c = candidate("torch", "Torch CP/N", Tier::Certain, info);
        c.owner = dw::fs::cpmDiskWrangler();
        c.evidence << "Torch's allocation map and formatter bytes on cylinder 0 side 1";
        return {c};
    }
};

// DFS (SSD / DSD, Watford 62-file catalogues) and Acorn Z80 CP/M's catalogue: DiskWranglerCore's DFS module. A
// catalogue that lists files is Likely; an empty one (it is only a disc size and a title) Possible.
class DfsProbe : public dw::fs::Probe {
public:
    QString id() const override { return "dfs"; }
    QVector<Candidate> probe(const ProbeContext &ctx) const override {
        QVector<Candidate> out;
        for (Candidate c : m_dfs.probe(ctx)) {
            DiskInfo info = baseInfo(ctx);
            info.description = c.description;
            if (c.foreign()) {
                info.kind = DiskInfo::AcornCpm;
                Candidate a = candidate(c.fsId, c.name, c.tier, info);
                a.owner = c.owner;
                a.evidence = c.evidence;
                out << a;
                continue;
            }
            const dw::dfs::Found f = std::any_cast<dw::dfs::Found>(c.data);
            info.kind = DiskInfo::Dfs;
            info.dfs = f.layout;
            info.sides = f.sides;
            info.fullSize = f.fullSize;
            info.notes = f.notes;
            const bool files = f.watford || quint8(ctx.stored(256 + 5, 1)[0]) != 0;
            Candidate a = candidate("dfs", c.name, files ? Tier::Likely : Tier::Possible, info);
            a.evidence = c.evidence;
            out << a;
        }
        return out;
    }
private:
    dw::dfs::Probe m_dfs;
};

static QString inContainer(dw::device::Kind k) {
    switch (k) {
    case dw::device::Kind::Imd: return "in an ImageDisk file";
    case dw::device::Kind::Dsk: return "in a DSK file";
    case dw::device::Kind::Edsk: return "in an extended DSK file";
    case dw::device::Kind::Td0: return "in a TeleDisk file (read-only)";
    default: return QString();
    }
}

// The probes in the order the program has always tried them: ADFS, FAT, Torch CP/N, DFS. Among candidates of the
// same tier, that order decides; two of the same tier from different probes (Certain or Likely) are asked about.
static dw::fs::Verdict detectAll(const QString &path, int force, DiskInfo *unknown) {
    *unknown = DiskInfo();
    dw::device::Container c;
    QString e;
    if (!c.open(path, &e)) return dw::fs::Verdict();
    c.setRawPadding(0, 0);
    const bool raw = c.kind() == dw::device::Kind::Raw;
    if (!raw && c.storedSize(&e) == 0) {
        unknown->description = dw::device::kindName(c.kind());
        unknown->notes << e;
        return dw::fs::Verdict();
    }
    ProbeContext ctx;
    ctx.path = path;
    ctx.suffix = QFileInfo(path).suffix().toLower();
    ctx.container = &c;
    // ImageDisk, DSK, TeleDisk: read as the raw image their tracks make. Every track says where it is, so there is
    // no side order to detect (the stored order is interleaved).
    ctx.forceSequential = raw ? force : 0;
    if (ctx.size() < 512) return dw::fs::Verdict();
    *unknown = baseInfo(ctx);
    static const AdfsProbe adfs;
    static const FatProbe fat;
    static const TorchProbe torch;
    static const DfsProbe dfs;
    dw::fs::Verdict v = dw::fs::detect(ctx, {&adfs, &fat, &torch, &dfs});
    if (!raw) {
        unknown->sidesForced = false;
        for (Candidate &x : v.ranked) {
            DiskInfo i = std::any_cast<DiskInfo>(x.data);
            i.sidesForced = false;
            i.sidesConfirmed = true;
            i.description += ", " + inContainer(c.kind());
            x.description = i.description;
            x.data = i;
        }
    }
    return v;
}

dw::fs::Verdict detectCandidates(const QString &path, int force, DiskInfo *unknown) {
    DiskInfo u;
    return detectAll(path, force, unknown ? unknown : &u);
}

DiskInfo diskInfo(const Candidate &c) {
    const DiskInfo *i = std::any_cast<DiskInfo>(&c.data);
    return i ? *i : DiskInfo();
}

DiskInfo detectDisk(const QString &path, int force) {
    DiskInfo unknown;
    const dw::fs::Verdict v = detectAll(path, force, &unknown);
    return v.bestCandidate() ? diskInfo(*v.bestCandidate()) : unknown;
}

bool rawLayoutFor(const DiskInfo &info, dw::device::RawLayout *out, QString *why) {
    dw::device::RawLayout l;
    l.mode = 5;
    const quint64 size = info.fullSize;
    auto no = [&](const QString &w) { if (why) *why = w; return false; };
    switch (info.kind) {
    case DiskInfo::Adfs:
        if (size == 163840) { l.seclen = 256; l.sectrk = 16; l.cyls = 40; l.heads = 1; }
        else if (size == 327680) { l.seclen = 256; l.sectrk = 16; l.cyls = 80; l.heads = 1; }
        else if (size == 655360) { l.seclen = 256; l.sectrk = 16; l.cyls = 80; l.heads = 2; }
        else return no("a hard disc image has no track layout to convert");
        l.firstId = 0;
        break;
    case DiskInfo::AdfsDosHybrid:
        l.seclen = 256; l.sectrk = 16; l.cyls = 80; l.heads = 2; l.firstId = 0;
        break;
    case DiskInfo::Dfs:
        if (info.dfs.sectorsPerSide == 0) return no("this DFS disc's size isn't known");
        l.seclen = 256; l.sectrk = 10; l.cyls = int(info.dfs.sectorsPerSide / 10); l.heads = info.dfs.doubleSided ? 2 : 1;
        l.firstId = 0;
        l.mode = 2;                                             // FM
        break;
    case DiskInfo::DosAcorn800:
        l.seclen = 1024; l.sectrk = 5; l.cyls = 80; l.heads = 2; l.firstId = 0;
        break;
    case DiskInfo::DosPc: {
        const int spt = size == 368640 || size == 737280 ? 9 : size == 1228800 ? 15 : size == 1474560 ? 18 : 0;
        if (!spt) return no("this PC disc isn't a 360K, 720K, 1.2M or 1.44M floppy, so its track layout isn't known");
        l.seclen = 512; l.sectrk = spt; l.cyls = int(size / (512 * spt * 2)); l.heads = 2; l.firstId = 1;
        break;
    }
    default:
        return no("this kind of disc has no track layout to convert");
    }
    l.sideBySide = l.heads == 2 && info.kind != DiskInfo::DosPc && info.sides.sequential;
    if (out) *out = l;
    return true;
}
