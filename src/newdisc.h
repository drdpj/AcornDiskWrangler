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
// newdisc.h - create blank disc images of every supported format.
//
// The formats offered are presets in the core's registry (dw::fs::presets(), <diskwrangler/fs/presets.h>): the DFS
// module adds DFS and Watford DFS, the FAT module Acorn DOS Plus 800K and the PC floppies, and addPresets() below this
// program's own (ADFS S, M, L and the DOS Plus 640K hybrid). types() is this program's list, in its own order, read
// from the registry; making the blank disc stays here (create()).
#pragma once

#include <QString>
#include <QVector>

namespace dw { namespace fs { class PresetRegistry; } }

namespace NewDisc {

enum Type { AdfsS, AdfsM, AdfsL, DfsSs40, DfsSs80, DfsDs40, DfsDs80, Acorn800, Acorn640, Pc360, Pc720, Pc1200, Pc1440,
            WatSs40, WatSs80, WatDs40, WatDs80 };       // Watford DFS 62-file discs

struct TypeInfo {
    Type type;
    const char *name;           // shown in the New image dialog
    const char *suffix;         // default file extension (no dot)
    bool doubleSided;           // has a side order to choose (Acorn double-sided formats; PC floppies are always interleaved)
    int titleMax;               // longest disc title / volume label
    const char *titleWhat;      // what the title is called on this format
    const char *id;             // the preset's id (acorndw new --format)
};

// Adds this program's presets (ADFS S, M, L; DOS Plus 640K) and the DFS and FAT modules' to r. Adding them again
// changes nothing. types() does it for the program's registry the first time it is asked.
void addPresets(dw::fs::PresetRegistry &r);

const QVector<TypeInfo> &types();       // every format offered, in this program's order (the New image dialog, new --list)
const TypeInfo &info(Type t);

// Writes a blank, valid image of the given type to path (replacing any file there). Double-sided formats are
// written in the requested side order. The title is the ADFS disc name, the DFS title (both sides of a DSD) or
// the DOS volume label; empty = none.
// Acorn640 needs bootSource: the boot track (ADFS catalogue + DOSBOOT) is copied exactly from an existing DOS
// Plus 640K disc image, or built around a DOSBOOT file the way Acorn's discs have it. DOSBOOT is Acorn's code,
// so it can't be built in.
bool create(const QString &path, Type t, const QString &title, bool sequential, QString *err,
            const QString &bootSource = QString());

// Size limit of a DOSBOOT file (sectors 7-15 of the boot track).
const int MaxDosBoot = 9 * 256;

} // namespace NewDisc
