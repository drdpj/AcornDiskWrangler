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

#include <QHash>
#include <QSet>
#include <QTreeWidget>

#include <diskwrangler/ui/discwindow.h>

#include "adfs.h"
#include "adfsvolume.h"
#include "diskformat.h"
#include "dosfs.h"
#include "mainwindow_p.h"
#include "dragout.h"
#include <functional>
#include <memory>

class QAction;
class QLabel;
class QTabWidget;
class QMimeData;

// A pane of the window (the window kit's VolumePane) that accepts files and folders dragged in from the desktop, and
// items dragged between discs and windows. Its rows carry the program's own roles (RoleParent ...) beside the kit's.
class DropTree : public dw::ui::VolumePane {
    Q_OBJECT
public:
    explicit DropTree(QWidget *parent = nullptr);
    void setPane(quint64 owner, int pane) { m_owner = owner; m_pane = pane; }
    int pane() const { return m_pane; }
    // The payload of a drag from this tree: the private formats (copying between this program's windows) plus the
    // files themselves, written only if a drop target asks for them (see dragout.h).
    QMimeData *makeMime(const QList<QTreeWidgetItem *> &items) const;
    // The rows a drag or a copy of these items is made of: no root rows, nothing inside a selected directory.
    QVector<DragRef> refsFor(const QList<QTreeWidgetItem *> &items) const;
    // A drag carries "pid:window:pane" so a drop can tell whether it came from this process.
    static bool fromThisProcess(const QMimeData *m);
signals:
    void pathsDropped(const QStringList &paths, QTreeWidgetItem *target);
    void internalDrop(const QByteArray &payload, QTreeWidgetItem *target);
    void retroFilesDropped(const QByteArray &json, QTreeWidgetItem *target);    // from another program / instance
    void dragOutFinished(int files, int dirs, const QStringList &errors);      // a drag whose files were written
protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    QStringList mimeTypes() const override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)               // Qt 6 passes the list by reference, Qt 5 by value
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
#else
    QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;
#endif
    Qt::DropActions supportedDropActions() const override;
    void startDrag(Qt::DropActions supported) override;
private:
    bool acceptable(const QMimeData *m) const;
    mutable std::shared_ptr<RetroFiles::DragState> m_lastDrag;
    quint64 m_owner = 0;
    int m_pane = 0;
};

struct ImportCtx;

