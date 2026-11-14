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
// dragout.h - dragging files out of a disc to the host or to another program, and the "retro-files" drag format
// (docs/retro-files-format.md) in both directions. The format itself - the document, the path checks, the drag data -
// is DiskWranglerCore's (diskwrangler/xfer, diskwrangler/ui/dragoutmime.h); this file holds what is Acorn's: the drag
// folders and how a drag is written out (dragSpec: host names, DFS names, the .inf sidecar files).
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QMimeData>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include <diskwrangler/ui/dragoutmime.h>
#include <diskwrangler/xfer/transferitem.h>

#include "mainwindow_p.h"      // the pane numbers

namespace RetroFiles {

extern const char *const MimeType;          // "application/x-retro-files+json"

// filesystem name used in the JSON for a pane kind: "adfs", "dfs", "fat"
QString filesystemName(int srcPane);

// Temporary folders for drags (the core's DragOutMime makes one per drag, kept until the program exits: a drop
// target may still be copying when the drag returns). cleanStale() removes ones left behind by a crash, older than
// maxAgeSecs.
QString dragDirPrefix();                     // e.g. /tmp/acorndiskwrangler-drag-
int cleanStale(qint64 maxAgeSecs = 24 * 3600);

// What a drag did, for the window that started it (filled in when a target asks for the files).
typedef dw::DragState DragState;

// How this program's drags (and Edit > Copy) are written out: the core's DragSpec with Acorn's policy (host-safe
// names, DFS NAME or D.NAME, .inf sidecars when writeInf is set), the source filesystem of the pane ("adfs", "dfs",
// "fat") and this program's drag folders. The window builds a dw::DragOutMime from it and an exporter of
// TransferItems (MainWindow::exportRefs).
dw::DragSpec dragSpec(bool writeInf, const QString &imageName, int srcPane);

} // namespace RetroFiles
