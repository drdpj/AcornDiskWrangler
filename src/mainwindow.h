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


// The main window, on DiskWranglerCore's window kit (step 5i): a dw::ui::DiscWindow showing an Adfs::OpenedDisc, with
// a pane per mount group as tabs that come and go with the disc ("ADFS", "DOS", "DFS": paneShown, paneTitle), the
// rows written by decorateRow from this program's own classes (Adfs::Image, the FAT volume, the DFS catalogue), and
// Delete, New directory and Extract on the kit's patterns; opening on the kit's open pipeline (step 5j: detectImage,
// openDocument, handOverMessage, afterOpen), the side order and Save As through the document; and (step 5k) View,
// Copy, the drags' files and Rename on the kit's copy-out and change patterns, Properties of ADFS and DOS files, the
// disc's name and boot option (volumeEditor) and Check disc (showCheck); and (step 5l) every import on the kit's import
// session (importItems, with this program's hooks: its names, attributes, DFS letter rules, questions and summary),
// host files read by the core's one host reader, and a DFS drive's Properties (and its letter mover) on the kit's
// Properties hooks. The tests reach the window through the kit (document(), pane(group), openFile), not through forwards.
class MainWindow : public dw::ui::DiscWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    static MainWindow *byId(quint64 id);

    // The document, typed: the image, the DFS discs of the two sides (dfs(side)), the FAT volume (dos()), the reading (info()).
    Adfs::OpenedDisc *document() const override { return const_cast<Adfs::OpenedDisc *>(&m_doc); }
    dw::app::AppInfo appInfo() const override;

    QTreeWidgetItem *dfsRoot(int side) const;           // that side's root row ("Drive 0", "Drive 2"), or nullptr
    int dfsSide() const;            // the DFS tab is showing: the side of the selection (or current row), else -1
    int dfsTargetSide(QTreeWidgetItem *it) const;       // the side a DFS row belongs to (0 for none)
    // Edit > Properties with several DFS files and a directory letter: see mainwindow_dfs.cpp
    int dfsMoveSelected(QChar letter, bool moveOthers, QStringList *clashes, QStringList *errors);
    bool paneEditable(int pane) const;
    bool isHardDisc() const;                    // an ADFS disc bigger than any floppy (over 640K)
    static MainWindow *openInNewWindow(const QString &path);   // nullptr (and no window) if it can't be opened

    // (Opening is the kit's openFile(path, forceSequential): -1 = detect the side order (the format's usual order,
    // interleaved, when the contents can't tell), 0 / 1 = read an Acorn double-sided image as interleaved / sequential.)
    // Tools > Side order: re-read the image in the given order (nothing is written), or stage a rewrite of the file
    // in the other order (done on Save). confirm = false skips the explanation box (tests).
    bool readWithSideOrder(bool sequential);
    bool convertSideOrder(bool toSequential, bool confirm = true);
    // Boot option of the disc in the current pane (ADFS floppy or hard disc, or one DFS side): -1 if it has none.
    int bootOption() const;
    bool setBootOption(int opt);                     // staged like any edit; shows a message on failure
    bool save() override;
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
    QString propertiesItemError(const PropertyChange &c, const QString &err) const override;
    bool volumeEditor(int mount, const QString &key, VolumeEdit *edit) override;
    void showCheck(const dw::fs::Report &all, const QVector<dw::fs::Report> &perMount) override;
    std::shared_ptr<dw::xfer::HostPolicy> hostPolicy(const QVector<dw::TransferItem> &items) const override;
    // importing (step 5l): the session's hooks for a pane, its question and its summary
    dw::ui::ImportHooks importHooks(int mount) override;
    dw::xfer::Conflict askReplace(int mount, const dw::fs::Path &target, const dw::fs::Entry &existing,
                                  const dw::TransferItem &incoming) override;
    void importSummary(const dw::xfer::ImportResult &r) override;