// The main window, on DiskWranglerCore's window kit (step 5i): a dw::ui::DiscWindow showing an Adfs::OpenedDisc, with
// a pane per mount group as tabs that come and go with the disc ("ADFS", "DOS", "DFS": paneShown, paneTitle), the
// rows written by decorateRow from this program's own classes (Adfs::Image, the FAT volume, the DFS catalogue), and
// Delete, New directory and Extract on the kit's patterns; opening on the kit's open pipeline (step 5j: detectImage,
// openDocument, handOverMessage, afterOpen), the side order and Save As through the document; and (step 5k) View,
// Copy, the drags' files and Rename on the kit's copy-out and change patterns, Properties of ADFS and DOS files, the
// disc's name and boot option (volumeEditor) and Check disc (showCheck). A DFS drive's Properties (and its letter
// mover) and the imports are still this program's own code (series 5l moves them); every public function and slot a
// test calls stays, forwarding where the kit now does the work.
class MainWindow : public dw::ui::DiscWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    static MainWindow *byId(quint64 id);
    static QList<MainWindow *> windows();                // every main window, oldest first (the kit's DiscWindow::all())

    dw::app::Document *document() const override { return const_cast<Adfs::OpenedDisc *>(&m_doc); }
    dw::app::AppInfo appInfo() const override;

    const DiskInfo &info() const { return m_doc.info(); }
    DropTree *dfsTree() { return m_dfsTree; }            // both sides of a DFS disc
    QTreeWidgetItem *dfsRoot(int side) const;           // that side's root row ("Drive 0", "Drive 2"), or nullptr
    dw::dfs::Disc &dfs(int side) { return m_doc.dfs(side); }
    int dfsSide() const;            // the DFS tab is showing: the side of the selection (or current row), else -1
    int dfsTargetSide(QTreeWidgetItem *it) const;       // the side a DFS row belongs to (0 for none)
    // Edit > Properties with several DFS files and a directory letter: see mainwindow_dfs.cpp
    int dfsMoveSelected(QChar letter, bool moveOthers, QStringList *clashes, QStringList *errors);
    void refreshDfs();
    bool exportRefs(int pane, const QVector<DragRef> &refs, QVector<XItem> *out, QString *err);
    bool exportRefs(int pane, const QVector<DragRef> &refs, QVector<dw::TransferItem> *out, QString *err);   // a drag's items, for the kit
    bool paneEditable(int pane) const;
    bool isHardDisc() const;                    // an ADFS disc bigger than any floppy (over 640K)
    static MainWindow *openInNewWindow(const QString &path);   // nullptr (and no window) if it can't be opened

    // The kit's open pipeline (openFile; step 5j). forceSequential: -1 = detect the side order (the format's usual
    // order, interleaved, when the contents can't tell), 0 / 1 = read an Acorn double-sided image as interleaved /
    // sequential.
    bool openImage(const QString &path, int forceSequential = -1) { return openFile(path, forceSequential); }
    // Tools > Side order: re-read the image in the given order (nothing is written), or stage a rewrite of the file
    // in the other order (done on Save). confirm = false skips the explanation box (tests).
    bool readWithSideOrder(bool sequential);
    bool convertSideOrder(bool toSequential, bool confirm = true);
    // Boot option of the disc in the current pane (ADFS floppy or hard disc, or one DFS side): -1 if it has none.
    int bootOption() const;
    bool setBootOption(int opt);                     // staged like any edit; shows a message on failure
    void importPaths(const QStringList &paths, quint32 dirSector);
    Adfs::Image &image() { return m_doc.image(); }
    DropTree *tree() { return m_tree; }
    void refreshTree();                         // every pane (the kit's refresh)
    void refreshDos();
    bool save() override;
    dw::fat::Volume &dos() { return m_doc.dos(); }
    DropTree *dosTree() { return m_dosTree; }
    QTabWidget *tabs() { return tabWidget(); }
    bool dosEditable() const { return m_img.editable() && m_doc.dosOk(); }
    bool writeInfOnExtract() const;
    bool dosActive() const;
    QString readOnlyReason() const;                      // empty if the pane in use can be changed
    using DiscWindow::readOnlyReason;

    // the kit's slots, as this program does them
    void refresh() override;
    void newWindow() override;
    void openDialog() override;
    void saveAsDialog() override;
    void revert() override;
    void addFilesDialog() override;
    void addFolderDialog() override;
    void newDirectory() override;
    void editProperties() override;
    void extractSelected() override;
    void about() override;
    void newImageDialog() override;
    void viewSelected() override;
    void renameSelected() override;
    void copySelected() override;
    void paste() override;

