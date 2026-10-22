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
#pragma once

#include <QMainWindow>
#include <QSet>
#include <QTreeWidget>

#include "adfs.h"
#include "diskformat.h"
#include "dosfs.h"
#include "mainwindow_p.h"

class QAction;
class QLabel;
class QTabWidget;
class QMimeData;

// Tree that accepts files/folders dragged in from the desktop, and items dragged between discs/windows.
class DropTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit DropTree(QWidget *parent = nullptr);
    void setPane(quint64 owner, int pane) { m_owner = owner; m_pane = pane; }
    int pane() const { return m_pane; }
    QMimeData *makeMime(const QList<QTreeWidgetItem *> &items) const;   // the payload of a drag from this tree
signals:
    void pathsDropped(const QStringList &paths, QTreeWidgetItem *target);
    void internalDrop(const QByteArray &payload, QTreeWidgetItem *target);
protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;
    Qt::DropActions supportedDropActions() const override;
private:
    bool acceptable(const QMimeData *m) const;
    quint64 m_owner = 0;
    int m_pane = 0;
};

struct ImportCtx;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    quint64 id() const { return quint64(quintptr(this)); }
    static MainWindow *byId(quint64 id);
    static QList<MainWindow *> &windows();
    const DiskInfo &info() const { return m_info; }
    DropTree *dfsTree(int side) { return m_dfsTree[side]; }
    Dfs::Disc &dfs(int side) { return m_dfs[side]; }
    int dfsSide() const;            // 0/1 if a DFS tab is showing, else -1
    void refreshDfs();
    bool exportRefs(int pane, const QVector<DragRef> &refs, QVector<XItem> *out, QString *err);
    bool paneEditable(int pane) const;

    // forceSequential: -1 = detect the side order (the format's usual order, interleaved, when the contents can't
    // tell), 0 / 1 = read an Acorn double-sided image as interleaved / sequential.
    bool openImage(const QString &path, int forceSequential = -1);
    // Tools > Side order: re-read the image in the given order (nothing is written), or stage a rewrite of the file
    // in the other order (done on Save). confirm = false skips the explanation box (tests).
    bool readWithSideOrder(bool sequential);
    bool convertSideOrder(bool toSequential, bool confirm = true);
    // Boot option of the disc in the current pane (ADFS floppy or hard disc, or one DFS side): -1 if it has none.
    int bootOption() const;
    bool setBootOption(int opt);                     // staged like any edit; shows a message on failure
    void importPaths(const QStringList &paths, quint32 dirSector);
    Adfs::Image &image() { return m_img; }
    DropTree *tree() { return m_tree; }
    void refreshTree();
    void refreshDos();
    bool save();
    Dos::Volume &dos() { return m_dos; }
    DropTree *dosTree() { return m_dosTree; }
    QTabWidget *tabs() { return m_tabs; }
    bool dosEditable() const { return m_img.editable() && m_dosOk; }
    bool dosActive() const;

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void openDialog();
    void revert();
    void addFilesDialog();
    void addFolderDialog();
    void newDirectory();
    void deleteSelected();
    void editProperties();
    void extractSelected();
    void checkImage();
    void about();
    void newImageDialog();
    void bootOptionDialog();
    void viewSelected();
    void renameSelected();
    void renameVolume();
    void onDosDropped(const QStringList &paths, QTreeWidgetItem *target);
    void onDropped(const QStringList &paths, QTreeWidgetItem *target);
    void showContextMenu(const QPoint &pos);
    void updateUi();

