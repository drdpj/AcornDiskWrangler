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
#include "adfs.h"
#include "dfs.h"
#include "dosfs.h"

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

inline QString fmtBytes(quint64 b) {
    if (b >= 1024ull * 1024) return QString("%1 MB").arg(double(b) / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 KB").arg(double(b) / 1024.0, 0, 'f', 1);
}

// ------------------------------------------------------------- tree column layout (all panes)
// Every pane is laid out by setupTreeColumns() so they look the same: the Name column takes the
// spare width (so the last column always meets the right-hand edge), every other column is sized
// from the font (never fixed pixels - those break on scaled displays), and numbers are right-
// aligned, header included, with the same trailing pad (setNumCell) so they never touch the next
// column. Give a column that appears in several panes the same sample so it lines up across them.
struct ColSpec {
    const char *label;
    const char *sample;      // widest text the column normally shows (ignored for the Name column)
    bool numeric;
};

static const char *const NumPad = "  ";

inline void setupTreeColumns(QTreeWidget *t, std::initializer_list<ColSpec> cols) {
    const QFontMetrics fm(t->font());
    const int margin = 2 * t->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, t) + fm.averageCharWidth();
    QStringList labels;
    for (const ColSpec &c : cols) labels << (c.numeric ? QString(c.label) + NumPad : QString(c.label));
    t->setColumnCount(labels.size());
    t->setHeaderLabels(labels);
    QHeaderView *h = t->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    int fixed = 0, i = 0;
    for (const ColSpec &c : cols) {
        if (i > 0) {
            QString pad = c.numeric ? NumPad : "";
            int w = qMax(fm.horizontalAdvance(c.sample + pad), fm.horizontalAdvance(labels[i])) + margin;
            t->setColumnWidth(i, w);
            fixed += w;
            if (c.numeric) t->headerItem()->setTextAlignment(i, Qt::AlignRight | Qt::AlignVCenter);
        }
        ++i;
    }
    // Keep a readable Name column: the window can't be made so narrow that the columns squash it.
    int sb = t->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, t);
    t->setMinimumWidth(fixed + fm.averageCharWidth() * 22 + sb + 2 * t->frameWidth());
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->setUniformRowHeights(true);
    t->setContextMenuPolicy(Qt::CustomContextMenu);
}

inline void setNumCell(QTreeWidgetItem *it, int col, const QString &text) {
    it->setText(col, text + NumPad);
    it->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
}

// "$   [BeebSCSI0]", "C:\   [Master 512]", "Drive 0   [SIDE0]": the same shape for every pane's root row.
inline QString rootLabel(const QString &base, const QString &title) {
    return title.isEmpty() ? base : base + "   [" + title + "]";
}

inline QString hostSafeName(const QString &n) {
    QString s = n;
    for (QChar &c : s)
        if (QString("/\\:*?\"<>|").contains(c) || c.unicode() < 0x20) c = '_';
    // names Windows reserves for devices, with or without an extension (an ADFS file can be called "Aux")
    static const QStringList dev = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (dev.contains(s.section('.', 0, 0), Qt::CaseInsensitive)) s.insert(s.indexOf('.') < 0 ? s.size() : s.indexOf('.'), '_');
    if (s.isEmpty() || s == "." || s == "..") s = "_" + s;
    while (s.endsWith('.') || s.endsWith(' ')) s[s.size() - 1] = '_';     // Windows drops these silently
    return s;
}

// ------------------------------------------------------------- panes and cross-disc copying

enum Pane { PaneAdfs = 0, PaneDos = 1, PaneDfs0 = 2, PaneDfs1 = 3 };

#define WRANGLER_MIME_ITEMS  "application/x-acorndiskwrangler-items"
#define WRANGLER_MIME_SOURCE "application/x-acorndiskwrangler-source"
#define WRANGLER_MIME_CLIP   "application/x-acorndiskwrangler-clip"     // "pid:n": what Edit > Copy put there

// Delete: the platform's Delete key(s), and on macOS Finder's Cmd+Backspace too (most Mac keyboards have no
// forward-delete key). Same in CP/M Disk Wrangler.
inline QList<QKeySequence> deleteKeys() {
    QList<QKeySequence> k = QKeySequence::keyBindings(QKeySequence::Delete);
#ifdef Q_OS_MACOS
    k << QKeySequence("Ctrl+Backspace");               // Ctrl is the Command key on macOS
#endif
    return k;
}

// One dragged item as identified inside its source window (parent dir sector/cluster + name).
struct DragRef {
    quint32 parent = 0;
    QString name;
    bool isDir = false;
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

// .inf sidecar: NAME LOAD EXEC [LENGTH] [ATTR|L]; NAME may be "D.NAME" (DFS directory letter)
struct InfData {
    QString name;
    QChar dir;
    quint32 load = 0, exec = 0;
    quint8 flags = Adfs::FlagR | Adfs::FlagW;
};
bool readInf(const QString &hostPath, InfData *out);

inline QString uniqueName(const QString &base, const Adfs::Dir &d, const ImportCtx &c, quint32 dir) {
    for (int i = 1; i < 1000; ++i) {
        QString suf = "~" + QString::number(i);
        QString n = base.left(Adfs::MaxNameLen - suf.size()) + suf;
        if (d.indexOf(n) < 0 && !c.created.contains(keyFor(dir, n))) return n;
    }
    return base;
}

inline QString uniqueDosName(const QString &name, const QVector<Dos::DEntry> &L, const ImportCtx &c, quint32 dir) {
    int dot = name.lastIndexOf('.');
    QString base = dot < 0 ? name : name.left(dot), ext = dot < 0 ? QString() : name.mid(dot);
    for (int i = 1; i < 1000; ++i) {
        QString suf = "~" + QString::number(i);
        QString n = base.left(8 - suf.size()) + suf + ext;
        bool clash = c.created.contains(QString("D%1:%2").arg(dir).arg(n.toUpper()));
        for (const Dos::DEntry &e : L) if (e.name.compare(n, Qt::CaseInsensitive) == 0) clash = true;
        if (!clash) return n;
    }
    return name;
}

inline QString dfsKey(int side, QChar dir, const QString &name) {
    return QString("F%1:%2.%3").arg(side).arg(QString(dir.toUpper())).arg(name.toUpper());
}

inline QString uniqueDfsName(const QString &base, QChar dir, const Dfs::Disc &d, const ImportCtx &c, int side) {
    for (int i = 1; i < 100; ++i) {
        QString suf = "~" + QString::number(i);
        QString n = base.left(Dfs::MaxName - suf.size()) + suf;
        if (d.indexOf(dir, n) < 0 && !c.created.contains(dfsKey(side, dir, n))) return n;
    }
    return base;
}

// DFS keeps 18-bit addresses; values with bits 16 and 17 set stand for 0xFFFFxxxx (I/O processor).
inline quint32 extendDfsAddr(quint32 v) { return (v & 0x30000u) == 0x30000u ? (v | 0xFFFC0000u) : v; }
