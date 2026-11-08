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

#include <diskwrangler/device/container.h>
#include <diskwrangler/fs/detection.h>

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
        AdfsNewMap,     // ADFS D/E (new map): recognised but not supported
        AcornCpm,       // an Acorn Z80 CP/M disc: its DFS catalogue only fences CP/M's data off ($.CPMDISC); refused
        TorchCpn        // a Torch CP/N disc (CP/M files in Torch's own filesystem); refused, CP/M Disk Wrangler opens it
    };
    Kind kind = Unknown;
    QString description;
    quint64 fullSize = 0;           // size the disc should have; a shorter file is padded when saved
    dw::dfs::Layout dfs;
    dw::fat::Format dosFormat = dw::fat::FmtPc720;
    bool dosFromTable = false;      // PC image without a usable BPB: use the table geometry
    // Acorn double-sided floppies (ADFS L, DFS .dsd, DOS Plus 800K / 640K): geometry and the side order of the FILE
    // (open the Image with this). PC FAT floppies are always interleaved and get no layout. For DFS the Image does
    // the side translation, so dfs.interleaved is always true once a layout is set.
    Adfs::SideLayout sides;
    bool sidesConfirmed = true;     // false: both orders looked valid (e.g. a blank disc), so the usual order (interleaved) was used
    bool sidesForced = false;       // the order was given by the caller, not detected
    QStringList notes;
};

// What a disc is, found by DiskWranglerCore's detection by tiers (core/diskwrangler/fs/detection.h): a probe for each
// filesystem - ADFS (here), FAT and DFS (the core's modules) - and one for the CP/M discs that are CP/M Disk
// Wrangler's (Torch CP/N here; Acorn Z80 CP/M in the DFS module). Every candidate's data is its DiskInfo.
// forceSequential: -1 = detect the side order, 0 = read as interleaved, 1 = read as sequential.
// unknown (if given): what the disc is when no probe recognises it (the Unknown DiskInfo detectDisk gives).
dw::fs::Verdict detectCandidates(const QString &path, int forceSequential = -1, DiskInfo *unknown = nullptr);
DiskInfo diskInfo(const dw::fs::Candidate &c);
// The best candidate's DiskInfo (Unknown if none): what the program opens when it doesn't have to ask.
DiskInfo detectDisk(const QString &path, int forceSequential = -1);
// Human-readable side order, e.g. "interleaved" or "sequential (all of side 0, then side 1)".
QString sideOrderName(bool sequential);

// The physical shape of a floppy of this kind, for writing it into an ImageDisk, DSK or extended DSK file (and
// reading such a file back as a raw one): sector size, sectors per track, cylinders, heads, the first sector ID (ADFS and
// DFS 0, PC FAT 1), the ImageDisk mode (DFS FM = 2, the rest MFM = 5) and sideBySide from the file's side order.
// false (and why): hard discs and kinds this table doesn't cover; no geometry is invented. tests/containertest.cpp
// keeps its own copy of the table; tests/convtest.cpp requires the two to agree.
bool rawLayoutFor(const DiskInfo &info, dw::device::RawLayout *out, QString *why);