private:
    struct Sel {
        quint32 parent;
        QString name;
        bool isDir;
        quint32 sector;
        QTreeWidgetItem *item;
    };

    struct DSel {
        quint32 parent;
        QString name;
        bool isDir;
        quint32 cluster;
        QTreeWidgetItem *item;
    };
    // DOS partition (implemented in mainwindow_dos.cpp)
    bool findDriveC(Adfs::Entry *out) const;
    void populateDos(QTreeWidgetItem *parentItem, quint32 dirCluster, QSet<quint32> &seen, int depth);
    QVector<DSel> dosSelected() const;
    quint32 dosTargetDir(QTreeWidgetItem *it) const;
    void dosImportPaths(const QStringList &paths, quint32 dirCluster);
    void dosImportFile(const QString &path, quint32 dir, ImportCtx &c);
    void dosImportDir(const QString &path, quint32 parent, ImportCtx &c);
    void dosAddOne(quint32 dir, QString name, const QByteArray &data, const QString &src, ImportCtx &c,
                   bool readOnly = false);
    void dosNewDir(const QString &name);
    void dosDelete();
    void dosProps();
    void dosExtract();
    void dosExtractEntry(const Dos::DEntry &e, quint32 cluster, const QString &hostDir, QStringList &errors);
    void dosCount(quint32 dirCluster, int &files, int &dirs, int &readOnly, int depth) const;
    void importSummary(const ImportCtx &c, bool refreshDosOnly);

    struct FSel {
        QChar dir;
        QString name;
        QTreeWidgetItem *item;
    };
    // DFS panes and copying between discs (implemented in mainwindow_dfs.cpp)
    QVector<FSel> dfsSelected(int side) const;
    bool dfsEditable(int side) const;
    void dfsImportPaths(int side, const QStringList &paths);
    void dfsImportFile(int side, const QString &path, ImportCtx &c);
    void dfsImportDir(int side, const QString &path, ImportCtx &c);
    void dfsAddOne(int side, QChar dir, QString name, const QByteArray &data, quint32 load, quint32 exec,
                   bool locked, bool haveAttrs, const QString &src, ImportCtx &c);
    void dfsDelete(int side);
    void dfsProps(int side);
    void dfsExtract(int side);
    void dfsRename(int side);
    void dfsRenameVolume(int side);
    int paneOf(const QTreeWidget *t) const;
    quint32 dropTarget(int pane, QTreeWidgetItem *it) const;
    void onInternalDrop(int pane, quint32 dir, const QByteArray &payload);
    bool exportAdfs(quint32 parent, const QString &name, XItem *x, QString *err, int depth);
    bool exportDos(quint32 parent, const QString &name, XItem *x, QString *err, int depth);
    bool exportDfs(int side, QChar dir, const QString &name, XItem *x, QString *err);
    void importX(int pane, quint32 dir, const QVector<XItem> &items, ImportCtx &c);
    void importXAdfs(quint32 dir, const XItem &x, ImportCtx &c);
    void importXDos(quint32 dir, const XItem &x, ImportCtx &c);
    void importXDfs(int side, const XItem &x, ImportCtx &c);
    void ensureTab(QWidget *w, const QString &text, bool want);

    void populate(QTreeWidgetItem *parentItem, quint32 dirSector, QSet<quint32> &seen, int depth);
    QVector<Sel> selectedObjects() const;
    quint32 targetDirForItem(QTreeWidgetItem *it) const;
    bool confirmDiscard();
    void showReport(const QString &title, const QString &headline, const Adfs::Report &r, bool warn);

    void importFile(const QString &path, quint32 dir, ImportCtx &c);
    void importDir(const QString &path, quint32 parent, ImportCtx &c);
    void addOne(quint32 dir, QString name, const QByteArray &data, quint32 load, quint32 exec,
                quint8 flags, bool haveAttrs, const QString &src, ImportCtx &c);
    bool isSidecar(const QString &path) const;

    void extractEntry(quint32 parent, const Adfs::Entry &e, const QString &hostDir, QStringList &errors);
    void countTree(quint32 dirSector, int &files, int &dirs, int &locked, int depth) const;

    Adfs::Image m_img;
    DropTree *m_tree;
    QLabel *m_status;
    QAction *m_actOpen, *m_actSave, *m_actRevert, *m_actQuit;
    QAction *m_actAddFiles, *m_actAddFolder, *m_actNewDir, *m_actDelete, *m_actProps, *m_actExtract;
    QAction *m_actCheck, *m_optReadInf, *m_optWriteInf, *m_optBackup, *m_actView, *m_actRename, *m_actRenameVol;
    QTabWidget *m_tabs;
    DropTree *m_dosTree;
    Dos::Volume m_dos;
    bool m_dosOk = false;          // DOS volume passed its consistency check when the image was opened
    DiskInfo m_info;               // what kind of disc this is
    DropTree *m_dfsTree[2] = {nullptr, nullptr};
    Dfs::Disc m_dfs[2];
    bool m_dfsOk[2] = {false, false};
    QString m_dfsErr[2];
    QAction *m_actNewWin = nullptr;
    QAction *m_actNewImage = nullptr;
    QAction *m_actBootOpt = nullptr;
    QAction *m_actReadIl = nullptr, *m_actReadSeq = nullptr, *m_actConvertSides = nullptr;
    QString m_sidesHow;            // how the side order was settled: "" (detected), "assumed", "set by hand"
};
