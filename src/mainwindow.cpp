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
#include "mainwindow.h"
#include <diskwrangler/app/appsettings.h>
#include <diskwrangler/ui/buttonbar.h>
#include <diskwrangler/ui/licences.h>
#include "mainwindow_p.h"
#include "viewer.h"
#include "newdisc.h"
#include "dragout.h"
#include <QPointer>

#include <QAction>
#include <QApplication>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialog>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QDataStream>
#include <QTabBar>
#include <QTimer>
#include <QDialogButtonBox>
#include <QDir>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <diskwrangler/ui/handoffdialog.h>
#include <QRegularExpressionValidator>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QJsonArray>
#include <algorithm>
#include <functional>
#include <diskwrangler/fs/dfs/dfsvolume.h>
#include <diskwrangler/xfer/retrofiles.h>

using namespace Adfs;


// ===================================================================== DropTree

DropTree::DropTree(QWidget *parent) : dw::ui::VolumePane(parent) {
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
}

QStringList DropTree::mimeTypes() const { return QStringList() << DW_MIME_ITEMS; }

// Our own drag, rather than QAbstractItemView's, so that we know whether the drop was accepted: on Windows the files
// may already have been written while the drag passed over another program, and a drag that then comes back and is
// let go of here (or anywhere it isn't accepted) must not report "dragged out".
void DropTree::startDrag(Qt::DropActions) {
    QList<QTreeWidgetItem *> items = selectedItems();
    QMimeData *data = makeMime(items);
    if (!data) return;
    auto *drag = new QDrag(this);
    drag->setMimeData(data);
    if (QTreeWidgetItem *first = currentItem() && currentItem()->isSelected() ? currentItem() : items.value(0)) {
        QRect r = visualItemRect(first).intersected(viewport()->rect());
        if (r.isValid()) {                       // a picture of the row being dragged, as Qt's own drag shows
            r.setWidth(qMin(r.width(), columnWidth(0)));
            drag->setPixmap(viewport()->grab(r));
            drag->setHotSpot(viewport()->mapFromGlobal(QCursor::pos()) - r.topLeft());
        }
    }
    // Copy only; reports dragOutFinished or dragOutNothing. Don't delete the drag: on X11 the drop target fetches the
    // files after exec() returns, and Qt deletes the drag itself when that is over (QAbstractItemView doesn't delete
    // its drags either). It is a child of this tree, so it goes at the latest when the window closes.
    execDragOut(drag);
}

QVector<DragRef> DropTree::refsFor(const QList<QTreeWidgetItem *> &items) const {
    QVector<DragRef> refs;
    for (QTreeWidgetItem *it : items) {
        if (it->data(0, RoleIsRoot).toBool()) continue;
        bool covered = false;                     // a child of a selected directory is copied with it
        for (QTreeWidgetItem *p = it->parent(); p; p = p->parent())
            if (!p->data(0, RoleIsRoot).toBool() && items.contains(p)) { covered = true; break; }
        if (covered) continue;
        DragRef r;
        r.parent = it->data(0, RoleParent).toUInt();      // a DFS row: its side
        r.name = it->data(0, RoleName).toString();
        r.isDir = it->data(0, RoleIsDir).toBool();
        const dw::ui::PaneRow row = rowOf(it);          // where the kit knows the row
        r.mount = row.mount;
        r.path = row.path;
        refs << r;
    }
    return refs;
}

QMimeData *DropTree::makeMime(const QList<QTreeWidgetItem *> &items) const {
    QVector<DragRef> refs = refsFor(items);
    if (refs.isEmpty()) return nullptr;
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << quint64(sourceWindow()) << qint32(sourcePane()) << qint32(refs.size());
    for (const DragRef &r : refs) ds << quint32(r.parent) << r.name << r.isDir;
    // the files themselves, for the host or another program: exported from the window when (if) they're asked for
    QPointer<MainWindow> win = MainWindow::byId(sourceWindow());
    int pane = sourcePane();
    dw::DragOutMime::Exporter exporter = [win, pane, refs](QVector<dw::TransferItem> *out, QString *err) {
        if (!win) { *err = "the disc's window was closed during the drag"; return false; }
        return win->exportRefs(pane, refs, out, err);
    };
    auto *m = new dw::DragOutMime(exporter, RetroFiles::dragSpec(win && win->writeInfOnExtract(),
                                  win ? QFileInfo(win->document()->path()).fileName() : QString(), pane));
    m->setData(DW_MIME_ITEMS, payload);
    m->setData(DW_MIME_SOURCE, sourceTag());
    return m;
}

// What a tree takes (the kit sorts the drop): items from another tree, or within the DFS tree from one side to the
// other; retro-files JSON and files, but an empty window wants a drop's files (it opens them: an image dragged out of
// another window).
dw::ui::VolumePane::DropRules DropTree::dropRules() const {
    DropRules r;
    MainWindow *w = MainWindow::byId(sourceWindow());
    r.fromItself = w && (((sourcePane() == PaneDfs0 || sourcePane() == PaneDfs1) && w->document()->dfs(0).attached() &&
                          w->document()->dfs(1).attached()) ||
                         (sourcePane() == PaneTape && w->paneEditable(PaneTape)));    // rows moved within a tape
    r.filesNotJson = w && !w->document()->isOpen();
    return r;
}

void DropTree::dragMoveEvent(QDragMoveEvent *e) {
    if (!acceptable(e->mimeData())) return;
    e->acceptProposedAction();
    QTreeWidgetItem *it = itemAt(dropPos(e));     // highlight where the drop will land
    if (it && it != currentItem()) setCurrentItem(it);
}


// ===================================================================== helpers



static QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QChar('0')).toUpper(); }

// ================================================================== MainWindow

// Every main window, oldest first (the kit's list of its windows, the ones that are this program's).
static QList<MainWindow *> allWindows() {
    QList<MainWindow *> l;
    for (dw::ui::DiscWindow *w : dw::ui::DiscWindow::all())
        if (auto *m = qobject_cast<MainWindow *>(w)) l << m;
    return l;
}

MainWindow *MainWindow::byId(quint64 id) { return qobject_cast<MainWindow *>(DiscWindow::byId(id)); }

MainWindow::~MainWindow() = default;

dw::app::AppInfo MainWindow::appInfo() const {
    dw::app::AppInfo a;
    a.name = "Acorn Disk Wrangler";
    a.id = "acorndiskwrangler";
    a.version = QApplication::applicationVersion();
    a.organisation = "acorndiskwrangler";
    a.releases = dw::fs::acornDiskWrangler().releases;
    a.handsTo = {dw::fs::cpmDiskWrangler()};
    return a;
}

// The standard actions' texts, keys and button-bar texts, exactly as this window has always had them.
dw::ui::DiscWindow::ActionSpec MainWindow::actionSpec(Act a) const {
    auto key = [](QKeySequence::StandardKey k) { return QList<QKeySequence>{QKeySequence(k)}; };
    switch (a) {
    case Act::NewWindow: return {"New &window", {QKeySequence("Ctrl+Shift+N")}, QString(), QString()};
    case Act::NewImage: return {"&New disc image...", key(QKeySequence::New), QString(), QString()};
    case Act::Open: return {"&Open...", key(QKeySequence::Open), QString(), QString()};
    case Act::Save: return {"&Save", key(QKeySequence::Save), QString(), QString()};
    case Act::SaveAs: return {"Save &as...", {QKeySequence("Ctrl+Shift+S")}, QString(), QString()};
    case Act::Revert: return {"&Revert", {}, QString(), QString()};
    case Act::Close: return {"&Close", key(QKeySequence::Close), QString(), QString()};
    case Act::Quit: return {"&Quit", key(QKeySequence::Quit), QString(), QString()};
    case Act::View: return {"&View (text / hex / BASIC)", {QKeySequence("F3")}, "View", QString()};
    case Act::Extract: return {"E&xtract to folder...", {QKeySequence("Ctrl+E")}, "Extract", QString()};
    case Act::AddFiles: return {"&Add files...", {QKeySequence("Ctrl+Shift+F")}, "Add files", QString()};
    case Act::AddFolder: return {"Add f&older...", {QKeySequence("Ctrl+Shift+D")}, "Add folder", QString()};
    case Act::Copy: return {"&Copy", QKeySequence::keyBindings(QKeySequence::Copy), QString(), QString()};     // the platform's own keys
    case Act::Paste: return {"&Paste", QKeySequence::keyBindings(QKeySequence::Paste), QString(), QString()};
    case Act::Rename: return {"Re&name...", {QKeySequence("F2")}, "Rename", QString()};
    case Act::Properties: return {"P&roperties / attributes...", {QKeySequence("Alt+Return")}, "Properties", QString()};
    case Act::Delete: return {"&Delete", dw::deleteKeys(), "Delete", QString()};
    // where it works, said wherever the action shows (button, menu, status bar): DFS discs have no directories
    case Act::NewDirectory: return {"New &directory...", {QKeySequence("F7")}, "New dir",
                                    "New directory - on ADFS and DOS (FAT) discs; DFS discs have no directories"};
    }
    return {};
}

// The ADFS pane is made inside the window and the others on their own, as they always were: the order of the window's
// keyboard focus (which pane has it when the window opens) follows it.
dw::ui::VolumePane *MainWindow::createPane(const QString &group) {
    auto *t = new DropTree(group == "ADFS" ? this : nullptr);
    t->setDragSource(id(), group == "ADFS" ? PaneAdfs : group == "DOS" ? PaneDos : group == "Tape" ? PaneTape : PaneDfs0);
    return t;
}

// Column layouts. Shared columns use the same sample text in every pane so ADFS, DFS and DOS windows line up; DOS
// keeps the ADFS order: numbers, then the date. ADFS and DOS are rebuilt from nothing at every refresh, as they always
// have been (the root open, nothing selected); the DFS pane keeps its selection, current row and closed sides.
dw::ui::PaneLayout MainWindow::paneLayout(const QString &group) const {
    const dw::ui::PaneColumn name{"Name", "", false}, attr{"Attr", "DLWRE", false},
        load{"Load", "FFFFFFFF", true}, exec{"Exec", "FFFFFFFF", true}, length{"Length", "999999999", true},
        sector{"Sector", "9999999", true}, typeDate{"Type / date", "FFF   2026-12-31 23:59", false},
        size{"Size", "999999999", true}, cluster{"Cluster", "9999999", true},
        modified{"Modified", "FFF   2026-12-31 23:59", false},
        blocks{"Blocks", "9999", true}, problem{"Problem", "block 99 header CRC", false};
    dw::ui::PaneLayout l;
    l.expand = dw::ui::PaneLayout::ExpandRoots;
    if (group == "ADFS") l.columns = {name, attr, load, exec, length, sector, typeDate};
    else if (group == "DOS") l.columns = {name, attr, size, cluster, modified};
    else if (group == "Tape") l.columns = {name, attr, load, exec, length, blocks, problem};   // in tape order, never sorted
    else l.columns = {name, attr, load, exec, length, sector};       // both sides of a DFS disc, one root row each
    if (group == "DFS") {
        l.keepCurrent = true;
    } else {
        l.keepSelection = false;
        l.rootsAlwaysOpen = true;
    }
    return l;
}

QString MainWindow::paneTitle(const QString &group) const {
    if (group == "ADFS") return m_img.adfsLocked() ? "ADFS (read-only)" : "ADFS";
    if (group == "DFS") return "DFS";
    if (group == "Tape") return "Tape";
    switch (m_doc.info().kind) {
    case DiskInfo::AdfsDosHybrid: return "DOS Plus (640K hybrid)";
    case DiskInfo::DosAcorn800: return "DOS Plus (800K)";
    case DiskInfo::DosPc: return "PC FAT";
    default: return "DOS Plus (DRIVE_C)";
    }
}

bool MainWindow::paneShown(const QString &group) const {
    if (group == "ADFS") return !m_doc.isOpen() || (m_img.isOpen() && m_img.isAdfs());   // (never beside a tape)
    if (group == "DOS") return m_doc.dosMount() >= 0;
    if (group == "Tape") return m_doc.tapeMount() >= 0;
    return m_doc.dfsMount(0) >= 0 || m_doc.dfsMount(1) >= 0;
}

QList<QAction *> MainWindow::contextActions() const {      // the Edit menu's order, as CP/M Disk Wrangler's
    return {m_actView, m_actExtract, nullptr, m_actAddFiles, m_actAddFolder, m_actNewDir, nullptr, m_actCopy, m_actPaste,
            nullptr, m_actRename, m_actProps, m_actDelete};
}

