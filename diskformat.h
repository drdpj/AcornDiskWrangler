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
// diskformat.h - work out what kind of disc image a file is.
#pragma once

#include <QString>
#include <QStringList>

#include "dfs.h"
#include "dosfs.h"

struct DiskInfo {
    enum Kind {
        Unknown,        // not recognised: opened as ADFS so the problems can be reported
        Adfs,           // old-map ADFS: S/M/L floppies and hard discs (may contain $.DOS.DRIVE_C)
        AdfsDosHybrid,  // Acorn 640K: ADFS catalogue + DOS Plus FAT on the same disc
        Dfs,            // Acorn DFS, single or double sided
        DosPc,          // PC FAT floppy (360K, 720K, 1.2M, 1.44M, or any BPB-described FAT12/16 volume)
        DosAcorn800,    // Acorn DOS Plus 800K (no boot record)
        AdfsNewMap      // ADFS D/E (new map): recognised but not supported
    };
    Kind kind = Unknown;
    QString description;
    quint64 fullSize = 0;           // size the disc should have; a shorter file is padded when saved
    Dfs::Layout dfs;
    Dos::Format dosFormat = Dos::FmtPc720;
    bool dosFromTable = false;      // PC image without a usable BPB: use the table geometry
    // Acorn double-sided floppies (ADFS L, DFS .dsd, DOS Plus 800K / 640K): geometry and the side order of the FILE
    // (open the Image with this). PC FAT floppies are always interleaved and get no layout. For DFS the Image does
    // the side translation, so dfs.interleaved is always true once a layout is set.
    Adfs::SideLayout sides;
    bool sidesConfirmed = true;     // false: both orders looked valid (e.g. a blank disc), so the usual order (interleaved) was used
    bool sidesForced = false;       // the order was given by the caller, not detected
    QStringList notes;
};

// forceSequential: -1 = detect the side order, 0 = read as interleaved, 1 = read as sequential.
DiskInfo detectDisk(const QString &path, int forceSequential = -1);
// Human-readable side order, e.g. "interleaved" or "sequential (all of side 0, then side 1)".
QString sideOrderName(bool sequential);
