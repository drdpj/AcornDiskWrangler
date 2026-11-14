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
// mainwindow_p.h - helpers shared by mainwindow.cpp and mainwindow_dos.cpp
#pragma once
#include <QSet>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QDateTime>
#include <QVector>
#include <QFontMetrics>
#include <QHeaderView>
#include <QKeySequence>
#include <QList>
#include <QStyle>
#include <QTreeWidget>
#include <initializer_list>
#include <diskwrangler/ui/treecolumns.h>
#include <diskwrangler/ui/uihelpers.h>
#include <diskwrangler/xfer/hostname.h>
#include <diskwrangler/xfer/mime.h>
#include <diskwrangler/fs/dfs/inf.h>
#include <diskwrangler/fs/volume.h>
#include "adfs.h"
#include <diskwrangler/fs/dfs/dfs.h>
#include <diskwrangler/fs/fat/fat.h>
#include "hostpolicy.h"

enum Roles { RoleParent = Qt::UserRole, RoleName, RoleIsDir, RoleSector, RoleIsRoot, RoleSide };   // RoleSide: DFS rows

inline QString fmtSectors(quint32 s) {
    double b = double(s) * Adfs::SectorSize;
    if (b >= 1024.0 * 1024.0) return QString("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
}

// ------------------------------------------------------------- panes and cross-disc copying

enum Pane { PaneAdfs = 0, PaneDos = 1, PaneDfs0 = 2, PaneDfs1 = 3 };

// One dragged item as identified inside its source window (parent dir sector/cluster + name).
struct DragRef {
    quint32 parent = 0;
    QString name;
    bool isDir = false;
    int mount = -1;             // the kit's address of the row, filled in where the row is known (refsFor)
    dw::fs::Path path;
};