MainWindow::MainWindow(QWidget *parent) : DiscWindow(parent) {
    resize(980, 640);
    setWindowTitle("Acorn Disk Wrangler");
    m_doc.setWindowRules(true);
    setupKit();                                 // the panes (ADFS, DOS, DFS as tabs) and the standard actions
    m_tree = static_cast<DropTree *>(pane("ADFS"));
    m_dosTree = static_cast<DropTree *>(pane("DOS"));
    m_dfsTree = static_cast<DropTree *>(pane("DFS"));
    m_tapeTree = static_cast<DropTree *>(pane("Tape"));
    m_tabs = tabWidget();
    m_tabs->tabBar()->hide();                   // no tab until a disc is open (the ADFS pane stays, to take drops)

    m_status = new QLabel(this);                 // as CP/M Disk Wrangler: the disc's state, left-aligned, all the width
    statusBar()->addWidget(m_status, 1);

    // Actions. The menus, the context menu and the button bar keep the same order as CP/M Disk Wrangler's, with this
    // program's extra items (Add folder, New directory, Boot option, Side order) in the same places.
    auto mk = [this](const QString &text, const QKeySequence &key = QKeySequence()) {
        QAction *a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        return a;
    };
    m_actNewWin = action(Act::NewWindow);
    m_actNewImage = action(Act::NewImage);
    m_actOpen = action(Act::Open);
    m_actSave = action(Act::Save);
    m_actSaveAs = action(Act::SaveAs);
    m_actRevert = action(Act::Revert);
    m_actClose = action(Act::Close);
    m_actQuit = action(Act::Quit);
    m_actView = action(Act::View);
    m_actExtract = action(Act::Extract);
    m_actAddFiles = action(Act::AddFiles);
    m_actAddFolder = action(Act::AddFolder);
    m_actNewDir = action(Act::NewDirectory);
    m_actCopy = action(Act::Copy);
    m_actPaste = action(Act::Paste);
    m_actRename = action(Act::Rename);
    m_actProps = action(Act::Properties);
    m_actDelete = action(Act::Delete);
    m_actNewDir->setStatusTip(m_actNewDir->toolTip());
    m_actRenameVol = mk("Rename disc / &volume...");
    m_actBootOpt = mk("&Boot option...");
    m_actCheck = mk("&Check disc", QKeySequence("Ctrl+K"));
    m_actReadIl = mk("Read as &interleaved (sides alternate track by track)");
    m_actReadSeq = mk("Read as &sequential (all of side 0, then side 1)");
    m_actConvertSides = mk("&Convert to sequential...");
    for (QAction *a : {m_actReadIl, m_actReadSeq}) a->setCheckable(true);
    auto *sideGroup = new QActionGroup(this);
    sideGroup->addAction(m_actReadIl);
    sideGroup->addAction(m_actReadSeq);
    m_optReadInf = mk("Read .inf sidecar files when adding");
    m_optWriteInf = mk("Write .inf sidecar files when extracting");
    m_optBackup = mk("Keep a .bak copy when saving");
    m_actReadAtom = mk("Read as Acorn &Atom DOS (DFS discs)");
    m_actReadAtom->setCheckable(true);
    m_actReadAtom->setStatusTip("Read this DFS disc as the Acorn Atom's DOS keeps it (default directory a space, 16-bit "
                                "addresses, no boot option); nothing is written");
    m_actReadAtom->setToolTip(m_actReadAtom->statusTip());
    m_optAtomInPc = mk("Read AtomInPC headers when adding to an Atom disc");
    m_optAtomInPc->setStatusTip("On: a file added to an Atom disc starts with AtomInPC's 16-byte header (load, exec, "
                                "length), which is taken off; .atm files are always read by their header");
    m_optLetterDirs = mk("DFS directories become ADFS directories");
    m_optLetterDirs->setStatusTip("On: a DFS file A.NAME copied to ADFS goes into subdirectory A; off: flattened to NAME");
    for (QAction *a : {m_optReadInf, m_optAtomInPc, m_optWriteInf, m_optBackup, m_optLetterDirs}) a->setCheckable(true);
    AppSettings st;
    m_optReadInf->setChecked(st.value("readInf", true).toBool());
    m_optAtomInPc->setChecked(st.value("atomInPc", false).toBool());
    m_optWriteInf->setChecked(st.value("writeInf", true).toBool());
    m_optBackup->setChecked(st.value("backup", true).toBool());
    m_optLetterDirs->setChecked(st.value("dfsLetterDirs", true).toBool());

    QMenu *mf = menuBar()->addMenu("&File");
    mf->addActions({m_actNewWin, m_actNewImage, m_actOpen});
    mf->addSeparator();
    mf->addActions({m_actSave, m_actSaveAs, m_actRevert});
    mf->addSeparator();
    AppSettings::addMenuItem(mf, this);           // where portable settings exist (not a Linux install)
    if (AppSettings::offered()) mf->addSeparator();
    mf->addActions({m_actClose, m_actQuit});
    QMenu *me = menuBar()->addMenu("&Edit");
    me->addActions({m_actView, m_actExtract});
    me->addSeparator();
    me->addActions({m_actAddFiles, m_actAddFolder, m_actNewDir});
    me->addSeparator();
    me->addActions({m_actCopy, m_actPaste});
    me->addSeparator();
    me->addActions({m_actRename, m_actProps, m_actDelete});
    me->addSeparator();
    me->addActions({m_actRenameVol, m_actBootOpt});
    QMenu *mt = menuBar()->addMenu("&Tools");
    mt->addAction(m_actCheck);
    QMenu *ms = mt->addMenu("&Side order (double-sided images)");
    ms->addActions({m_actReadIl, m_actReadSeq});
    ms->addSeparator();
    ms->addAction(m_actConvertSides);
    mt->addAction(m_actReadAtom);
    mt->addSeparator();
    mt->addActions({m_optReadInf, m_optAtomInPc, m_optWriteInf, m_optBackup, m_optLetterDirs});
    QMenu *mh = menuBar()->addMenu("&Help");
    mh->addAction("&About Acorn Disk Wrangler...", this, &MainWindow::about);
    mh->addAction("About &Qt...", qApp, &QApplication::aboutQt);
    mh->addAction("&Licences...", this, [this] { showLicences(this); });

    // Open / Save live in the File menu only; the button bar is for working on the disc contents.
    ButtonBar::makeButtonBar(addToolBar("Main"), {m_actView, m_actExtract, m_actAddFiles, m_actAddFolder, m_actNewDir,
                                                  m_actRename, m_actProps, m_actDelete});

    connect(m_actCheck, &QAction::triggered, this, &MainWindow::checkImage);
    connect(m_actBootOpt, &QAction::triggered, this, &MainWindow::bootOptionDialog);
    connect(m_actReadIl, &QAction::triggered, this, [this] { readWithSideOrder(false); });
    connect(m_actReadSeq, &QAction::triggered, this, [this] { readWithSideOrder(true); });
    connect(m_actReadAtom, &QAction::triggered, this, [this](bool on) { readAsAtom(on); });
    connect(m_actConvertSides, &QAction::triggered, this, [this] {
        if (m_doc.convertPending()) {                     // the action reads "Cancel ..." while one is staged
            QString e;
            m_doc.setConvertSides(m_img.sideLayout().sequential, &e);
            updateUi();
        } else {
            convertSideOrder(!m_img.sideLayout().sequential);
        }
    });
    connect(m_actRenameVol, &QAction::triggered, this, &MainWindow::renameVolume);
    for (DropTree *t : {m_tree, m_dosTree, m_dfsTree, m_tapeTree}) {
        connect(t, &QTreeWidget::currentItemChanged, this, &MainWindow::updateUi);   // which DFS side is in use
        // items dragged in from another disc/window (resolved now, handled after the drop returns)
        connect(t, &DropTree::internalDrop, this, [this, t](const QByteArray &payload, QTreeWidgetItem *it) {
            if (t == m_tapeTree && tapeRowsFromHere(payload)) {     // rows moved within the tape: before the row dropped on
                QStringList names;
                QDataStream ds(payload);
                quint64 win = 0;
                qint32 srcPane = 0, n = 0;
                ds >> win >> srcPane >> n;
                for (int i = 0; i < n && ds.status() == QDataStream::Ok; ++i) {
                    quint32 par = 0;
                    QString nm;
                    bool isDir = false;
                    ds >> par >> nm >> isDir;
                    names << nm;
                }
                const QString before = it && !it->data(0, RoleIsRoot).toBool() ? it->data(0, RoleName).toString() : QString();
                QTimer::singleShot(0, this, [this, names, before] { moveTapeRows(names, before); });
                return;
            }
            int pane = dropPane(t, it);
            quint32 dir = dropTarget(pane, it);
            QTimer::singleShot(0, this, [this, pane, dir, payload] { onInternalDrop(pane, dir, payload); });
        });
        connect(t, &DropTree::retroFilesDropped, this, [this, t](const QByteArray &json, QTreeWidgetItem *it) {
            int pane = dropPane(t, it);
            quint32 dir = dropTarget(pane, it);
            QTimer::singleShot(0, this, [this, pane, dir, json] { onRetroFilesDrop(pane, dir, json); });
        });
        connect(t, &DropTree::dragOutFinished, this, [this](int files, int dirs, const QStringList &errors) {
            if (!errors.isEmpty())
                QMessageBox::warning(this, "Drag", "Some items could not be written for the drag:\n\n" + errors.join("\n"));
            else
                statusBar()->showMessage(QString("Dragged out %1 file(s)%2.").arg(files)
                                             .arg(dirs ? QString(" in %1 folder(s)").arg(dirs) : QString()), 6000);
        });
        connect(t, &DropTree::dragOutNothing, this, [this] {
            statusBar()->showMessage("Nothing was dragged out: the drop wasn't taken, or no file was asked for.", 6000);
        });
    }
    for (QAction *a : {m_optReadInf, m_optAtomInPc, m_optWriteInf, m_optBackup, m_optLetterDirs})
        connect(a, &QAction::toggled, this, [this] {
            AppSettings s;
            s.setValue("readInf", m_optReadInf->isChecked());
            s.setValue("atomInPc", m_optAtomInPc->isChecked());
            s.setValue("writeInf", m_optWriteInf->isChecked());
            s.setValue("backup", m_optBackup->isChecked());
            s.setValue("dfsLetterDirs", m_optLetterDirs->isChecked());
        });
    updateUi();
}

// ---------------------------------------------------------------- open/save

bool MainWindow::confirmDiscard() {
    if (!m_doc.dirty()) return true;
    auto b = QMessageBox::question(this, "Unsaved changes",
                                   "The image has unsaved changes. Save them first?",
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                   QMessageBox::Save);
    if (b == QMessageBox::Cancel) return false;
    if (b == QMessageBox::Save) return save();
    return true;
}

void MainWindow::openDialog() {
    AppSettings s;
    QString p = QFileDialog::getOpenFileName(
        this, "Open disc image", s.value("lastDir").toString(),
        "Disc and tape images (" + dw::anyCase("*.dat *.img *.ima *.dsk *.hdf *.adf *.adl *.adm *.ads *.ssd *.dsd *.bin *.imd *.td0 *.uef") +
            ");;Tape images (" + dw::anyCase("*.uef") + ");;All files (*)");
    if (p.isEmpty()) return;
    openInWindow(p);                            // each disc lives in its own window, so they can be dragged between
}

// Each disc in its own window (Open..., New disc image, dropped images): a new one, closed again if the disc didn't open.
dw::ui::DiscWindow *MainWindow::createWindow() { return makeWindow<MainWindow>(); }

// ---------------------------------------------------------------- opening, on the kit's open pipeline (openFile)
// confirmDiscard (beforeOpen) -> detectImage -> the kit asks when two readings are equally likely, and hands a CP/M
// disc to CP/M Disk Wrangler (handOverMessage: this program's words) -> openDocument -> afterOpen (the notices).

// Readings asked for before the file is opened (openNextAs): absolute path -> fsId.
static QHash<QString, QString> &pendingReadings() {
    static QHash<QString, QString> h;
    return h;
}

void MainWindow::openNextAs(const QString &path, const QString &fsId) {
    pendingReadings()[QFileInfo(path).absoluteFilePath()] = fsId;
}

bool MainWindow::detectImage(const QString &path, int force, dw::fs::Verdict *verdict) {
    m_unknown = DiskInfo();
    *verdict = detectCandidates(path, force, &m_unknown);     // never refuses: a file nothing reads is opened as ADFS
    // the reading asked for (Tools > Read as Acorn Atom DOS, a new Atom disc): it holds for this file in this window
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (pendingReadings().contains(abs)) { m_readAs = pendingReadings().take(abs); m_readAsPath = abs; }
    else if (abs != m_readAsPath) { m_readAs.clear(); m_readAsPath.clear(); }
    if (!m_readAs.isEmpty()) {
        for (int i = 0; i < verdict->ranked.size(); ++i) {
            if (verdict->ranked[i].fsId != m_readAs || verdict->ranked[i].foreign()) continue;
            verdict->ranked.move(i, 0);
            verdict->best = 0;
            verdict->rival = -1;
            verdict->action = dw::fs::Verdict::Open;
            break;
        }
    }
    return true;
}

