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

DiskInfo detectDisk(const QString &path) {
    DiskInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return info;
    quint64 size = quint64(f.size());
    QString suffix = QFileInfo(path).suffix().toLower();
    if (size < 512) return info;
    info.fullSize = size;

    // ---- ADFS old map (S/M/L floppies, hard discs, and the Acorn 640K hybrid)
    QByteArray s01 = rdAt(f, 0, 512);
    FsMap m;
    QStringList e, w;
    if (FsMap::parse(s01, &m, &e, &w) && m.total >= 8) {
        Dir d;
        QString perr;
        if (Dir::parse(RootSector, rdAt(f, RootSector * SectorSize, DirBytes), &d, &perr)) {
            quint64 want = quint64(m.total) * SectorSize;
            // pad a short file only up to a standard floppy size; never trust an odd map size to grow a real image
            info.fullSize = (want == 163840 || want == 327680 || want == 655360) ? qMax(size, want) : size;
            info.kind = DiskInfo::Adfs;
            if (m.total == 640) info.description = "ADFS S (160K floppy)";
            else if (m.total == 1280) info.description = "ADFS M (320K floppy)";
            else if (m.total == 2560) info.description = "ADFS L (640K floppy)";
            else info.description = QString("ADFS hard disc image (%1)").arg(kb(want));
            // hybrid?  ADFS catalogue + DOS Plus FAT sharing one 640K disc. (A real one reports 2720 sectors in its ADFS
            // map, so don't test that.) The FAT must start with its media signature: an all-zero area would otherwise
            // "validate" as an empty DOS volume and turn every blank ADFS L floppy into a hybrid.
            Dos::Params hp = Dos::Volume::standard(Dos::FmtAcorn640);
            QByteArray sig = size == 655360 ? rdAt(f, 2 * 4096, 3)  /* logical sector 16 = logical track 1 = file track 2 */ : QByteArray();
            if (sig.size() == 3 && quint8(sig[0]) == hp.media && quint8(sig[1]) == 0xFF && quint8(sig[2]) == 0xFF) {
                Image probe;
                Dos::Volume v;
                QString err;
                if (probe.openRaw(path, 655360, &err) &&
                    v.attachFormat(&probe, 0, 655360, Dos::FmtAcorn640, &err) && v.check().ok()) {
                    info.kind = DiskInfo::AdfsDosHybrid;
                    info.description = "Acorn 640K hybrid (ADFS catalogue + DOS Plus FAT)";
                    info.dosFormat = Dos::FmtAcorn640;
                    info.fullSize = 655360;
                }
            }
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
        if (size < v) info.notes << QString("the image file is %1 bytes shorter than the volume; it will be padded with zeros when saved").arg(v - size);
        return info;
    }
    // ---- PC FAT without a BPB (old DOS 1.x/2.0 images): match by size and media byte
    struct Std { quint64 size; Dos::Format fmt; const char *name; };
    static const Std stds[] = {{368640, Dos::FmtPc360, "PC 360K FAT floppy"}, {737280, Dos::FmtPc720, "PC 720K FAT floppy"},
                               {1228800, Dos::FmtPc1200, "PC 1.2M FAT floppy"}, {1474560, Dos::FmtPc1440, "PC 1.44M FAT floppy"}};
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

    // ---- Acorn DOS Plus 800K: FAT starts at byte 0, media byte FD
    if (size == 819200) {
        QByteArray fat = rdAt(f, 0, 3);
        if (quint8(fat[0]) == 0xFD && quint8(fat[1]) == 0xFF && quint8(fat[2]) == 0xFF) {
            info.kind = DiskInfo::DosAcorn800;
            info.dosFormat = Dos::FmtAcorn800;
            info.dosFromTable = true;
            info.description = "Acorn DOS Plus 800K floppy";
            return info;
        }
    }

    // ---- DFS (SSD / DSD)
    quint32 sec = 0;
    if (Dfs::plausibleCatalogue(rdAt(f, 0, 256), rdAt(f, 256, 256), &sec)) {
        int hint = suffix == "dsd" ? 2 : (suffix == "ssd" ? 1 : 0);
        bool il = Dfs::plausibleCatalogue(rdAt(f, 10 * 256, 256), rdAt(f, 11 * 256, 256), nullptr);
        bool seq = size >= quint64(sec) * 256 + 512 &&
                   Dfs::plausibleCatalogue(rdAt(f, quint64(sec) * 256, 256), rdAt(f, quint64(sec) * 256 + 256, 256), nullptr);
        bool dsd = hint == 2 || (hint == 0 && (il || seq) && size > quint64(sec) * 256);
        info.kind = DiskInfo::Dfs;
        info.dfs.sectorsPerSide = sec;
        info.dfs.doubleSided = dsd;
        info.dfs.interleaved = !(dsd && !il && seq);
        info.fullSize = qMax(size, quint64(sec) * 256 * (dsd ? 2 : 1));
        info.description = QString("DFS %1 %2-track (%3 per side)")
                               .arg(dsd ? "double-sided" : "single-sided").arg(sec / 10).arg(kb(quint64(sec) * 256));
        if (dsd && !info.dfs.interleaved) info.description += ", sequential sides";
        if (size < info.fullSize)
            info.notes << QString("the image file is %1 bytes shorter than a full disc (blank tracks were trimmed); "
                                  "it will be padded with zeros when saved").arg(info.fullSize - size);
        return info;
    }
    return info;     // Unknown
}
