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
#include "dfs.h"
#include "dosfs.h"
#include "hostpolicy.h"

enum Roles { RoleParent = Qt::UserRole, RoleName, RoleIsDir, RoleSector, RoleIsRoot, RoleSide };   // RoleSide: DFS rows

struct ImportCtx {
    int files = 0, dirs = 0, replaced = 0, skipped = 0;
    int policy = 0;                 // 0 = ask, 1 = replace all, 2 = skip all
    int letterPolicy = 0;           // single-letter ADFS directory -> DFS: 0 = ask, 1 = keep letter, 2 = use $
    bool cancelled = false;
    QStringList errors;
    QSet<QString> created;          // keys of names created/replaced during this import
};

inline QString keyFor(quint32 dir, const QString &name) { return QString::number(dir) + ":" + name.toUpper(); }

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

// A file or directory read out of one kind of disc, ready to be written into any other kind.
struct XItem {
    QString name;               // name in the source (ADFS name, DOS "NAME.EXT", DFS name without directory)
    QChar dfsDir = '$';         // DFS directory letter (DFS sources only)
    bool isDir = false;
    QByteArray data;
    quint32 load = 0, exec = 0;
    bool hasAddr = false;       // load/exec are meaningful (ADFS and DFS sources)
    bool locked = false;        // ADFS L / DFS locked / DOS read-only
    quint8 adfsFlags = 0;       // Adfs::FlagR.. bits (ADFS sources)
    int src = PaneAdfs;         // which kind of disc it came from
    quint8 dosAttr = 0;         // FAT attribute bits (DOS sources)
    QDateTime when;             // the date the source records, as shown in the tool (invalid if none); local, no zone
    QVector<XItem> kids;
};

inline QString uniqueName(const QString &base, const Adfs::Dir &d, const ImportCtx &c, quint32 dir) {
    for (int i = 1; i < 1000; ++i) {
        QString suf = "~" + QString::number(i);
        QString n = base.left(Adfs::MaxNameLen - suf.size()) + suf;
        if (d.indexOf(n) < 0 && !c.created.contains(keyFor(dir, n))) return n;
    }
    return base;
}

inline QString uniqueDosName(const QString &name, const QVector<dw::fat::DEntry> &L, const ImportCtx &c, quint32 dir) {
    int dot = name.lastIndexOf('.');
    QString base = dot < 0 ? name : name.left(dot), ext = dot < 0 ? QString() : name.mid(dot);
    for (int i = 1; i < 1000; ++i) {
        QString suf = "~" + QString::number(i);
        QString n = base.left(8 - suf.size()) + suf + ext;
        bool clash = c.created.contains(QString("D%1:%2").arg(dir).arg(n.toUpper()));
        for (const dw::fat::DEntry &e : L) if (e.name.compare(n, Qt::CaseInsensitive) == 0) clash = true;
        if (!clash) return n;
    }
    return name;
}

inline QString dfsKey(int side, QChar dir, const QString &name) {
    return QString("F%1:%2.%3").arg(side).arg(QString(dir.toUpper())).arg(name.toUpper());
}

inline QString uniqueDfsName(const QString &base, QChar dir, const dw::dfs::Disc &d, const ImportCtx &c, int side) {
    for (int i = 1; i < 100; ++i) {
        QString suf = "~" + QString::number(i);
        QString n = base.left(dw::dfs::MaxName - suf.size()) + suf;
        if (d.indexOf(dir, n) < 0 && !c.created.contains(dfsKey(side, dir, n))) return n;
    }
    return base;
}