protected:
    ActionSpec actionSpec(Act a) const override;
    QStringList paneGroups() const override { return {"ADFS", "DOS", "DFS"}; }
    QString paneTitle(const QString &group) const override;
    void refreshChanged(const QVector<int> &mounts) override;
    bool paneShown(const QString &group) const override;
    dw::ui::PaneLayout paneLayout(const QString &group) const override;
    dw::ui::VolumePane *createPane(const QString &group) override;
    QList<QAction *> contextActions() const override;
    QString rootLabel(int mount) const override;
    void decorateRow(QTreeWidgetItem *row, int mount, const dw::fs::Path &path, const dw::fs::Entry *entry) const override;
    void listFailed(QTreeWidgetItem *row, int mount, const dw::fs::Path &path, const QString &err) const override;
    void orderRows(int mount, const dw::fs::Path &dir, QVector<dw::fs::Entry> *entries) const override;
    bool listChildren(int mount, const dw::fs::Path &dir, int depth) const override;
    QString title() const override;
    QStringList statusVariants() const override;
    void presentStatus(const QStringList &variants) override;
    void updateActions() override;
    bool confirmDiscard() override;
    // opening (the kit's open pipeline, step 5j)
    bool detectImage(const QString &path, int force, dw::fs::Verdict *verdict) override;
    bool openDocument(const QString &path, const dw::fs::Verdict &verdict, const dw::fs::Candidate *pick, int force) override;
    NotOpened handOverMessage(const QString &path, const dw::fs::Verdict &verdict, const dw::fs::Candidate &pick) const override;
    void afterOpen(const QString &path) override;
    dw::ui::DiscWindow *createWindow() override;
    QVector<Item> deleteItems() const override;
    bool confirmDelete(const QVector<Item> &items) override;
    bool deleteEachOnItsOwn() const override { return true; }
    QString deleteItemError(const Item &item, const QString &err) const override;
    QVector<Item> propertiesItems() const override;
    bool propertiesEditor(const QVector<Item> &items, PropertiesEdit *edit) override;
    bool volumeEditor(int mount, const QString &key, VolumeEdit *edit) override;
    void showCheck(const dw::fs::Report &all, const QVector<dw::fs::Report> &perMount) override;
    std::shared_ptr<dw::xfer::HostPolicy> hostPolicy(const QVector<dw::TransferItem> &items) const override;

