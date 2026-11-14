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
#include "dragout.h"
#include "hostpolicy.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <diskwrangler/xfer/hostname.h>
#include <diskwrangler/xfer/retrofiles.h>

namespace RetroFiles {

const char *const MimeType = "application/x-retro-files+json";
static const char *const Generator = "Acorn Disk Wrangler";

QString filesystemName(int srcPane) {
    if (srcPane == PaneAdfs) return "adfs";
    if (srcPane == PaneDfs0 || srcPane == PaneDfs1) return "dfs";
    return "fat";
}

// ------------------------------------------------------------------------------ temporary folders

QString dragDirPrefix() { return QDir::temp().filePath("acorndiskwrangler-drag-"); }

int cleanStale(qint64 maxAgeSecs) { return dw::xfer::cleanStale(dragDirPrefix(), maxAgeSecs); }

// ------------------------------------------------------------------------------ the drag data

dw::DragSpec dragSpec(bool writeInf, const QString &imageName, int srcPane) {
    dw::DragSpec s;
    s.policy = std::make_shared<AcornPolicy>(writeInf);
    s.filesystem = filesystemName(srcPane);
    s.generator = Generator;
    s.imageName = imageName;
    s.tempPrefix = dragDirPrefix();
    return s;
}

} // namespace RetroFiles
