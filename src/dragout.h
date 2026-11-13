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
// is DiskWranglerCore's (diskwrangler/xfer, diskwrangler/ui/dragoutmime.h); this file turns this program's XItem into
// the core's dw::TransferItem and back, and holds what is Acorn's: ADFS / DFS / FAT attributes, DFS names and the
// .inf sidecar files.
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

#include "mainwindow_p.h"

namespace RetroFiles {

extern const char *const MimeType;          // "application/x-retro-files+json"

// filesystem name used in the JSON for a pane kind: "adfs", "dfs", "fat"
QString filesystemName(int srcPane);

// XItem <-> the core's item. toTransfer puts the attributes in the "acorn" (ADFS, DFS) or "fat" family exactly as
// the document lists them; fromTransfer reads them back from whichever family the item has.
dw::TransferItem toTransfer(const XItem &x);
XItem fromTransfer(const dw::TransferItem &t);

// Writes items (files and directory trees) into hostDir the way Extract to host does: host-safe names, a folder per
// directory, DFS files named NAME or D.NAME, and Acorn .inf sidecars (ADFS and DFS files) when writeInf is set.
// Names that would clash in one folder (compared ignoring case) are made unique. The file date is set from
// XItem::when. Returns the paths (relative to hostDir) of the top-level items in *topLevel and appends one JSON
// item per file and directory to *items. Errors (e.g. a disc full) go to *errors; the rest is still written.
void writeHost(const QVector<XItem> &items, const QString &hostDir, bool writeInf, QStringList *topLevel,
               QJsonArray *jsonItems, QStringList *errors);

// The JSON document for files written by writeHost.
QByteArray makeJson(const QJsonArray &items, const QString &hostDir, const QString &imageName, int srcPane);

// Reads a JSON document (from any program) and the files it lists, into an XItem tree ready for
// MainWindow::importX. Items from a filesystem this program doesn't know become plain host files.
bool readJson(const QByteArray &json, QVector<XItem> *out, QString *err);

// Temporary folders for drags (the core's DragOutMime makes one per drag, kept until the program exits: a drop
// target may still be copying when the drag returns). cleanStale() removes ones left behind by a crash, older than
// maxAgeSecs.
QString dragDirPrefix();                     // e.g. /tmp/acorndiskwrangler-drag-
int cleanStale(qint64 maxAgeSecs = 24 * 3600);

// What a drag did, for the window that started it (filled in when a target asks for the files).
typedef dw::DragState DragState;

// The data of a drag out of a disc: the program's own private formats (set with setData) plus the files, as
// text/uri-list and the retro-files JSON. The files are written only when a drop target first asks for either,
// so a drag between this program's own windows, or one that is cancelled, writes nothing.
class DragOutMime : public dw::DragOutMime {
    Q_OBJECT
public:
    typedef dw::DragOutMime::Exporter Exporter;       // fills the core's items: the window kit's exportItems
    DragOutMime(Exporter exporter, bool writeInf, const QString &imageName, int srcPane);
};

} // namespace RetroFiles