private slots:
    void checkImage();
    void bootOptionDialog();
    void renameVolume();
    void onDosDropped(const QStringList &paths, QTreeWidgetItem *target);
    void onDropped(const QStringList &paths, QTreeWidgetItem *target);

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
    QVector<DSel> dosSelected() const;
    quint32 dosTargetDir(QTreeWidgetItem *it) const;
    void dosImportPaths(const QStringList &paths, quint32 dirCluster);
    void dosImportFile(const QString &path, quint32 dir, ImportCtx &c);
    void dosImportDir(const QString &path, quint32 parent, ImportCtx &c);
    void dosAddOne(quint32 dir, QString name, const QByteArray &data, const QString &src, ImportCtx &c,
                   bool readOnly = false);
    bool dosProperties(const QVector<Item> &items, PropertiesEdit *edit);
    bool adfsProperties(const QVector<Item> &items, PropertiesEdit *edit);
    void dosCount(quint32 dirCluster, int &files, int &dirs, int &readOnly, int depth) const;
    void importSummary(const ImportCtx &c, bool refreshDosOnly);
    using DiscWindow::importSummary;

    struct FSel {
        int side;
        QChar dir;
        QString name;
        QTreeWidgetItem *item;
    };
    // DFS panes and copying between discs (implemented in mainwindow_dfs.cpp)
    QVector<FSel> dfsSelected() const;                  // from both sides
    bool dfsEditable(int side) const;
    void dfsImportPaths(int side, const QStringList &paths);
    void dfsImportFile(int side, const QString &path, ImportCtx &c);
    void dfsImportDir(int side, const QString &path, ImportCtx &c);
    void dfsAddOne(int side, QChar dir, QString name, const QByteArray &data, quint32 load, quint32 exec,
                   bool locked, bool haveAttrs, const QString &src, ImportCtx &c);
    void dfsProps();
    void dfsRename();
    int dropPane(const DropTree *t, QTreeWidgetItem *it) const;   // the pane a drop onto that row lands in
    quint32 dropTarget(int pane, QTreeWidgetItem *it) const;
    void onInternalDrop(int pane, quint32 dir, const QByteArray &payload);
    void onRetroFilesDrop(int pane, quint32 dir, const QByteArray &json);
    // copying out (5k): the kit's exportItems with this program's dates; a drag's references as the kit's items
    bool exportForCopy(const QVector<Item> &items, QVector<dw::TransferItem> *out, QString *err) const;
    bool refItems(int pane, const QVector<DragRef> &refs, QVector<Item> *out, QString *err) const;
    bool renameRow(const dw::ui::PaneRow &row, const dw::fs::Path &to);
    void importX(int pane, quint32 dir, const QVector<XItem> &items, ImportCtx &c);
    void importXAdfs(quint32 dir, const XItem &x, ImportCtx &c);
    // A DFS file letter.NAME added under ADFS directory *dir: moves *dir into the letter's subdirectory (made if need
    // be) or changes *name, by the "DFS directories become ADFS directories" setting. False (an error in c) if it
    // can't go in. src names the file in messages.
    bool placeDfsLetter(QChar letter, const QString &src, quint32 *dir, QString *name, ImportCtx &c);
    void importXDos(quint32 dir, const XItem &x, ImportCtx &c);
    void importXDfs(int side, const XItem &x, ImportCtx &c, QChar keepDir = QChar());

    QVector<Sel> selectedObjects() const;
    quint32 targetDirForItem(QTreeWidgetItem *it) const;
    QTreeWidget *currentTree() const;
    bool clipboardUsable() const;                         // something Paste can add
    bool pasteItems(int pane, quint32 dir, const QMimeData *m);
    void showReport(const QString &title, const QString &headline, const Adfs::Report &r, bool warn);

    void importFile(const QString &path, quint32 dir, ImportCtx &c);
    void importDir(const QString &path, quint32 parent, ImportCtx &c);
    void addOne(quint32 dir, QString name, const QByteArray &data, quint32 load, quint32 exec,
                quint8 flags, bool haveAttrs, const QString &src, ImportCtx &c);
    bool isSidecar(const QString &path) const;
    bool dropImages(int pane, const QStringList &paths, std::function<void(const QStringList &)> importRest);
    void countTree(quint32 dirSector, int &files, int &dirs, int &locked, int depth) const;

    // The pane a mount is shown in, and back: PaneAdfs, PaneDos, PaneDfs0 / PaneDfs1 (-1: no such mount).
    int paneOf(int mount) const;
    int mountOf(int pane) const;
    void refreshPane(DropTree *t);              // one pane's rows again (its mounts unchanged), then updateUi
    // What a row of a pane is, read from this program's own classes while the rows are made (decorateRow): the
    // directory a row's path names (its sector, or its cluster), each directory read once, and the next entry of each.
    static QString rowKey(int mount, const dw::fs::Path &p) { return QString::number(mount) + QChar(1) + p.parts.join(QChar(1)); }
    mutable QHash<QString, quint32> m_rowDir;   // a directory row's sector (ADFS) or cluster (DOS)
    mutable QHash<QString, int> m_rowNext;      // the position of the next row made under a directory
    mutable QHash<quint32, Adfs::Dir> m_adfsDirs;
    mutable QHash<quint32, QVector<dw::fat::DEntry>> m_dosDirs;
    mutable QSet<quint32> m_seenAdfs, m_seenDos;   // directories already shown (a loop on a damaged disc stops there)
    void clearRowCache() const;

    Adfs::OpenedDisc m_doc;
    Adfs::Image &m_img = m_doc.image();
    dw::fat::Volume &m_dos = m_doc.dos();
    DropTree *m_tree = nullptr, *m_dosTree = nullptr, *m_dfsTree = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_status;
    QAction *m_actOpen, *m_actSave, *m_actSaveAs, *m_actRevert, *m_actClose, *m_actQuit, *m_actCopy, *m_actPaste;
    QAction *m_actAddFiles, *m_actAddFolder, *m_actNewDir, *m_actDelete, *m_actProps, *m_actExtract;
    QAction *m_actCheck, *m_optReadInf, *m_optWriteInf, *m_optBackup, *m_optLetterDirs, *m_actView, *m_actRename, *m_actRenameVol;
    QAction *m_actNewWin = nullptr;
    QAction *m_actNewImage = nullptr;
    QAction *m_actBootOpt = nullptr;
    QAction *m_actReadIl = nullptr, *m_actReadSeq = nullptr, *m_actConvertSides = nullptr;
    QString m_sidesHow;            // how the side order was settled: "" (detected), "assumed", "set by hand"
    DiskInfo m_unknown;            // what detection says of a disc nothing recognised (opened as ADFS)
    DiskInfo m_opened;             // the reading just opened, for afterOpen's notices
};