dw::ui::DiscWindow::NotOpened MainWindow::handOverMessage(const QString &, const dw::fs::Verdict &, const dw::fs::Candidate &pick) const {
    // CP/M discs are CP/M Disk Wrangler's: the kit starts it with the image, or shows this (with where to get it)
    const DiskInfo info = diskInfo(pick);
    const QString why = info.kind == DiskInfo::AcornCpm
        ? "Its DFS catalogue holds only $.CPMDISC, a locked file that keeps DFS off the CP/M files."
        : "Its files are CP/M files, kept in Torch's own filesystem.";
    NotOpened m;
    m.title = "A CP/M disc";
    m.startedTitle = "A CP/M disc";
    m.text = info.description + "\n\n" + why + " Open it with " + pick.owner.name + " (github.com/drdpj/CPMDiskWrangler).";
    return m;
}

bool MainWindow::openDocument(const QString &path, const dw::fs::Verdict &, const dw::fs::Candidate *pick, int force) {
    const DiskInfo info = pick ? diskInfo(*pick) : m_unknown;
    // the contents can't always tell (e.g. a blank disc): then detection uses the format's usual order
    QString sidesHow = force >= 0 ? "set by hand" : (info.sides.doubleSided() && !info.sidesConfirmed ? "assumed" : "");
    if (info.kind == DiskInfo::AdfsNewMap) {
        QMessageBox::information(this, "Not supported",
                                 info.description + "\n\nADFS D/E (new map) discs are not supported yet.");
        return false;
    }
    if (info.kind == DiskInfo::ForeignTape) {
        QMessageBox::information(this, "Not an Acorn tape",
                                 "This is " + info.description + ".\n\nAcorn Disk Wrangler reads BBC Micro and Electron "
                                 "tapes (UEF files); no program of the family reads this kind of tape yet.");
        return false;
    }
    QString err;
    if (!m_doc.open(path, info, &err)) {        // the document: the image and its mounts (an unknown disc as ADFS)
        QMessageBox::critical(this, "Cannot open image", err);
        refresh(); updateUi();
        return false;
    }
    m_opened = info;
    m_sidesHow = info.sides.doubleSided() ? sidesHow : QString();
    AppSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    refresh();
    if (m_dos.attached()) {
        Report dr = m_dos.check();
        m_doc.setDosOk(dr.ok());
        if (!dr.ok())
            showReport("DOS volume has errors",
                       "The DOS filesystem has structural errors, so it can be browsed but not changed.", dr, true);
    }
    for (int s = 0; s < 2; ++s)
        if (m_doc.dfs(s).attached()) {
            Report dr = m_doc.dfs(s).check();
            m_doc.setDfsOk(s, dr.ok());
            if (!dr.ok())
                showReport(QString("DFS drive %1 has errors").arg(s * 2),
                           "This DFS catalogue has errors, so the drive can be browsed but not changed.", dr, true);
        }
    m_tabs->setCurrentIndex(0);
    updateUi();
    return true;
}

// The read-only notices and the status line once a disc is open (no refresh: openDocument has made the rows).
void MainWindow::afterOpen(const QString &path) {
    const DiskInfo &info = m_opened;
    QString pad;
    if (m_img.needsPadding())
        pad = QString("  NOTE: the image file is %1 bytes shorter than a full disc; it will be padded with zeros when saved.")
                  .arg(m_img.fullSize() - m_img.fileSize());
    const Report &r = m_img.openReport();
    if (m_doc.isTape()) {
        const Acorn::Cfs::Tape &t = m_doc.cfs()->tapeRead();
        int bad = 0;
        for (const Acorn::Cfs::Item &i : t.items) if (i.type == Acorn::Cfs::Item::FileItem && !i.file.problems.isEmpty()) ++bad;
        const QString ro = m_doc.readOnlyWhy(m_doc.tapeMount());
        statusBar()->showMessage("Opened " + path + " - " + info.description +
                                     (bad ? QString(", %1 with problems - see Tools > Check.").arg(bad) : QString(".")) +
                                     (ro.isEmpty() ? QString("  Files dropped on it are added at the end; drag them to change the order.")
                                                   : "  Read-only: " + ro + "."), 15000);
        return;
    }
    if (m_img.isAdfs() && !r.ok()) {
        showReport("Image failed validation",
                   "This image's free space map / directory structure is damaged, so it has been opened "
                   "READ-ONLY. Nothing can be added or changed until it has been repaired.", r, true);
    } else if (!m_img.editable()) {
        QMessageBox::information(this, "Read-only", m_img.containerKind() == dw::device::Kind::Td0
                                     ? "TeleDisk images can only be read, so it has been opened read-only. Files can be copied from it."
                                     : "The image file is not writable, so it has been opened read-only.");
    } else if (m_img.isAdfs() && !r.warnings.isEmpty() && info.kind != DiskInfo::AdfsDosHybrid) {
        statusBar()->showMessage("Opened with warnings - see Tools > Check." + pad, 15000);
    } else {
        QString sides;
        if (m_sidesHow == "assumed")
            sides = "  The side order can't be told from the contents, so the usual order (interleaved) is used - Tools > Side order to change.";
        statusBar()->showMessage("Opened " + path + " - " + (info.description.isEmpty() ? QString("ADFS") : info.description) + "." + pad + sides, 15000);
    }
}

// ---------------------------------------------------------------- side order

bool MainWindow::readWithSideOrder(bool sequential) {
    if (!m_img.isOpen() || !m_img.sideLayout().doubleSided() || !m_img.isRawFile()) return false;
    if (m_img.sideLayout().sequential == sequential && !m_img.convertPending()) { updateUi(); return true; }
    bool ok = openFile(m_img.path(), sequential ? 1 : 0);   // read again, the order given (nothing is written)
    updateUi();
    return ok;
}

bool MainWindow::atomDisc() const {
    return m_doc.isOpen() && !m_doc.isTape() && m_doc.dfs(0).attached() && m_doc.dfs(0).atom();
}

bool MainWindow::readAsAtom(bool atom) {
    if (!m_doc.isOpen() || m_doc.isTape() || !m_doc.dfs(0).attached()) { updateUi(); return false; }
    if (atomDisc() == atom) { updateUi(); return true; }
    const dw::device::SideOrder &sl = m_img.sideLayout();
    const int force = sl.doubleSided() && m_img.isRawFile() ? (sl.sequential ? 1 : 0) : -1;   // the side order kept
    const QString path = m_img.path();
    openNextAs(path, atom ? "atom" : "dfs");
    const bool ok = openFile(path, force);              // read again (nothing is written; unsaved changes are asked about)
    pendingReadings().remove(QFileInfo(path).absoluteFilePath());   // not taken when the open was cancelled
    updateUi();
    return ok && atomDisc() == atom;
}

bool MainWindow::convertSideOrder(bool toSequential, bool confirm) {
    if (!m_img.isOpen() || !m_img.sideLayout().doubleSided() || !m_img.isRawFile()) return false;
    if (confirm) {
        QString to = toSequential ? "sequentially (all of side 0, then all of side 1)"
                                  : "interleaved (each cylinder's side 0 track, then its side 1 track)";
        if (QMessageBox::question(this, "Convert side order",
                                  "Rewrite the image file with its two sides stored " + to + "?\n\n"
                                  "The disc's contents don't change, only the order of the tracks in the file. "
                                  "Use this when an emulator or tool expects the other order.\n\n"
                                  "It is done when you save; Revert cancels it.") != QMessageBox::Yes)
            return false;
    }
    QString err;
    if (!m_doc.setConvertSides(toSequential, &err)) {
        QMessageBox::warning(this, "Convert side order", err);
        return false;
    }
    updateUi();
    return true;
}

bool MainWindow::writeInfOnExtract() const { return m_optWriteInf->isChecked(); }

// ---------------------------------------------------------------- boot option

int MainWindow::bootOption() const {
    if (!m_img.isOpen() || dosActive()) return -1;
    int ds = dfsSide();
    if (ds >= 0) return m_doc.dfs(ds).attached() ? m_doc.dfs(ds).bootOption() : -1;
    return m_img.isAdfs() ? m_img.bootOption() : -1;
}

// The disc's boot option on the kit's pattern for a disc's own attributes (5f): the program's dialog says the value
// (volumeEditor), the kit checks the mount is writable, changes it in one transaction and refreshes.
bool MainWindow::setBootOption(int opt) {
    QString err;
    const int ds = dfsSide();
    if (dosActive() || !m_img.isOpen()) err = "DOS discs have no boot option";
    else if (ds >= 0 && !dfsEditable(ds)) err = "this DFS drive is read-only";
    const int mount = ds >= 0 ? m_doc.dfsMount(ds) : m_doc.adfsMount();
    bool ok = err.isEmpty() && applyVolumeAttrs(mount, {{"boot", opt}}, &err);
    if (!ok) message("Boot option", err);
    refreshChanged({mount});
    return ok;
}

void MainWindow::bootOptionDialog() {
    if (bootOption() < 0) return;
    const int ds = dfsSide();
    editVolume(ds >= 0 ? m_doc.dfsMount(ds) : m_doc.adfsMount(), "boot");
}

// The dialogs of the disc's own attributes: Rename disc / volume (title, label) and the boot option. They give only
// the value; applyVolumeAttrs does the change.
bool MainWindow::volumeEditor(int mount, const QString &key, VolumeEdit *edit) {
    const int pane = paneOf(mount);
    const bool dfs = pane == PaneDfs0 || pane == PaneDfs1;
    const int side = pane == PaneDfs1 ? 1 : 0;
    QString err;
    bool ok = false;
    if (key == "boot") {
        const int cur = dfs ? (m_doc.dfs(side).attached() ? m_doc.dfs(side).bootOption() : -1) : (m_img.isAdfs() ? m_img.bootOption() : -1);
        if (cur < 0) return false;
        QStringList items;
        for (int i = 0; i < 4; ++i) items << Adfs::bootOptionName(i);
        if (cur > 3) items << Adfs::bootOptionName(cur);           // keep an odd value visible rather than hide it
        QString which = dfs ? QString("DFS drive %1").arg(side * 2) : QString("this disc");
        QString pick = QInputDialog::getItem(this, "Boot option",
                                             "What SHIFT+BREAK does with " + which + " (*OPT 4):",
                                             items, cur > 3 ? 4 : cur, false, &ok);
        if (!ok) return false;
        const int opt = items.indexOf(pick);
        if (opt == cur || opt > 3) return false;
        edit->values["boot"] = opt;
        edit->title = "Boot option";
        return true;
    }
    if (pane == PaneDos) {
        QString n = QInputDialog::getText(this, "Rename volume", "DOS volume label (max 11 characters, blank to remove):",
                                          QLineEdit::Normal, m_dos.label(), &ok);
        if (!ok) return false;
        edit->values["label"] = n;
        edit->title = "Rename volume";
    } else if (dfs) {
        QString n = QInputDialog::getText(this, m_doc.dfs(side).atom() ? "Rename Atom disc" : "Rename DFS disc",
                                          "Disc title (max 12 characters):", QLineEdit::Normal,
                                          m_doc.dfs(side).title(), &ok);
        if (!ok) return false;
        edit->values["title"] = n.trimmed();
        edit->title = "Rename disc";
    } else {
        Dir root;
        if (!m_img.readDir(RootSector, &root, &err)) { QMessageBox::warning(this, "Rename volume", err); return false; }
        QString n = QInputDialog::getText(this, "Rename disc", "Disc name (root directory title, max 10 characters):",
                                          QLineEdit::Normal, root.title(), &ok);
        if (!ok) return false;
        edit->values["title"] = n.trimmed();
        edit->title = "Rename disc";
    }
    return true;
}

// Rename disc (ADFS, DFS) or Rename volume (DOS): the pane in use's root.
void MainWindow::renameVolume() {
    const int ds = dfsSide();
    editVolume(ds >= 0 ? m_doc.dfsMount(ds) : dosActive() ? m_doc.dosMount() : m_doc.adfsMount(), dosActive() && ds < 0 ? "label" : "title");
}