private slots:
    void checkImage();
    void bootOptionDialog();
    void renameVolume();
    void onDosDropped(const QStringList &paths, QTreeWidgetItem *target);
    void onDropped(const QStringList &paths, QTreeWidgetItem *target);

private:
    friend class DropTree;
    struct XItem;                                       // a disc's file as the import hooks read it (mainwindow_dfs.cpp)
    bool exportRefs(int pane, const QVector<DragRef> &refs, QVector<dw::TransferItem> *out, QString *err);   // a drag's items, for the kit
    void refreshDfs();
    void importPaths(const QStringList &paths, quint32 dirSector);
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
    bool dosProperties(const QVector<Item> &items, PropertiesEdit *edit);
    bool adfsProperties(const QVector<Item> &items, PropertiesEdit *edit);
    void dosCount(quint32 dirCluster, int &files, int &dirs, int &readOnly, int depth) const;

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
    bool dfsProperties(const QVector<Item> &items, PropertiesEdit *edit);
    QVector<int> planDfsMove(const QVector<FSel> &sel, QChar letter, QStringList *clashes) const;
    void dfsRename();
    int dropPane(const DropTree *t, QTreeWidgetItem *it) const;   // the pane a drop onto that row lands in
    quint32 dropTarget(int pane, QTreeWidgetItem *it) const;
    void onInternalDrop(int pane, quint32 dir, const QByteArray &payload);
    void onRetroFilesDrop(int pane, quint32 dir, const QByteArray &json);
    // copying out (5k): the kit's exportItems with this program's dates; a drag's references as the kit's items
    bool exportForCopy(const QVector<Item> &items, QVector<dw::TransferItem> *out, QString *err) const;
    bool refItems(int pane, const QVector<DragRef> &refs, QVector<Item> *out, QString *err) const;
    bool renameRow(const dw::ui::PaneRow &row, const dw::fs::Path &to);
    // importing (mainwindow_dfs.cpp): every import ends in importAll, the kit's importItems with this program's hooks
    struct ImportRun {
        int pane = PaneAdfs;
        bool internal = false;          // the items were copied in this program (a drop from one of its windows, its clipboard)
        bool refreshAll = false;        // every pane made again before the summary
        int letterPolicy = 0;           // the single-letter directory question's "to all": 0 ask, 1 keep the letter, 2 use $
        QStringList problems;           // the summary's lines, in the order met
        QHash<QString, QString> src, letterSrc;    // an item's Importer "from" -> how messages name it
        QHash<QString, bool> isDir;
        int readErrors = 0, hooked = 0;
    };
    ImportRun m_run;
    void hostImport(int pane, quint32 dir, const QStringList &paths, bool useInf);
    void importAll(int pane, quint32 dir, const QVector<dw::TransferItem> &items,
                   const QVector<dw::xfer::HostReadError> &readProblems, bool internal, bool refreshAll);
    XItem asX(const dw::TransferItem &t) const;
    QString importSrc(const dw::TransferItem &item) const;
    void mapImportItem(const QString &from, const dw::TransferItem &t);
    dw::fs::Path dirPath(int pane, quint32 dir) const;
    // A DFS file letter.NAME added under an ADFS directory: p's directory or name, by the "DFS directories become ADFS
    // directories" setting (a letter ADFS can't have: X_NAME, its warning among the summary's problems).
    void placeDfsLetter(QChar letter, dw::fs::Placement *p);
    QString importErrorText(const dw::xfer::ImportError &e) const;

    QVector<Sel> selectedObjects() const;
    quint32 targetDirForItem(QTreeWidgetItem *it) const;
    QTreeWidget *currentTree() const;
    bool clipboardUsable() const;                         // something Paste can add
    bool pasteItems(int pane, quint32 dir, const QMimeData *m);
    void showReport(const QString &title, const QString &headline, const Adfs::Report &r, bool warn);

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
