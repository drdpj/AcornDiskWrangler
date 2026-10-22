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
    QStringList notes;
};

DiskInfo detectDisk(const QString &path);