// ---------------------------------------------------------------- save as
// A floppy can be saved as ImageDisk, standard or extended DSK, or a raw image (deliberate change 8), through the same
// code as acorndw convert (Adfs::OpenedDisc::writeCopy): the copy is checked, then opened in this window. What the copy
// is comes from the filter chosen, never from the name typed (as in CP/M Disk Wrangler), so the dialog starts on the
// image's own kind. A copy of the image's own kind is made as it always was; a hard disc (and a disc nothing
// recognised) is saved only as a copy of its own kind, in the dialog it always had.
void MainWindow::saveAsDialog() {
    if (m_doc.isTape()) {                                // UEF, compressed or not (the filter chosen), with the changes, checked
        QFileInfo fi(m_doc.path());
        const QString gz = "UEF tape, compressed (*.uef)", plain = "UEF tape, uncompressed (*.uef)";
        QString filter = m_doc.tape().gzip() ? gz : plain;
        const QString p = QFileDialog::getSaveFileName(this, "Save as", fi.absolutePath() + "/" + fi.completeBaseName() + "-copy." + fi.suffix(),
                                                       gz + ";;" + plain, &filter);
        if (p.isEmpty()) return;
        const QString abs = QFileInfo(p).absoluteFilePath();
        for (MainWindow *w : allWindows())
            if (w != this && w->m_doc.isOpen() && QFileInfo(w->m_doc.path()).absoluteFilePath() == abs) {
                QMessageBox::warning(this, "Save as", "That file is open in another window."); return; }
        QString err;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = m_doc.saveTapeAs(p, filter == gz, &err);
        QApplication::restoreOverrideCursor();
        if (!ok) { QMessageBox::warning(this, "Save as", "Not saved: " + err); return; }
        refresh(); updateUi();
        return;
    }
    if (!m_img.isOpen()) return;
    using dw::device::Kind;
    QFileInfo fi(m_img.path());
    const Kind own = m_img.containerKind();
    Kind kind = own;
    QString p;
    if (!m_doc.convertible()) {
        p = QFileDialog::getSaveFileName(this, "Save as", fi.absolutePath() + "/" + fi.completeBaseName() + "-copy." + fi.suffix(),
                                         QString("Disc image (*.%1);;All files (*)").arg(fi.suffix()));
    } else {
        const QString imd = "ImageDisk (*.imd)";
        const QString edsk = "Extended DSK image (*.dsk)";
        const QString dsk = "Standard DSK image (*.dsk)";
        // the image's own suffix first: the dialog gives a name the first suffix of the filter chosen
        QStringList rawSuffixes = {"*.adf", "*.adl", "*.adm", "*.ads", "*.ssd", "*.dsd", "*.img", "*.ima", "*.dsk", "*.bin"};
        if (own == Kind::Raw && !fi.suffix().isEmpty()) {
            rawSuffixes.removeAll("*." + fi.suffix().toLower());
            rawSuffixes.prepend("*." + fi.suffix());
        }
        const QString raw = "Raw image (" + rawSuffixes.join(' ') + ")";
        QString filter = own == Kind::Raw ? raw : own == Kind::Dsk ? dsk : own == Kind::Edsk ? edsk : imd;
        const QString name = own == Kind::Td0 ? fi.completeBaseName() + ".imd"                // TeleDisk: a writable kind
                                              : fi.completeBaseName() + "-copy." + fi.suffix();
        p = QFileDialog::getSaveFileName(this, "Save as", fi.absolutePath() + "/" + name,
                                         QStringList{imd, edsk, dsk, raw}.join(";;"), &filter);
        kind = filter == imd ? Kind::Imd : filter == edsk ? Kind::Edsk : filter == dsk ? Kind::Dsk : Kind::Raw;
    }
    if (p.isEmpty()) return;
    QString abs = QFileInfo(p).absoluteFilePath();
    if (abs == fi.absoluteFilePath()) {
        if (kind == own) { save(); return; }
        QMessageBox::warning(this, "Save as", "That is the image's own file. Choose another name for the " + dw::device::kindName(kind) + ".");
        return;
    }
    for (MainWindow *w : allWindows())
        if (w != this && w->m_doc.isOpen() && QFileInfo(w->m_doc.path()).absoluteFilePath() == abs) {
            QMessageBox::warning(this, "Save as", "That file is open in another window."); return; }
    QString err;
    if (kind != own) {                                   // another kind: written with what is staged, checked, then opened
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = m_doc.writeCopy(p, kind, QString(), true, &err);
        QApplication::restoreOverrideCursor();
        if (!ok) { QMessageBox::warning(this, "Save as", "Not saved: " + err); return; }
        m_doc.revert();                                  // the changes are in the copy, which this window now shows
        if (!m_readAs.isEmpty()) openNextAs(p, m_readAs);  // read as this disc was (an Atom disc)
        openFile(p);
        return;
    }
    if (!m_img.dirty() || !m_img.editable()) {          // nothing to write, or opened read-only: copy, then open the copy
        if (!QFile::exists(p) || QFile::remove(p)) QFile::copy(m_img.path(), p);
        QFile(p).setPermissions(QFile(p).permissions() | QFile::WriteOwner);
        if (!m_readAs.isEmpty()) openNextAs(p, m_readAs);
        openFile(p);
        return;
    }
    if (!m_img.moveToCopy(p, &err)) { QMessageBox::warning(this, "Save as", err); return; }
    if (!m_readAs.isEmpty()) m_readAsPath = abs;         // the window now shows the copy, read as before
    save();                                              // writes the staged changes into the copy
    updateUi();                                          // title now shows the new name
}

// ---------------------------------------------------------------- new image

void MainWindow::newImageDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("New disc image");
    auto *form = new QFormLayout;
    auto *type = new QComboBox;
    for (const NewDisc::TypeInfo &t : NewDisc::types()) type->addItem(t.name, int(t.type));
    AppSettings st;
    type->setCurrentIndex(qBound(0, st.value("newType", 2).toInt(), type->count() - 1));
    auto *title = new QLineEdit;
    auto *titleLabel = new QLabel;
    auto *il = new QRadioButton("Interleaved: each cylinder's side 0 track, then its side 1 track (usual)");
    auto *seq = new QRadioButton("Sequential: all of side 0, then all of side 1");
    il->setChecked(true);
    auto *sideBox = new QWidget;
    auto *sv = new QVBoxLayout(sideBox);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->addWidget(il);
    sv->addWidget(seq);
    auto *note = new QLabel;
    note->setWordWrap(false);         // explicit line breaks: wrapped labels in a form layout get clipped
    // DOS Plus 640K: where to take DOSBOOT (Acorn's boot code) from
    auto *bootBox = new QWidget;
    auto *bh = new QHBoxLayout(bootBox);
    bh->setContentsMargins(0, 0, 0, 0);
    auto *boot = new QLineEdit(st.value("dosbootSource").toString());
    auto *browse = new QPushButton("Browse...");
    bh->addWidget(boot, 1);
    bh->addWidget(browse);
    auto *bootLabel = new QLabel("Boot code from:");
    connect(browse, &QPushButton::clicked, &dlg, [&] {
        QString p = QFileDialog::getOpenFileName(&dlg, "DOSBOOT from a DOS Plus 640K disc image, or a DOSBOOT file",
                                                 boot->text().isEmpty() ? st.value("lastDir").toString() : boot->text(),
                                                 "DOS Plus 640K disc or DOSBOOT (" + dw::anyCase("*.adl *.adf *.img *.dsk dosboot*") + ");;All files (*)");
        if (!p.isEmpty()) boot->setText(p);
    });
    form->addRow("Format:", type);
    form->addRow(titleLabel, title);
    form->addRow("Side order:", sideBox);
    form->addRow(bootLabel, bootBox);
    form->addRow(note);
    auto upd = [&] {
        const NewDisc::TypeInfo &t = NewDisc::info(NewDisc::Type(type->currentData().toInt()));
        titleLabel->setText(QString(t.titleWhat) + ":");
        title->setMaxLength(t.titleMax);
        const bool tape = t.type == NewDisc::Uef;           // a tape has no title and no sides
        titleLabel->setVisible(!tape);
        title->setVisible(!tape);
        sideBox->setEnabled(t.doubleSided);
        bool hyb = t.type == NewDisc::Acorn640;
        bootLabel->setVisible(hyb);
        bootBox->setVisible(hyb);
        note->setText(hyb ? "Set up like Acorn's DOS Plus 640K discs, with no DOS files.\n"
                            "DOSBOOT is Acorn's boot code, so it is copied from a disc you have:\n"
                            "a DOS Plus 640K image (its boot track is copied exactly) or a DOSBOOT file."
                      : tape ? "An empty cassette tape. Files added to it are saved as the BBC Micro saves them,\n"
                               "one after another; their order on the tape can be changed by dragging."
                             : QString());
        note->setVisible(!note->text().isEmpty());
    };
    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, upd);
    upd();
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *top = new QVBoxLayout(&dlg);
    top->setSizeConstraint(QLayout::SetFixedSize);     // grow and shrink with the (wrapped) note
    top->addLayout(form);
    top->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    st.setValue("newType", type->currentIndex());
    if (!boot->text().isEmpty()) st.setValue("dosbootSource", boot->text());

    const NewDisc::TypeInfo &t = NewDisc::info(NewDisc::Type(type->currentData().toInt()));
    QString ext = t.suffix;
    // suggest a file name from the title: "Games Disc" -> "Games Disc.adl"
    const bool tape = t.type == NewDisc::Uef;
    QString base = tape || title->text().trimmed().isEmpty() ? QString("untitled") : dw::hostSafeName(title->text().trimmed());
    QString path = QFileDialog::getSaveFileName(this, tape ? "Create tape image" : "Create disc image",
                                                st.value("lastDir").toString() + "/" + base + "." + ext,
                                                QString("%1 (*.%2);;All files (*)").arg(tape ? "Tape image" : "Disc image", ext));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += "." + ext;
    QString abs = QFileInfo(path).absoluteFilePath();
    for (MainWindow *w : allWindows())
        if (w->m_doc.isOpen() && QFileInfo(w->m_doc.path()).absoluteFilePath() == abs) {
            QMessageBox::warning(this, "New disc image", "That file is open in another window. Close it first, or choose another name.");
            return;
        }
    bool sequential = t.doubleSided && seq->isChecked();
    QString err;
    if (!NewDisc::create(path, t.type, tape ? QString() : title->text(), sequential, &err, boot->text())) {
        QMessageBox::critical(this, "New disc image", "Could not create the image:\n" + err);
        return;
    }
    st.setValue("lastDir", QFileInfo(path).absolutePath());
    if (NewDisc::isAtom(t.type)) openNextAs(path, "atom");   // a blank Atom disc looks like a blank DFS disc
    openInWindow(path);                          // in this window if it is empty, else in a new one
}

bool MainWindow::save() {
    if (!m_doc.isOpen() || !(m_doc.dirty() || m_img.needsPadding())) return true;
    QString err;
    bool converting = m_doc.convertPending();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_doc.save(&err, m_optBackup->isChecked());   // the document's checked save (the volumes it touches)
    QApplication::restoreOverrideCursor();
    if (converting && !m_doc.convertPending()) m_sidesHow.clear();   // the file's side order changed (the document has it)
    if (!ok) {
        QMessageBox::critical(this, "Save failed", err);
        refresh(); updateUi();
        return false;
    }
    refresh(); updateUi();
    statusBar()->showMessage(m_doc.isTape() ? "Saved. The tape was read back: the same files."
                                            : "Saved. Free space map and directories re-checked: OK.", 6000);
    return true;
}

void MainWindow::revert() {
    if (!m_doc.dirty()) return;
    if (QMessageBox::question(this, "Revert", "Discard all unsaved changes?") != QMessageBox::Yes) return;
    m_doc.revert();
    refresh(); updateUi();
}

void MainWindow::showReport(const QString &title, const QString &headline, const Report &r, bool warn) {
    QMessageBox box(warn ? QMessageBox::Warning : QMessageBox::Information, title, headline, QMessageBox::Ok, this);
    QStringList lines;
    for (const QString &s : r.errors) lines << "ERROR:   " + s;
    for (const QString &s : r.warnings) lines << "WARNING: " + s;
    for (const QString &s : r.info) lines << s;
    box.setDetailedText(lines.join("\n"));
    box.exec();
}

