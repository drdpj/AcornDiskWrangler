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
// agreetest - step 4 of the move to DiskWranglerCore replaced detectDisk with detection by tiers (probes for ADFS,
// FAT, Torch CP/N and DFS; src/diskformat.cpp). This checks that it gives exactly what the old detection
// (legacydetect.cpp) gave, field by field, for every image named, with the side order detected and forced both ways;
// and that none of them makes the program ask (two equally likely readings), so opening them looks as before.
//   agreetest <image> ...
#include "diskformat.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

DiskInfo legacyDetectDisk(const QString &path, int force);

static QString show(const DiskInfo &d) {
    return QString("kind %1 | %2 | full %3 | dfs %4/%5/%6 | fat %7%8 | sides %9 %10 %11 %12%13%14 | %15")
        .arg(int(d.kind)).arg(d.description).arg(d.fullSize)
        .arg(d.dfs.doubleSided).arg(d.dfs.interleaved).arg(d.dfs.sectorsPerSide)
        .arg(int(d.dosFormat)).arg(d.dosFromTable ? " table" : "")
        .arg(d.sides.trackBytes).arg(d.sides.cylinders).arg(d.sides.sequential).arg(d.sides.sideFirst)
        .arg(d.sidesConfirmed ? " confirmed" : "").arg(d.sidesForced ? " forced" : "")
        .arg(d.notes.join("; "));
}

static QString spit(const QString &p, const QByteArray &d) {
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly) || f.write(d) != d.size()) printf("could not write %s\n", qPrintable(p));
    return p;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int bad = 0, n = 0;
    // and two CP/M discs, which are CP/M Disk Wrangler's: an Acorn Z80 CP/M disc (its DFS catalogue fences off the
    // CP/M data) and a Torch CP/N disc (the allocation map and formatter bytes on cylinder 0 side 1)
    QTemporaryDir tmp;
    QStringList paths, foreign;
    QByteArray z80(800 * 256, char(0xE5));
    z80.replace(0, 512, dw::dfs::cpmFenceCatalogue(400));
    foreign << spit(tmp.filePath("acorncpm.dsd"), z80);
    QByteArray torch(409600, char(0xE5));
    for (int i = 0; i < 256; ++i) {
        const quint16 w = i < 2 ? 0xFFFF : 0xFC00;
        torch[2560 + 6 * 256 + 2 * i] = char(w & 0xFF);
        torch[2560 + 6 * 256 + 2 * i + 1] = char(w >> 8);
    }
    for (int i = 0; i < 512; ++i) torch[2560 + 8 * 256 + i] = char(i < 256 ? quint8(0xD6 + i) : 0);
    torch[2560 + 8 * 256 + 510] = char(0x85);
    foreign << spit(tmp.filePath("torch.img"), torch);
    for (int i = 1; i < argc; ++i) paths << QString::fromLocal8Bit(argv[i]);
    for (const QString &p : foreign) {
        const dw::fs::Verdict v = detectCandidates(p);
        ++n;
        if (v.action != dw::fs::Verdict::Handoff || v.bestCandidate()->owner.id != "cpmdiskwrangler") {
            printf("NOT HANDED OFF %s\n", qPrintable(p));
            ++bad;
        }
    }
    paths << foreign;
    for (const QString &p : paths)
        for (int force : {-1, 0, 1}) {
            const QString a = show(legacyDetectDisk(p, force)), b = show(detectDisk(p, force));
            const dw::fs::Verdict v = detectCandidates(p, force);
            ++n;
            if (a != b) {
                printf("DIFFER %s (force %d)\n  old: %s\n  new: %s\n", qPrintable(p), force, qPrintable(a), qPrintable(b));
                ++bad;
            } else if (v.action == dw::fs::Verdict::Ask) {
                printf("ASKS %s (force %d): %s / %s\n", qPrintable(p), force, qPrintable(v.bestCandidate()->description),
                       qPrintable(v.rivalCandidate()->description));
                ++bad;
            } else if (force < 0) {
                printf("same %s: %s%s\n", qPrintable(p), qPrintable(a),
                       v.action == dw::fs::Verdict::Handoff ? qPrintable(" -> " + v.bestCandidate()->owner.name) : "");
            }
        }
    printf(bad ? "\n%d of %d DIFFER\n" : "\nALL OK (%d checks)\n", bad ? bad : n, n);
    return bad ? 1 : 0;
}