void MainWindow::about() {
    QMessageBox box(this);
    box.setWindowTitle("About Acorn Disk Wrangler");
    box.setIconPixmap(windowIcon().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText("<h3>Acorn Disk Wrangler</h3>"
                "<p>Version " + QApplication::applicationVersion().toHtmlEscaped() + "</p>"
                "<p>View and edit Acorn disc images: ADFS, DFS, DOS Plus and PC FAT.</p>"
                "<p>Copyright &copy; 2026 Daniel Jameson</p>");
    box.setInformativeText("This program is free software: you can redistribute it and/or modify it under the "
                           "terms of the GNU General Public License as published by the Free Software "
                           "Foundation, either version 3 of the License, or (at your option) any later version."
                           "<p>This program is distributed in the hope that it will be useful, but WITHOUT ANY "
                           "WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A "
                           "PARTICULAR PURPOSE. See the "
                           "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU General Public License</a> "
                           "for more details.</p>"
                           "<p>Uses Qt under the LGPL v3. Shares its drag-and-drop format (retro-files) with CP/M Disk "
                           "Wrangler. Help &gt; Licences shows this program's licence, its third-party notices and the licences of "
                           "the libraries distributed with it.</p><p>" + AppSettings::where().toHtmlEscaped() + "</p>");
    box.exec();
}

// Check disc: the kit's (Disc::validateAll, each mount's own report beside it); the words are this program's, as they
// always were: the ADFS report as it is, the DOS lines headed "DOS: ", each DFS drive's "DFS drive 0: " (or 2), and
// the warning for an image that is shorter than a full disc.
void MainWindow::checkImage() {
    if (!m_doc.isOpen()) return;
    checkDisc();
}

void MainWindow::showCheck(const dw::fs::Report &, const QVector<dw::fs::Report> &perMount) {
    Report r;
    auto of = [&](int pane) { const int m = mountOf(pane); return m >= 0 && m < perMount.size() ? perMount[m] : Report(); };
    if (m_doc.isTape()) {
        r = of(PaneTape);
        showReport("Tape check", r.warnings.isEmpty() ? "Every file on the tape was read with good CRCs."
                                                      : "Some files on the tape have problems - see details. They can't be copied whole.",
                   r, !r.warnings.isEmpty());
        return;
    }
    if (m_doc.adfsMount() >= 0) r = of(PaneAdfs);
    if (m_dos.attached()) {
        const Report dr = of(PaneDos);
        for (const QString &s : dr.errors) r.errors << "DOS: " + s;
        for (const QString &s : dr.warnings) r.warnings << "DOS: " + s;
        r.info += dr.info;
    }
    for (int s = 0; s < 2; ++s)
        if (m_doc.dfs(s).attached()) {
            const Report dr = of(s ? PaneDfs1 : PaneDfs0);
            for (const QString &e : dr.errors) r.errors << QString("DFS drive %1: %2").arg(s * 2).arg(e);
            for (const QString &e : dr.warnings) r.warnings << QString("DFS drive %1: %2").arg(s * 2).arg(e);
            r.info += dr.info;
        }
    if (m_img.needsPadding())
        r.warnings << QString("the image file is %1 bytes shorter than a full disc; it will be padded with zeros when saved")
                          .arg(m_img.fullSize() - m_img.fileSize());
    showReport("Disc check",
               r.ok() ? (r.warnings.isEmpty() ? "The free space map and directory tree are consistent."
                                              : "No errors, but there are warnings.")
                      : "PROBLEMS FOUND - see details.",
               r, !r.ok() || !r.warnings.isEmpty());
}

// -------------------------------------------------------------------- tree

// Every pane: an ADFS disc's DOS Plus partition is followed to wherever $.DOS.DRIVE_C is now (as the window always
// has), then the kit shows or hides the tabs (paneShown, paneTitle) and makes the rows (rootLabel, decorateRow). With
// no disc the tab bar is hidden: the empty ADFS pane stays, as the place an image is dropped to open it.
void MainWindow::refresh() {
    m_doc.followDriveC();
    clearRowCache();
    DiscWindow::refresh();
    m_tabs->tabBar()->setVisible(m_doc.isOpen());
}


// After a change (Delete): an ADFS change can move or remove $.DOS.DRIVE_C, so every pane is made again; a DOS or DFS
// change only its own pane, the others left as they are (their selection and current row too), as always; a tape its own.
void MainWindow::refreshChanged(const QVector<int> &mounts) {
    bool all = mounts.isEmpty();
    for (int m : mounts) all = all || paneOf(m) == PaneAdfs || paneOf(m) < 0;
    if (all) { refresh(); return; }
    bool dos = false, dfs = false, tape = false;
    for (int m : mounts) (paneOf(m) == PaneDos ? dos : paneOf(m) == PaneTape ? tape : dfs) = true;
    clearRowCache();
    if (dos) m_dosTree->refresh();
    if (dfs) m_dfsTree->refresh();
    if (tape) m_tapeTree->refresh();
    updateUi();
}


void MainWindow::refreshPane(DropTree *t) {
    clearRowCache();
    t->refresh();
    updateUi();
}

void MainWindow::clearRowCache() const {
    m_rowDir.clear();
    m_rowNext.clear();
    m_adfsDirs.clear();
    m_dosDirs.clear();
    m_seenAdfs.clear();
    m_seenDos.clear();
}

int MainWindow::paneOf(int mount) const {
    if (mount < 0) return -1;
    if (mount == m_doc.adfsMount()) return PaneAdfs;
    if (mount == m_doc.dosMount()) return PaneDos;
    if (mount == m_doc.dfsMount(1)) return PaneDfs1;
    if (mount == m_doc.dfsMount(0)) return PaneDfs0;
    if (mount == m_doc.tapeMount()) return PaneTape;
    return -1;
}

int MainWindow::mountOf(int pane) const {
    switch (pane) {
    case PaneAdfs: return m_doc.adfsMount();
    case PaneDos: return m_doc.dosMount();
    case PaneDfs1: return m_doc.dfsMount(1);
    case PaneTape: return m_doc.tapeMount();
    default: return m_doc.dfsMount(0);
    }
}

QString MainWindow::rootLabel(int mount) const {
    switch (paneOf(mount)) {
    case PaneAdfs: {
        Dir root;
        QString err;
        return m_img.readDir(RootSector, &root, &err) ? dw::rootLabel("$", root.title())
                                                      : dw::rootLabel("$", "unreadable root directory");
    }
    case PaneDos: return dw::rootLabel("C:\\", m_dos.label());
    case PaneDfs1: return dw::rootLabel("Drive 2", m_doc.dfs(1).title());
    case PaneDfs0: return dw::rootLabel("Drive 0", m_doc.dfs(0).title());
    case PaneTape: return dw::rootLabel("Tape", QFileInfo(m_doc.path()).fileName());
    }
    return QString();
}

// A row's cells, icon and this program's own roles (RoleParent ...: the tests and the drag code read them), from
// Acorn's own classes: the ADFS entry (its sector, load and exec, date stamp), the FAT entry (cluster, attributes,
// date) or the DFS catalogue entry (start sector). The kit makes a directory's rows in the volume's order, so the
// n-th row under a directory is the n-th entry of that directory as read here.
void MainWindow::decorateRow(QTreeWidgetItem *it, int mount, const dw::fs::Path &path, const dw::fs::Entry *entry) const {
    const int pane = paneOf(mount);
    QString err;
    if (!entry) {                                // a root row
        it->setData(0, RoleName, pane == PaneDos ? "\\" : "$");
        it->setData(0, RoleIsDir, true);
        it->setData(0, RoleIsRoot, true);
        if (pane == PaneAdfs) {
            it->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
            it->setData(0, RoleParent, 0);
            it->setData(0, RoleSector, RootSector);
            m_rowDir.insert(rowKey(mount, path), RootSector);
            m_seenAdfs.insert(RootSector);
        } else if (pane == PaneDos) {
            it->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
            it->setData(0, RoleParent, 0);
            it->setData(0, RoleSector, 0);
            m_rowDir.insert(rowKey(mount, path), 0);
        } else if (pane == PaneTape) {
            it->setIcon(0, style()->standardIcon(QStyle::SP_DriveFDIcon));
            it->setData(0, RoleParent, 0);
            it->setData(0, RoleSector, 0);
        } else {
            const int side = pane == PaneDfs1 ? 1 : 0;
            it->setIcon(0, style()->standardIcon(QStyle::SP_DriveFDIcon));
            it->setData(0, RoleParent, side);
            it->setData(0, RoleSector, 0);
            it->setData(0, RoleSide, side);
        }
        return;
    }
    const QString parentKey = rowKey(mount, path.parent());
    const int pos = m_rowNext[parentKey]++;
    if (pane == PaneTape) {                      // the entry says it all: its label, attributes and any problem
        const bool file = entry->attrs.contains("load");
        it->setText(0, entry->shown());
        it->setText(1, entry->attrs.value("protected").toBool() ? "P" : "");
        if (file) {
            dw::setNumCell(it, 2, hex8(entry->attrs.value("load").toUInt()));
            dw::setNumCell(it, 3, hex8(entry->attrs.value("exec").toUInt()));
            dw::setNumCell(it, 5, QString::number(entry->attrs.value("blocks").toInt()));
        }
        dw::setNumCell(it, 4, QString::number(entry->size));
        it->setToolTip(4, QString("%1 bytes (&%2)").arg(entry->size).arg(entry->size, 0, 16));
        if (file) {
            it->setText(6, entry->problem);
        } else {
            it->setText(6, "can't be read as files");
            for (int c = 0; c < it->columnCount(); ++c) it->setForeground(c, Qt::gray);
        }
        if (!entry->problem.isEmpty() && file) it->setForeground(6, Qt::red);
        if (entry->attrs.value("protected").toBool()) it->setToolTip(1, "Protected: *RUN only (*LOAD and CHAIN say \"Locked\")");
        it->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        it->setData(0, RoleParent, 0);
        it->setData(0, RoleName, entry->name);   // the unique name: what a drag or a copy names it by
        it->setData(0, RoleIsDir, false);
        it->setData(0, RoleSector, pos);
        it->setData(0, RoleIsRoot, false);
        return;
    }
    if (pane == PaneAdfs) {
        const quint32 dirSector = m_rowDir.value(parentKey, RootSector);
        if (!m_adfsDirs.contains(dirSector)) {
            Dir d;
            m_img.readDir(dirSector, &d, &err);
            m_adfsDirs.insert(dirSector, d);
        }
        const Dir &d = m_adfsDirs[dirSector];
        if (pos >= d.entries.size()) return;
        const Entry &e = d.entries[pos];
        QString nm = e.name();
        it->setText(0, nm.isEmpty() ? "<bad name>" : nm);
        it->setText(1, e.attrString());
        dw::setNumCell(it, 5, QString::number(e.sin));
        it->setData(0, RoleParent, dirSector);
        it->setData(0, RoleName, nm);
        it->setData(0, RoleIsDir, e.isDir());
        it->setData(0, RoleSector, e.sin);
        it->setData(0, RoleIsRoot, false);
        if (e.isDir()) {
            it->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            m_rowDir.insert(rowKey(mount, path), e.sin);
        } else {
            it->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
            dw::setNumCell(it, 2, hex8(e.load));
            dw::setNumCell(it, 3, hex8(e.exec));
            dw::setNumCell(it, 4, QString::number(e.length));
            it->setToolTip(4, QString("%1 bytes (&%2), %3 sectors").arg(e.length).arg(e.length, 0, 16).arg(sectorsFor(e.length)));
            if (e.hasStamp()) {
                QDateTime t = e.stamp();
                if (t.date().year() >= 1981 && t.date().year() <= 2100)   // else it's just an address like FFFF1900
                    it->setText(6, QString("%1   %2").arg(e.fileType(), 3, 16, QChar('0')).toUpper().arg(t.toString("yyyy-MM-dd hh:mm")));
            }
        }
    } else if (pane == PaneDos) {
        const quint32 dirCluster = m_rowDir.value(parentKey, 0);
        if (!m_dosDirs.contains(dirCluster)) {
            QVector<dw::fat::DEntry> v, l;
            m_dos.listDir(dirCluster, &v, &err);
            for (const dw::fat::DEntry &e : v) if (e.name != "." && e.name != "..") l << e;   // as the volume lists them
            m_dosDirs.insert(dirCluster, l);
        }
        const QVector<dw::fat::DEntry> &l = m_dosDirs[dirCluster];
        if (pos >= l.size()) return;
        const dw::fat::DEntry &e = l[pos];
        it->setText(0, e.name);
        it->setText(1, e.attrString());
        dw::setNumCell(it, 3, QString::number(e.cluster));
        QDateTime w = e.when();
        if (w.isValid()) it->setText(4, w.toString("yyyy-MM-dd hh:mm"));
        it->setData(0, RoleParent, dirCluster);
        it->setData(0, RoleName, e.name);
        it->setData(0, RoleIsDir, e.isDir());
        it->setData(0, RoleSector, e.cluster);
        it->setData(0, RoleIsRoot, false);
        if (e.isDir()) {
            it->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            m_rowDir.insert(rowKey(mount, path), e.cluster);
        } else {
            it->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
            dw::setNumCell(it, 2, QString::number(e.size));
        }
    } else {
        const int side = pane == PaneDfs1 ? 1 : 0;
        QChar dir;
        QString name;
        dw::dfs::splitName(entry->name, &dir, &name);
        const int i = m_doc.dfs(side).indexOf(dir, name);
        if (i < 0) return;
        const dw::dfs::File f = m_doc.dfs(side).files()[i];
        const bool atom = m_doc.dfs(side).atom();
        // an Atom disc as its *CAT shows it: its default directory's files by name alone, 16-bit addresses
        it->setText(0, atom && f.dir == '$' && !f.atomDollar ? f.name : f.fullName());
        it->setText(1, f.locked ? "L" : "");
        auto addr = [atom](quint32 v) { return atom ? QString("%1").arg(v, 4, 16, QChar('0')).toUpper() : dw::dfs::addrText(v); };
        dw::setNumCell(it, 2, addr(f.load));
        dw::setNumCell(it, 3, addr(f.exec));
        dw::setNumCell(it, 4, QString::number(f.length));
        dw::setNumCell(it, 5, QString::number(f.start));
        it->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        it->setData(0, RoleParent, side);           // a drag's DragRef::parent: the side
        it->setData(0, RoleName, f.fullName());
        it->setData(0, RoleIsDir, false);
        it->setData(0, RoleSector, f.start);
        it->setData(0, RoleIsRoot, false);
        it->setData(0, RoleSide, side);
    }
}

// A directory that can't be read: red, with the reason as its tooltip
void MainWindow::listFailed(QTreeWidgetItem *row, int, const dw::fs::Path &, const QString &err) const {
    row->setToolTip(0, err);
    row->setForeground(0, Qt::red);
}

// A DFS catalogue is shown by directory letter, then name (ignoring case); ADFS and FAT in their own order
void MainWindow::orderRows(int mount, const dw::fs::Path &, QVector<dw::fs::Entry> *entries) const {
    const int pane = paneOf(mount);
    if (pane != PaneDfs0 && pane != PaneDfs1) return;
    std::stable_sort(entries->begin(), entries->end(), [](const dw::fs::Entry &a, const dw::fs::Entry &b) {
        QChar da, db;
        QString na, nb;
        dw::dfs::splitName(a.name, &da, &na);
        dw::dfs::splitName(b.name, &db, &nb);
        if (da.toUpper() != db.toUpper()) return da.toUpper() < db.toUpper();
        return na.toUpper() < nb.toUpper();
    });
}

// As the panes have always been made: ADFS to 64 levels, DOS to 32, a directory already shown (a loop on a damaged
// disc) left empty
bool MainWindow::listChildren(int mount, const dw::fs::Path &dir, int depth) const {
    const int pane = paneOf(mount);
    const quint32 at = m_rowDir.value(rowKey(mount, dir), 0);
    if (pane == PaneAdfs) {
        if (depth > 64 || m_seenAdfs.contains(at)) return false;
        m_seenAdfs.insert(at);
    } else if (pane == PaneDos) {
        if (depth > 32 || (at && m_seenDos.contains(at))) return false;
        if (at) m_seenDos.insert(at);
    }
    return true;
}

QVector<MainWindow::Sel> MainWindow::selectedObjects() const {
    QVector<Sel> v;
    for (QTreeWidgetItem *it : m_tree->selectedItems()) {
        if (it->data(0, RoleIsRoot).toBool()) continue;
        v.append({it->data(0, RoleParent).toUInt(), it->data(0, RoleName).toString(),
                  it->data(0, RoleIsDir).toBool(), it->data(0, RoleSector).toUInt(), it});
    }
    return v;
}

quint32 MainWindow::targetDirForItem(QTreeWidgetItem *it) const {
    if (!it) return RootSector;
    if (it->data(0, RoleIsDir).toBool()) return it->data(0, RoleSector).toUInt();
    return it->data(0, RoleParent).toUInt();
}

QString MainWindow::title() const {                // as CP/M Disk Wrangler: "<file> * - <program>"
    QString n = m_doc.isOpen() ? QFileInfo(m_doc.path()).fileName() : QString("no disc");
    bool changed = m_doc.isOpen() && (m_doc.dirty() || m_img.needsPadding());
    return n + (changed ? " *" : "") + " - Acorn Disk Wrangler";
}

// The pane in use's reason (the document's words): the DOS or DFS volume shown, else the image and the ADFS side
QString MainWindow::readOnlyReason() const {
    if (m_doc.isTape()) return m_doc.readOnlyWhy(m_doc.tapeMount());
    if (!m_img.isOpen()) return "no disc";
    int ds = dfsSide();
    if (dosActive()) return m_doc.readOnlyWhy(m_doc.dosMount());
    if (ds >= 0) return m_doc.readOnlyWhy(m_doc.dfsMount(ds));
    return m_doc.readOnlyWhy(-1);
}

void MainWindow::updateActions() {
    const bool tape = tapeActive();
    bool has = m_doc.isOpen(), ed = has && (tape ? paneEditable(PaneTape) : m_img.editable());
    int ds = dfsSide();
    bool dosPane = dosActive();
    bool canEdit = tape ? paneEditable(PaneTape) : dosPane ? dosEditable() : (ds >= 0 ? dfsEditable(ds) : (ed && m_img.isAdfs() && !m_img.adfsLocked()));
    QTreeWidget *ct = currentTree();
    bool changed = m_doc.dirty() || m_img.needsPadding();
    m_actSave->setEnabled(ed && changed);
    m_actRevert->setEnabled(ed && m_doc.dirty());
    m_actAddFiles->setEnabled(canEdit);
    m_actAddFolder->setEnabled(canEdit && !tape);    // a tape has no folders
    m_actNewDir->setEnabled(canEdit && ds < 0 && !tape);      // DFS and tapes have no directories
    m_actCheck->setEnabled(has);
    m_actExtract->setEnabled(has);
    bool sel = tape ? !m_tapeTree->selectedItems().isEmpty()
                    : dosPane ? !dosSelected().isEmpty() : (ds >= 0 ? !dfsSelected().isEmpty() : !selectedObjects().isEmpty());
    m_actCopy->setEnabled(has && sel);
    m_actPaste->setEnabled(canEdit && clipboardUsable());
    m_actDelete->setEnabled(canEdit && sel);
    m_actProps->setEnabled(canEdit && sel);
    m_actView->setEnabled(has && sel);
    m_actRename->setEnabled(canEdit && !ct->selectedItems().isEmpty());
    m_actRenameVol->setEnabled(canEdit && !tape);
    m_actBootOpt->setEnabled(canEdit && !dosPane && !tape && !(ds >= 0 && m_doc.dfs(ds).atom()));   // the Atom has none
    const bool dfsDisc = has && !tape && m_doc.dfs(0).attached();
    // a disc with a file in the Atom's default directory (a space) has no DFS reading to go back to
    bool marked = false;
    for (int k = 0; k < 2; ++k)
        if (atomDisc() && m_doc.dfs(k).attached())
            for (const dw::dfs::File &f : m_doc.dfs(k).files()) marked = marked || (f.dir == '$' && !f.atomDollar);
    m_actReadAtom->setEnabled(dfsDisc && (atomDisc() ? !marked : !m_doc.dfs(0).watford()));
    m_actReadAtom->setChecked(atomDisc());
    const dw::device::SideOrder &sl = m_img.sideLayout();
    bool twoSides = has && sl.doubleSided() && m_img.isRawFile();     // ImageDisk, DSK: each track says where it is
    m_actReadIl->setEnabled(twoSides);
    m_actReadSeq->setEnabled(twoSides);
    m_actReadIl->setChecked(twoSides && !sl.sequential);
    m_actReadSeq->setChecked(twoSides && sl.sequential);
    m_actConvertSides->setEnabled(twoSides && ed);
    m_actConvertSides->setText(m_img.convertPending() ? "&Cancel the pending side order change"
                                                       : (sl.sequential ? "&Convert to interleaved..." : "&Convert to sequential..."));
}

// The same shape as CP/M Disk Wrangler's: what the disc is and its space, then anything unusual, " - " between.
QStringList MainWindow::statusVariants() const {
    if (!m_doc.isOpen()) return {"Open a disc image (File > Open), or drop one here."};
    if (m_doc.isTape()) {
        const Acorn::Cfs::Tape &t = m_doc.cfs()->tapeRead();
        int unread = 0;
        for (const Acorn::Cfs::Item &i : t.items) if (i.type == Acorn::Cfs::Item::Unrecognised) ++unread;
        QString s = QString("%1 - %2 file%3, %4 blocks").arg(m_doc.tape().kind() == dw::tape::Kind::Uef ? m_doc.info().description : QString("Tape"))
                        .arg(t.files()).arg(t.files() == 1 ? "" : "s").arg(t.blocks.size());
        if (unread) s += QString("  -  %1 stretch%2 of unrecognised data").arg(unread).arg(unread == 1 ? "" : "es");
        if (!readOnlyReason().isEmpty()) s += "  -  read-only: " + readOnlyReason();
        return {s};
    }
    int ds = dfsSide();
    bool dosPane = dosActive();
    QString s;
    auto files = [](int n) { return QString("%1 file%2").arg(n).arg(n == 1 ? "" : "s"); };
    if (dosPane) {
        s = QString("%1 - %2 free of %3").arg(m_tabs->tabText(m_tabs->currentIndex()),
                                              dw::fmtBytes(quint64(m_dos.freeClusters()) * m_dos.clusterBytes()),
                                              dw::fmtBytes(quint64(m_dos.totalClusters()) * m_dos.clusterBytes()));
    } else if (ds >= 0) {
        int n = m_doc.dfs(1).attached() ? 2 : 1;
        const bool atom = atomDisc();
        const QString what = atom ? "Atom DOS" : "DFS";
        s = n > 1 ? what + " (a disc on each side)" : what;
        for (int k = 0; k < n; ++k)
            s += QString(" - %1%2, %3 free of %4%5").arg(n > 1 ? QString("drive %1: ").arg(k * 2) : QString())
                     .arg((m_doc.dfs(k).watford() ? QString("62-file catalogue, ") : QString()) + files(m_doc.dfs(k).files().size()),
                          fmtSectors(m_doc.dfs(k).freeSectors()), fmtSectors(m_doc.dfs(k).sectors()))
                     .arg(atom ? QString() : ", boot " + Adfs::bootOptionName(m_doc.dfs(k).bootOption()).mid(4));
    } else if (m_img.isAdfs()) {
        s = QString("%1 - %2 free of %3 (largest block %4), boot %5")
                .arg(m_doc.info().description.isEmpty() ? QString("ADFS") : m_doc.info().description,
                     fmtSectors(m_img.freeSectors()), fmtSectors(m_img.totalSectors()), fmtSectors(m_img.largestFreeRun()))
                .arg(Adfs::bootOptionName(m_img.bootOption()).mid(4));
    }
    const dw::device::SideOrder &sl = m_img.sideLayout();
    if (sl.doubleSided() && m_img.isRawFile()) {
        s += QString("  -  sides %1").arg(sl.sequential ? "sequential" : "interleaved");
        if (m_img.convertPending()) s += QString(", %1 when saved").arg(sl.sequential ? "interleaved" : "sequential");
        else if (!m_sidesHow.isEmpty()) s += " (" + m_sidesHow + ")";
    }
    QString ro = readOnlyReason();
    if (!ro.isEmpty()) s += "  -  read-only: " + ro;
    if (m_img.needsPadding()) s += "  -  the image file is short; it will be padded to full size when saved";
    return {s};
}

void MainWindow::presentStatus(const QStringList &variants) { m_status->setText(variants.value(0)); }

QTreeWidget *MainWindow::currentTree() const {
    if (tapeActive()) return m_tapeTree;
    if (dosActive()) return m_dosTree;
    if (dfsSide() >= 0) return m_dfsTree;
    return m_tree;
}

// ------------------------------------------------------------------ importing

// Host files dropped on a pane of an open disc (an empty window opens them: the kit's openDropped, with .inf files
// left out, dropSkipsFile): disc images go to dropImages, the rest is imported where they were dropped.
void MainWindow::droppedOnDisc(dw::ui::VolumePane *pane, QTreeWidgetItem *target, const QStringList &paths) {
    if (pane == m_dosTree) { onDosDropped(paths, target); return; }
    if (pane == m_tapeTree) {                    // disc images open in their own windows; other files go on the end of the tape
        if (!dropImages(PaneTape, paths, [this](const QStringList &r) { tapeImportPaths(r); })) tapeImportPaths(paths);
        return;
    }
    if (pane == m_dfsTree) {
        int s = dfsTargetSide(target);
        if (!dropImages(s ? PaneDfs1 : PaneDfs0, paths, [this, s](const QStringList &r) { dfsImportPaths(s, r); }))
            dfsImportPaths(s, paths);
        return;
    }
    quint32 dir = targetDirForItem(target);
    if (!dropImages(PaneAdfs, paths, [this, dir](const QStringList &r) { importPaths(r, dir); })) importPaths(paths, dir);
}

bool MainWindow::dropSkipsFile(const QString &path) const {
    return QFileInfo(path).isDir() || path.endsWith(".inf", Qt::CaseInsensitive);
}

bool MainWindow::isHardDisc() const {
    return m_img.isOpen() && m_doc.info().kind == DiskInfo::Adfs && m_img.fullSize() > 655360;   // (never a tape)
}

MainWindow *MainWindow::openInNewWindow(const QString &path) {
    MainWindow *w = makeWindow<MainWindow>();
    if (w->openFile(path)) return w;
    w->close();
    return nullptr;
}

// Files dropped from the host on an open disc that are disc images (an empty window opens whatever is dropped on it:
// the kit's openDropped, as in CP/M Disk Wrangler). A window holding a floppy opens disc images in new windows and adds
// the other files as usual (CP/M Disk Wrangler adds them all). A hard disc may be where an image is meant to be kept,
// so it asks whether to open the images or copy them onto the disc as files. Returns true if it took the drop over; the
// work is queued, so no dialog or new window appears inside the drop handler. (Add files never opens images.)
bool MainWindow::dropImages(int pane, const QStringList &paths, std::function<void(const QStringList &)> importRest) {
    QStringList images, kinds;
    for (const QString &p : paths) {
        if (QFileInfo(p).isDir()) continue;
        DiskInfo i = detectDisk(p);
        if (i.kind != DiskInfo::Unknown) { images << p; kinds << i.description; }
    }
    if (images.isEmpty()) return false;
    QTimer::singleShot(0, this, [this, pane, paths, images, kinds, importRest] {
        bool copy = false;
        if (isHardDisc() && paneEditable(pane)) {
            QString text = images.size() == 1
                ? QString("'%1' is a disc image (%2).\n\nOpen it in its own window, or copy it onto this hard disc "
                          "as a file?").arg(QFileInfo(images[0]).fileName(), kinds[0])
                : QString("%1 of the dropped files are disc images.\n\nOpen them in their own windows, or copy them "
                          "onto this hard disc as files?").arg(images.size());
            QMessageBox box(QMessageBox::Question, "Disc image dropped", text, QMessageBox::NoButton, this);
            QPushButton *open = box.addButton(images.size() == 1 ? "Open it" : "Open them", QMessageBox::AcceptRole);
            QPushButton *onto = box.addButton("Copy onto this disc", QMessageBox::ActionRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(open);
            box.exec();
            if (box.clickedButton() == onto) copy = true;
            else if (box.clickedButton() != open) return;
        }
        QStringList rest;
        for (const QString &p : paths)
            if (copy || !images.contains(p)) rest << p;
        if (!rest.isEmpty()) importRest(rest);
        if (copy) return;
        int opened = 0;
        for (const QString &p : images)
            if (openInNewWindow(p)) ++opened;
        if (rest.isEmpty()) statusBar()->showMessage(QString("Opened %1 disc image(s) in new windows.").arg(opened), 10000);
    });
    return true;
}

void MainWindow::addFilesDialog() {
    AppSettings s;
    QStringList f = QFileDialog::getOpenFileNames(this, "Add files to the image", s.value("addDir").toString());
    if (f.isEmpty()) return;
    s.setValue("addDir", QFileInfo(f.first()).absolutePath());
    if (tapeActive()) tapeImportPaths(f);
    else if (dfsSide() >= 0) dfsImportPaths(dfsSide(), f);
    else if (dosActive()) dosImportPaths(f, dosTargetDir(m_dosTree->currentItem()));
    else importPaths(f, targetDirForItem(m_tree->currentItem()));
}

void MainWindow::addFolderDialog() {
    AppSettings s;
    QString d = QFileDialog::getExistingDirectory(this, "Add folder to the image", s.value("addDir").toString());
    if (d.isEmpty()) return;
    s.setValue("addDir", d);
    if (dfsSide() >= 0) dfsImportPaths(dfsSide(), {d});
    else if (dosActive()) dosImportPaths({d}, dosTargetDir(m_dosTree->currentItem()));
    else importPaths({d}, targetDirForItem(m_tree->currentItem()));
}

// Add files, Add folder, host files dropped or pasted on the ADFS pane: read by the core's one host reader with this
// program's .inf rule (acornHostRead, as acorndw put reads them), then the kit's import session (importAll).
void MainWindow::importPaths(const QStringList &paths, quint32 dir) {
    if (!m_doc.isOpen()) {
        if (!paths.isEmpty()) openFile(paths.first());
        return;
    }
    if (!m_img.editable()) {
        QMessageBox::warning(this, "Read-only", "This image is read-only, so nothing can be added.");
        return;
    }
    hostImport(PaneAdfs, dir, paths, m_optReadInf->isChecked());
}

// Files from the computer onto a tape: added at its end, saved as the MOS saves them (the volume's placeIncoming and
// fromRetro: a .inf's name and addresses, its access 04 as the protected flag).
void MainWindow::tapeImportPaths(const QStringList &paths) {
    if (!paneEditable(PaneTape)) {
        QMessageBox::warning(this, "Read-only", "This tape is read-only, so nothing can be added: " + readOnlyReason() + ".");
        return;
    }
    hostImport(PaneTape, 0, paths, m_optReadInf->isChecked());
}

// Rows of the tape dragged within it: moved, in their tape order, to just before the row dropped on (after the last
// row: to the end), in one transaction. The tape's other files keep their own chunks.
void MainWindow::moveTapeRows(const QStringList &names, const QString &before) {
    if (!paneEditable(PaneTape)) { message("Move", "This tape is read-only: " + readOnlyReason() + "."); return; }
    const int mount = m_doc.tapeMount();
    QVector<dw::fs::Path> nodes;
    for (const QString &n : names) nodes << dw::fs::Path({n});
    QString err;
    const bool ok = m_doc.disc().transact([&](QString *e) {
        return m_doc.cfs()->move(nodes, before.isEmpty() ? dw::fs::Path() : dw::fs::Path({before}), e);
    }, &err);
    if (!ok) { message("Move", err); return; }
    refreshChanged({mount});
    updateUi();
}

bool MainWindow::tapeRowsFromHere(const QByteArray &payload) const {
    QDataStream ds(payload);
    quint64 win = 0;
    qint32 srcPane = 0;
    ds >> win >> srcPane;
    return ds.status() == QDataStream::Ok && win == id() && srcPane == PaneTape;
}

// ----------------------------------------------------------- new / delete / props

// The path a row stands for as a target (the window kit's row): a directory or root itself, a file's directory
static dw::fs::Path targetPath(QTreeWidgetItem *it) {
    const dw::ui::PaneRow r = dw::ui::VolumePane::rowOf(it);
    if (!r.valid() || r.root) return dw::fs::Path();
    return r.isFile() ? r.path.parent() : r.path;
}

// New directory, on the window kit's pattern for a change: Volume::mkdir inside one Disc::transact, then refresh
void MainWindow::newDirectory() {
    if (dfsSide() >= 0) { statusBar()->showMessage("DFS discs have no directories.", 4000); return; }
    bool ok = false;
    QString n = QInputDialog::getText(this, "New directory", "Name (max 10 characters):", QLineEdit::Normal, "", &ok);
    if (!ok || n.isEmpty()) return;
    const bool dos = dosActive();
    const int mount = dos ? m_doc.dosMount() : m_doc.adfsMount();
    const dw::fs::Path dir = targetPath((dos ? m_dosTree : m_tree)->currentItem());
    QString err;
    if (!m_doc.disc().transact([&](QString *e) {
            dw::fs::Volume *v = m_doc.disc().volume(mount);
            if (!v) { *e = "no such volume"; return false; }
            return v->mkdir(dir, n.trimmed(), e);
        }, &err)) {
        QMessageBox::warning(this, "Cannot create directory", err);
        return;
    }
    if (dos) refreshPane(m_dosTree); else refresh();
    updateUi();
}

void MainWindow::countTree(quint32 dirSector, int &files, int &dirs, int &locked, int depth) const {
    if (depth > 64) return;
    Dir d;
    QString err;
    if (!m_img.readDir(dirSector, &d, &err)) return;
    for (const Entry &e : d.entries) {
        if (e.flag(Entry::L)) ++locked;
        if (e.isDir()) { ++dirs; countTree(e.sin, files, dirs, locked, depth + 1); }
        else ++files;
    }
}

// Delete, on the window kit's pattern (deleteSelected): what the pane in use has selected - rows inside a selected
// directory go with it - each removed in its own transaction (a directory with what is in it), the failures listed.
QVector<dw::ui::DiscWindow::Item> MainWindow::deleteItems() const {
    QVector<Item> v;
    const int ds = dfsSide();
    QList<QTreeWidgetItem *> rows;
    if (tapeActive()) {
        // last on the tape first: removing a file never renames one before it (the second GAME is "GAME (2)" only
        // while the first is there)
        for (QTreeWidgetItem *it : m_tapeTree->selectedItems()) if (!it->data(0, RoleIsRoot).toBool()) rows << it;
        std::sort(rows.begin(), rows.end(), [](QTreeWidgetItem *a, QTreeWidgetItem *b) {
            const QTreeWidgetItem *pa = a->parent(), *pb = b->parent();
            const int ia = pa ? pa->indexOfChild(a) : a->treeWidget()->indexOfTopLevelItem(a);
            const int ib = pb ? pb->indexOfChild(b) : b->treeWidget()->indexOfTopLevelItem(b);
            return ia > ib;
        });
    } else if (ds >= 0) {
        for (const FSel &s : dfsSelected()) rows << s.item;
    } else {
        const bool dos = dosActive();
        QList<QTreeWidgetItem *> sel;
        if (dos) for (const DSel &s : dosSelected()) sel << s.item;
        else for (const Sel &s : selectedObjects()) sel << s.item;
        for (QTreeWidgetItem *it : sel) {           // skip items whose ancestor is also being deleted
            bool covered = false;
            for (QTreeWidgetItem *p = it->parent(); p; p = p->parent())
                if (p->isSelected()) { covered = true; break; }
            if (!covered) rows << it;
        }
    }
    for (QTreeWidgetItem *it : rows) {
        const dw::ui::PaneRow r = dw::ui::VolumePane::rowOf(it);
        if (r.valid() && !r.root) v << Item(r.mount, r.path);
    }
    return v;
}

bool MainWindow::confirmDelete(const QVector<Item> &items) {
    QString msg;
    if (tapeActive()) {
        int prot = 0;
        for (const Item &i : items) {
            dw::fs::Entry e;
            QString err;
            if (m_doc.cfs()->stat(i.path, &e, &err) && e.attrs.value("protected").toBool()) ++prot;
        }
        msg = QString("Delete %1 file(s) from the tape?").arg(items.size());
        if (prot) msg += QString("\n\n%1 of these are protected.").arg(prot);
    } else if (dfsSide() >= 0) {
        int locked = 0;
        QSet<int> sides;
        for (const Item &i : items) {
            const int side = paneOf(i.mount) == PaneDfs1 ? 1 : 0;
            sides << side;
            QChar d;
            QString n;
            dw::dfs::splitName(i.path.name(), &d, &n);
            int x = m_doc.dfs(side).indexOf(d, n);
            if (x >= 0 && m_doc.dfs(side).files()[x].locked) ++locked;
        }
        const int first = items.isEmpty() || paneOf(items[0].mount) != PaneDfs1 ? 0 : 1;
        QString from = sides.size() > 1 ? QString("both sides") : QString("DFS drive %1").arg(first * 2);
        msg = QString("Delete %1 file(s) from %2?").arg(items.size()).arg(from);
        if (locked) msg += QString("\n\n%1 of these are LOCKED.").arg(locked);
    } else {
        const bool dos = dosActive();
        DropTree *t = dos ? m_dosTree : m_tree;
        int files = 0, dirs = 0, flagged = 0;
        for (const Item &i : items) {
            QTreeWidgetItem *it = t->findRow(i.mount, i.path);
            if (!it) continue;
            const bool isDir = it->data(0, RoleIsDir).toBool();
            const quint32 at = it->data(0, RoleSector).toUInt();
            if (isDir) {
                ++dirs;
                if (dos) dosCount(at, files, dirs, flagged, 0); else countTree(at, files, dirs, flagged, 0);
            } else {
                ++files;
            }
            if (it->text(1).contains(dos ? 'R' : 'L')) ++flagged;
        }
        msg = QString(dos ? "Delete %1 file(s) and %2 director%3 from the DOS partition?" : "Delete %1 file(s) and %2 director%3?")
                  .arg(files).arg(dirs).arg(dirs == 1 ? "y" : "ies");
        if (flagged) msg += QString(dos ? "\n\n%1 of these are marked read-only." : "\n\n%1 of these are LOCKED.").arg(flagged);
    }
    if (autoAnswer) return true;
    return QMessageBox::question(this, "Delete", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

QString MainWindow::deleteItemError(const Item &item, const QString &err) const {
    const int pane = paneOf(item.mount);
    if ((pane == PaneDfs0 || pane == PaneDfs1) && !writable(item.mount))
        return item.path.name() + ": this side is read-only";
    return item.path.name() + ": " + err;
}

// Properties, on the kit's pattern (5f): the selection is the rows of the pane in use, the dialogs are this program's
// (the ADFS one here, the DOS one in mainwindow_dos.cpp) and say what changes; each entry is changed in its own
// transaction and the ones that fail are listed. A DFS drive keeps its own code (dfsProps).
QVector<dw::ui::DiscWindow::Item> MainWindow::propertiesItems() const {
    QVector<Item> v;
    for (QTreeWidgetItem *it : currentTree()->selectedItems()) {
        const dw::ui::PaneRow r = dw::ui::VolumePane::rowOf(it);
        if (r.valid() && !r.root) v << Item(r.mount, r.path);
    }
    return v;
}

bool MainWindow::propertiesEditor(const QVector<Item> &items, PropertiesEdit *edit) {
    edit->eachOnItsOwn = true;
    const int pane = paneOf(items[0].mount);
    if (pane == PaneTape) return tapeProperties(items, edit);
    if (pane == PaneDfs0 || pane == PaneDfs1) return dfsProperties(items, edit);
    return pane == PaneDos ? dosProperties(items, edit) : adfsProperties(items, edit);
}

bool MainWindow::adfsProperties(const QVector<Item> &items, PropertiesEdit *edit) {
    QVector<Entry> ents;
    QString err;
    for (const Item &i : items) {
        const QTreeWidgetItem *row = m_tree->findRow(i.mount, i.path);
        const quint32 parent = row ? row->data(0, RoleParent).toUInt() : 0;
        const QString name = row ? row->data(0, RoleName).toString() : i.path.name();
        Dir d;
        if (!m_img.readDir(parent, &d, &err) || d.indexOf(name) < 0) {
            QMessageBox::warning(this, "Properties", err.isEmpty() ? "Entry not found" : err);
            return false;
        }
        ents.append(d.entries[d.indexOf(name)]);
    }
    bool single = ents.size() == 1;
    bool allDirs = true;
    for (const Entry &e : ents) allDirs = allDirs && e.isDir();

    QDialog dlg(this);
    dlg.setWindowTitle(single ? "Properties - " + ents[0].name() : QString("Properties - %1 items").arg(ents.size()));
    QFormLayout *form = new QFormLayout(&dlg);
    QLineEdit *nameEd = nullptr, *loadEd = nullptr, *execEd = nullptr;
    if (single) {
        nameEd = new QLineEdit(ents[0].name());
        nameEd->setMaxLength(MaxNameLen);
        form->addRow("Name:", nameEd);
    }
    auto mkBox = [&](const QString &label, int idx) {
        auto *cb = new QCheckBox(label);
        int on = 0;
        for (const Entry &e : ents) on += e.flag(idx) ? 1 : 0;
        if (on == 0) cb->setCheckState(Qt::Unchecked);
        else if (on == ents.size()) cb->setCheckState(Qt::Checked);
        else { cb->setTristate(true); cb->setCheckState(Qt::PartiallyChecked); }
        return cb;
    };
    QCheckBox *cbR = mkBox("R  - readable", Entry::R);
    QCheckBox *cbW = mkBox("W  - writable", Entry::W);
    QCheckBox *cbL = mkBox("L  - locked (cannot be deleted/overwritten)", Entry::L);
    QCheckBox *cbE = mkBox("E  - execute only", Entry::E);
    cbE->setEnabled(!allDirs);
    form->addRow("Attributes:", cbR);
    form->addRow("", cbW);
    form->addRow("", cbL);
    form->addRow("", cbE);
    if (single && !ents[0].isDir()) {
        QRegularExpressionValidator *v = new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]{1,8}"), &dlg);
        loadEd = new QLineEdit(hex8(ents[0].load)); loadEd->setValidator(v);
        execEd = new QLineEdit(hex8(ents[0].exec)); execEd->setValidator(v);
        form->addRow("Load address (hex):", loadEd);
        form->addRow("Exec address (hex):", execEd);
        form->addRow("Length:", new QLabel(QString("%1 bytes (&%2)").arg(ents[0].length).arg(ents[0].length, 0, 16)));
    }
    if (single) form->addRow("Start sector:", new QLabel(QString::number(ents[0].sin)));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;

    for (int i = 0; i < ents.size(); ++i) {
        const Entry &e = ents[i];
        PropertyChange c;
        c.item = items[i];
        // every attribute is given (a box left partly ticked keeps the entry's own), so the entry is written as it always was
        auto flag = [&](QCheckBox *cb, int idx) { return cb->checkState() == Qt::PartiallyChecked ? e.flag(idx) : cb->checkState() == Qt::Checked; };
        c.attrs["read"] = flag(cbR, Entry::R);
        c.attrs["write"] = flag(cbW, Entry::W);
        c.attrs["locked"] = flag(cbL, Entry::L);
        c.attrs["execute"] = e.isDir() ? e.flag(Entry::E) : flag(cbE, Entry::E);
        if (loadEd) c.attrs["load"] = loadEd->text().toUInt(nullptr, 16);
        if (execEd) c.attrs["exec"] = execEd->text().toUInt(nullptr, 16);
        if (nameEd && nameEd->text().trimmed() != e.name()) c.to = items[i].path.parent().child(nameEd->text().trimmed());
        edit->changes << c;
    }
    return true;
}

// A tape file's properties: its name (one file), the protected flag (*RUN only), load and exec (one file). A file the
// cassette filing system reads with problems, and unrecognised data, are kept as they are (the volume refuses them).
bool MainWindow::tapeProperties(const QVector<Item> &items, PropertiesEdit *edit) {
    QVector<dw::fs::Entry> ents;
    QString err;
    for (const Item &i : items) {
        dw::fs::Entry e;
        if (!m_doc.cfs()->stat(i.path, &e, &err)) { QMessageBox::warning(this, "Properties", err); return false; }
        if (!e.problem.isEmpty()) {
            QMessageBox::warning(this, "Properties", e.shown() + ": " + e.problem + ", so it is kept as it is.");
            return false;
        }
        ents << e;
    }
    const bool single = ents.size() == 1;
    QDialog dlg(this);
    dlg.setWindowTitle(single ? "Properties - " + ents[0].shown() : QString("Properties - %1 files").arg(ents.size()));
    QFormLayout *form = new QFormLayout(&dlg);
    QLineEdit *nameEd = nullptr, *loadEd = nullptr, *execEd = nullptr;
    if (single) {
        nameEd = new QLineEdit(ents[0].shown());
        nameEd->setMaxLength(10);
        form->addRow("Name:", nameEd);
    }
    auto *cbP = new QCheckBox("P  - protected (*RUN only: *LOAD and CHAIN say \"Locked\")");
    int on = 0;
    for (const dw::fs::Entry &e : ents) on += e.attrs.value("protected").toBool() ? 1 : 0;
    if (on == 0) cbP->setCheckState(Qt::Unchecked);
    else if (on == ents.size()) cbP->setCheckState(Qt::Checked);
    else { cbP->setTristate(true); cbP->setCheckState(Qt::PartiallyChecked); }
    form->addRow("Attributes:", cbP);
    if (single) {
        auto *v = new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]{1,8}"), &dlg);
        loadEd = new QLineEdit(hex8(ents[0].attrs.value("load").toUInt())); loadEd->setValidator(v);
        execEd = new QLineEdit(hex8(ents[0].attrs.value("exec").toUInt())); execEd->setValidator(v);
        form->addRow("Load address (hex):", loadEd);
        form->addRow("Exec address (hex):", execEd);
        form->addRow("Length:", new QLabel(QString("%1 bytes (&%2), %3 block%4").arg(ents[0].size).arg(ents[0].size, 0, 16)
                                               .arg(ents[0].attrs.value("blocks").toInt()).arg(ents[0].attrs.value("blocks").toInt() == 1 ? "" : "s")));
        form->addRow("Position:", new QLabel(QString("file %1 on the tape").arg(ents[0].attrs.value("position").toInt())));
    }
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    for (int i = 0; i < ents.size(); ++i) {
        const dw::fs::Entry &e = ents[i];
        PropertyChange c;
        c.item = items[i];
        c.attrs["protected"] = cbP->checkState() == Qt::PartiallyChecked ? e.attrs.value("protected").toBool() : cbP->checkState() == Qt::Checked;
        if (loadEd) c.attrs["load"] = loadEd->text().toUInt(nullptr, 16);
        if (execEd) c.attrs["exec"] = execEd->text().toUInt(nullptr, 16);
        if (nameEd && nameEd->text() != e.shown()) c.to = dw::fs::Path({nameEd->text()});
        edit->changes << c;
    }
    return true;
}

// ------------------------------------------------------------------ extracting

std::shared_ptr<dw::xfer::HostPolicy> MainWindow::hostPolicy(const QVector<dw::TransferItem> &) const {
    return std::make_shared<RetroFiles::AcornPolicy>(writeInfOnExtract());
}

static void noDates(QVector<dw::TransferItem> *items) {   // Extract has never set the files' dates (a drag does)
    for (dw::TransferItem &t : *items) {
        t.when = QDateTime();
        noDates(&t.kids);
    }
}

// Extract, on the window kit's pattern for copying out: the items (exportItems: a directory with what is in it) are
// written by this program's host policy (RetroFiles::AcornPolicy: host-safe names, DFS NAME or D.NAME, a .inf beside
// every ADFS and DFS file when Write .inf is on). The selection of the pane in use, or with nothing selected, all of
// it: the root of ADFS or DOS, the side in use of DFS. DFS files from both sides go into a folder per side.
void MainWindow::extractSelected() {
    if (!m_doc.isOpen()) return;
    const int ds = dfsSide();
    const bool dos = ds < 0 && dosActive();
    const bool tape = tapeActive();
    AppSettings s;
    QString dest = QFileDialog::getExistingDirectory(this, ds >= 0 ? "Extract DFS files to folder"
                                                           : dos ? "Extract DOS files to folder" : "Extract to folder",
                                                     s.value("extractDir").toString());
    if (dest.isEmpty()) return;
    s.setValue("extractDir", dest);

    QVector<Item> items;
    QString err;
    if (ds >= 0) {
        const QVector<FSel> sel = dfsSelected();
        if (sel.isEmpty()) {
            for (const dw::dfs::File &f : m_doc.dfs(ds).files()) items << Item(m_doc.dfsMount(ds), dw::fs::Path({f.fullName()}));
        } else {
            for (const FSel &x : sel)
                if (m_doc.dfs(x.side).indexOf(x.dir, x.name) >= 0) items << Item(m_doc.dfsMount(x.side), dw::fs::Path({x.item->data(0, RoleName).toString()}));
        }
    } else {
        const int mount = tape ? m_doc.tapeMount() : dos ? m_doc.dosMount() : m_doc.adfsMount();
        QList<QTreeWidgetItem *> rows;
        if (tape) {
            for (QTreeWidgetItem *it : m_tapeTree->selectedItems()) if (!it->data(0, RoleIsRoot).toBool()) rows << it;
        } else if (dos) for (const DSel &x : dosSelected()) rows << x.item;
        else for (const Sel &x : selectedObjects()) rows << x.item;
        if (rows.isEmpty()) {           // nothing selected: extract the whole disc
            QVector<dw::fs::Entry> root;
            const dw::fs::Volume *v = m_doc.disc().volume(mount);
            if (!v || !v->list(dw::fs::Path(), &root, &err)) { QMessageBox::warning(this, "Extract", err); return; }
            for (const dw::fs::Entry &e : root) items << Item(mount, dw::fs::Path({e.name}));
        } else {
            for (QTreeWidgetItem *it : rows) {
                const dw::ui::PaneRow r = dw::ui::VolumePane::rowOf(it);
                if (r.valid() && !r.root) items << Item(r.mount, r.path);
            }
        }
        for (const Item &i : items)
            if (QFileInfo::exists(dest + "/" + dw::hostSafeName(i.path.name()))) {
                if (QMessageBox::question(this, "Extract", "Some files already exist in that folder. Overwrite them?") !=
                    QMessageBox::Yes)
                    return;
                break;
            }
    }
    QStringList errors;
    QVector<dw::TransferItem> out;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool both = false;
    for (const Item &i : items) both = both || i.mount != items[0].mount;
    for (const Item &i : items) {
        QVector<dw::TransferItem> one;
        if (!exportItems({i}, &one, &err)) { errors << QString("%1: %2").arg(i.path.name(), err); continue; }
        noDates(&one);
        if (both) {                     // DFS files from both sides: "Drive 0", "Drive 2"
            const QString folder = QString("Drive %1").arg(paneOf(i.mount) == PaneDfs1 ? 2 : 0);
            int at = -1;
            for (int k = 0; k < out.size(); ++k) if (out[k].isDir && out[k].name == folder) at = k;
            if (at < 0) {
                dw::TransferItem d;
                d.name = folder;
                d.isDir = true;
                out << d;
                at = out.size() - 1;
            }
            out[at].kids << one;
        } else {
            out << one;
        }
    }
    QJsonArray json;
    dw::xfer::writeHost(out, dest, *hostPolicy(out), nullptr, &json, &errors);
    QApplication::restoreOverrideCursor();
    if (errors.isEmpty())
        statusBar()->showMessage(QString(ds >= 0 ? "Extracted %1 file(s) to %2" : "Extracted %1 item(s) to %2").arg(items.size()).arg(dest), 6000);
    else QMessageBox::warning(this, "Extract", errors.join("\n"));
}
