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
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialog>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QDataStream>
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
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <functional>

using namespace Adfs;


// ===================================================================== DropTree

DropTree::DropTree(QWidget *parent) : QTreeWidget(parent) {
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
}

QStringList DropTree::mimeTypes() const { return QStringList() << WRANGLER_MIME_ITEMS; }
Qt::DropActions DropTree::supportedDropActions() const { return Qt::CopyAction; }   // copying never removes the source
QMimeData *DropTree::mimeData(const QList<QTreeWidgetItem *> items) const { return makeMime(items); }

static QByteArray sourceTag(quint64 owner, int pane) {
    return QByteArray::number(qint64(QCoreApplication::applicationPid())) + ":" + QByteArray::number(qulonglong(owner)) +
           ":" + QByteArray::number(pane);
}

bool DropTree::fromThisProcess(const QMimeData *m) {
    if (!m->hasFormat(WRANGLER_MIME_SOURCE) || !m->hasFormat(WRANGLER_MIME_ITEMS)) return false;
    QList<QByteArray> f = m->data(WRANGLER_MIME_SOURCE).split(':');
    // window ids are addresses, so the same id can exist in another copy of the program: check the process too
    return f.size() == 3 && f[0].toLongLong() == qint64(QCoreApplication::applicationPid()) &&
           MainWindow::byId(f[1].toULongLong()) != nullptr;
}

// Our own drag, rather than QAbstractItemView's, so that we know whether the drop was accepted: on Windows the files
// may already have been written while the drag passed over another program, and a drag that then comes back and is
// let go of here (or anywhere it isn't accepted) must not report "dragged out".
void DropTree::startDrag(Qt::DropActions) {
    QList<QTreeWidgetItem *> items = selectedItems();
    QMimeData *data = makeMime(items);
    if (!data) return;
    std::shared_ptr<RetroFiles::DragState> state = m_lastDrag;
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
    Qt::DropAction result = drag->exec(Qt::CopyAction, Qt::CopyAction);   // copy only: never "move" off the disc
    // Don't delete the drag: on X11 the drop target fetches the files after exec() returns, and Qt deletes the
    // drag itself when that is over (QAbstractItemView doesn't delete its drags either). It is a child of this
    // tree, so it goes at the latest when the window closes.
    if (state && state->extracted && result == Qt::CopyAction)
        emit dragOutFinished(state->files, state->dirs, state->errors);
    m_lastDrag.reset();
}

QMimeData *DropTree::makeMime(const QList<QTreeWidgetItem *> &items) const {
    QList<QTreeWidgetItem *> sel;
    for (QTreeWidgetItem *it : items) {
        if (it->data(0, RoleIsRoot).toBool()) continue;
        bool covered = false;                     // a child of a selected directory is copied with it
        for (QTreeWidgetItem *p = it->parent(); p; p = p->parent())
            if (!p->data(0, RoleIsRoot).toBool() && items.contains(p)) { covered = true; break; }
        if (!covered) sel << it;
    }
    if (sel.isEmpty()) return nullptr;
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << quint64(m_owner) << qint32(m_pane) << qint32(sel.size());
    QVector<DragRef> refs;
    for (QTreeWidgetItem *it : sel) {
        DragRef r;
        r.parent = it->data(0, RoleParent).toUInt();
        r.name = it->data(0, RoleName).toString();
        r.isDir = it->data(0, RoleIsDir).toBool();
        refs << r;
        ds << quint32(r.parent) << r.name << r.isDir;
    }
    // the files themselves, for the host or another program: exported from the window when (if) they're asked for
    QPointer<MainWindow> win = MainWindow::byId(m_owner);
    int pane = m_pane;
    RetroFiles::DragOutMime::Exporter exporter = [win, pane, refs](QVector<XItem> *out, QString *err) {
        if (!win) { *err = "the disc's window was closed during the drag"; return false; }
        return win->exportRefs(pane, refs, out, err);
    };
    auto *m = new RetroFiles::DragOutMime(exporter, win && win->writeInfOnExtract(),
                                          win ? QFileInfo(win->image().path()).fileName() : QString(), m_pane);
    m->setData(WRANGLER_MIME_ITEMS, payload);
    m->setData(WRANGLER_MIME_SOURCE, sourceTag(m_owner, m_pane));
    m_lastDrag = m->state();
    return m;
}

bool DropTree::acceptable(const QMimeData *m) const {
    // Only format names are looked at here: asking for a drag's file list would make its source write the files.
    if (fromThisProcess(m))                      // items from another tree (but not dropped back onto their own)
        return m->data(WRANGLER_MIME_SOURCE) != sourceTag(m_owner, m_pane);
    return m->hasFormat(RetroFiles::MimeType) || m->hasUrls();
}

void DropTree::dragEnterEvent(QDragEnterEvent *e) {
    if (acceptable(e->mimeData())) e->acceptProposedAction();
}

void DropTree::dragMoveEvent(QDragMoveEvent *e) {
    if (!acceptable(e->mimeData())) return;
    e->acceptProposedAction();
    QTreeWidgetItem *it = itemAt(e->pos());     // highlight where the drop will land
    if (it && it != currentItem()) setCurrentItem(it);
}

void DropTree::dropEvent(QDropEvent *e) {
    if (!acceptable(e->mimeData())) return;
    if (fromThisProcess(e->mimeData())) {        // one of this program's own windows: copy directly
        e->setDropAction(Qt::CopyAction);
        e->accept();
        emit internalDrop(e->mimeData()->data(WRANGLER_MIME_ITEMS), itemAt(e->pos()));
        return;
    }
    MainWindow *win = MainWindow::byId(m_owner);
    bool empty = win && !win->image().isOpen();  // an empty window opens dropped files, so it wants them as files
    if (!empty && e->mimeData()->hasFormat(RetroFiles::MimeType)) {   // another copy of this program, or another tool
        QByteArray json = e->mimeData()->data(RetroFiles::MimeType);
        if (!json.isEmpty()) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            emit retroFilesDropped(json, itemAt(e->pos()));
            return;
        }
    }
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (paths.isEmpty()) return;
    e->acceptProposedAction();
    emit pathsDropped(paths, itemAt(e->pos()));
}

// ===================================================================== helpers



static QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QChar('0')).toUpper(); }

// --- .inf sidecar files:  NAME LOAD EXEC [LENGTH] [ATTR|L|Locked]   (InfData is declared in mainwindow_p.h)
static quint32 parseAddr(const QString &tok, bool *ok) {
    quint32 v = tok.toUInt(ok, 16);
    if (*ok && tok.size() <= 6 && v >= 0xFF0000) v |= 0xFF000000u;   // 6-digit FFxxxx = I/O processor address
    return v;
}

bool readInf(const QString &hostPath, InfData *out) {
    QString infPath;
    for (const QString &ext : {QString(".inf"), QString(".INF")})
        if (QFileInfo::exists(hostPath + ext)) { infPath = hostPath + ext; break; }
    if (infPath.isEmpty()) return false;
    QFile f(infPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QString line = QString::fromLatin1(f.readLine()).trimmed();
    QString name, rest;
    if (line.startsWith('"')) {
        int q = line.indexOf('"', 1);
        if (q < 0) return false;
        name = line.mid(1, q - 1);
        rest = line.mid(q + 1).trimmed();
    } else {
        int sp = line.indexOf(QRegularExpression("\\s"));
        name = sp < 0 ? line : line.left(sp);
        rest = sp < 0 ? QString() : line.mid(sp + 1).trimmed();
    }
    QStringList t = rest.split(QRegularExpression("\\s+"));
    t.removeAll(QString());
    InfData d;
    QStringList parts = name.split('.');
    d.name = parts.last();
    if (parts.size() >= 2 && parts[parts.size() - 2].size() == 1) d.dir = parts[parts.size() - 2][0];   // "D.NAME"
    bool ok = false;
    if (t.size() > 0) d.load = parseAddr(t[0], &ok);
    if (t.size() > 1) d.exec = parseAddr(t[1], &ok);
    bool sawAttr = false;
    quint32 attr = 0;
    for (int i = 2; i < t.size(); ++i) {
        const QString &k = t[i];
        if (k.compare("L", Qt::CaseInsensitive) == 0 || k.compare("Locked", Qt::CaseInsensitive) == 0) {
            attr |= 8; sawAttr = true;
        } else if (i == 3) {
            quint32 a = k.toUInt(&ok, 16);
            if (ok) { attr |= a; sawAttr = true; }
        }
    }
    if (sawAttr)
        d.flags = quint8(((attr & 1) ? FlagR : 0) | ((attr & 2) ? FlagW : 0) | ((attr & 4) ? FlagE : 0) |
                         ((attr & 8) ? FlagL : 0));
    *out = d;
    return true;
}


// ================================================================== MainWindow

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    resize(980, 640);
    setWindowTitle("Acorn Disk Wrangler");

    // Column layouts (see setupTreeColumns). Shared columns use the same sample text in every pane
    // so ADFS, DFS and DOS windows line up; DOS keeps the ADFS order: numbers, then the date.
    const ColSpec name{"Name", "", false}, attr{"Attr", "DLWRE", false},
        load{"Load", "FFFFFFFF", true}, exec{"Exec", "FFFFFFFF", true}, length{"Length", "999999999", true},
        sector{"Sector", "9999999", true}, typeDate{"Type / date", "FFF   2026-12-31 23:59", false},
        size{"Size", "999999999", true}, cluster{"Cluster", "9999999", true},
        modified{"Modified", "FFF   2026-12-31 23:59", false};
    m_tree = new DropTree(this);
    setupTreeColumns(m_tree, {name, attr, load, exec, length, sector, typeDate});
    m_dosTree = new DropTree(nullptr);
    setupTreeColumns(m_dosTree, {name, attr, size, cluster, modified});
    for (int s = 0; s < 2; ++s) {
        m_dfsTree[s] = new DropTree(nullptr);
        setupTreeColumns(m_dfsTree[s], {name, attr, load, exec, length, sector});
    }
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_tree, "ADFS");
    setCentralWidget(m_tabs);
    windows().append(this);
    m_tree->setPane(id(), PaneAdfs);
    m_dosTree->setPane(id(), PaneDos);
    m_dfsTree[0]->setPane(id(), PaneDfs0);
    m_dfsTree[1]->setPane(id(), PaneDfs1);

    m_status = new QLabel(this);
    m_status->setMinimumWidth(420);
    m_status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(m_status);

    // actions
    auto mk = [this](const QString &text, const QKeySequence &key = QKeySequence()) {
        QAction *a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        return a;
    };
    m_actOpen = mk("&Open image...", QKeySequence::Open);
    m_actSave = mk("&Save changes", QKeySequence::Save);
    m_actRevert = mk("&Revert unsaved changes");
    m_actQuit = mk("&Quit", QKeySequence::Quit);
    m_actNewWin = mk("&New window", QKeySequence("Ctrl+Shift+N"));
    m_actAddFiles = mk("Add &files...", QKeySequence("Ctrl+Shift+F"));
    m_actAddFolder = mk("Add f&older...", QKeySequence("Ctrl+Shift+D"));
    m_actNewDir = mk("&New directory...", QKeySequence("Ctrl+N"));
    m_actView = mk("&View file (text / hex / BASIC)...", QKeySequence("F3"));
    m_actRename = mk("Re&name...", QKeySequence("F2"));
    m_actRenameVol = mk("Rename &volume / disc name...");
    m_actProps = mk("&Properties / attributes...", QKeySequence("Alt+Return"));
    m_actExtract = mk("&Extract to host...", QKeySequence("Ctrl+E"));
    m_actDelete = mk("&Delete", QKeySequence::Delete);
    m_actCheck = mk("&Check free space map...", QKeySequence("Ctrl+K"));
    m_actNewImage = mk("New &image...", QKeySequence("Ctrl+Alt+N"));
    m_actBootOpt = mk("&Boot option...");
    m_actReadIl = mk("Read as &interleaved (sides alternate track by track)");
    m_actReadSeq = mk("Read as &sequential (all of side 0, then side 1)");
    m_actConvertSides = mk("&Convert to sequential...");
    for (QAction *a : {m_actReadIl, m_actReadSeq}) a->setCheckable(true);
    auto *sideGroup = new QActionGroup(this);
    sideGroup->addAction(m_actReadIl);
    sideGroup->addAction(m_actReadSeq);
    m_optReadInf = mk("Read .inf sidecar files when adding");
    m_optWriteInf = mk("Write .inf sidecar files when extracting");
    m_optBackup = mk("Keep a .bak copy of the image when saving");
    for (QAction *a : {m_optReadInf, m_optWriteInf, m_optBackup}) a->setCheckable(true);
    QSettings st;
    m_optReadInf->setChecked(st.value("readInf", true).toBool());
    m_optWriteInf->setChecked(st.value("writeInf", true).toBool());
    m_optBackup->setChecked(st.value("backup", false).toBool());

    QMenu *mf = menuBar()->addMenu("&File");
    mf->addActions({m_actNewWin, m_actNewImage, m_actOpen, m_actSave, m_actRevert});
    mf->addSeparator();
    mf->addAction(m_actQuit);
    QMenu *me = menuBar()->addMenu("&Edit");
    me->addActions({m_actView, m_actAddFiles, m_actAddFolder, m_actNewDir, m_actRename, m_actRenameVol, m_actBootOpt, m_actProps, m_actExtract, m_actDelete});
    QMenu *mt = menuBar()->addMenu("&Tools");
    mt->addAction(m_actCheck);
    QMenu *ms = mt->addMenu("&Side order (double-sided images)");
    ms->addActions({m_actReadIl, m_actReadSeq});
    ms->addSeparator();
    ms->addAction(m_actConvertSides);
    mt->addSeparator();
    mt->addActions({m_optReadInf, m_optWriteInf, m_optBackup});
    QMenu *mh = menuBar()->addMenu("&Help");
    mh->addAction("&About Acorn Disk Wrangler...", this, &MainWindow::about);
    mh->addAction("About &Qt...", qApp, &QApplication::aboutQt);

    m_actView->setIconText("View");
    m_actAddFiles->setIconText("Add files");
    m_actAddFolder->setIconText("Add folder");
    m_actNewDir->setIconText("New dir");
    m_actProps->setIconText("Properties");
    m_actExtract->setIconText("Extract");
    QToolBar *tb = addToolBar("Main");
    tb->setMovable(false);
    // Open / Save live in the File menu only; the toolbar is for working on the disc contents.
    tb->addActions({m_actView, m_actAddFiles, m_actAddFolder, m_actNewDir, m_actRename, m_actProps, m_actExtract, m_actDelete});

    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openDialog);
    connect(m_actSave, &QAction::triggered, this, [this] { save(); });
    connect(m_actRevert, &QAction::triggered, this, &MainWindow::revert);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);
    connect(m_actAddFiles, &QAction::triggered, this, &MainWindow::addFilesDialog);
    connect(m_actAddFolder, &QAction::triggered, this, &MainWindow::addFolderDialog);
    connect(m_actNewDir, &QAction::triggered, this, &MainWindow::newDirectory);
    connect(m_actDelete, &QAction::triggered, this, &MainWindow::deleteSelected);
    connect(m_actProps, &QAction::triggered, this, &MainWindow::editProperties);
    connect(m_actExtract, &QAction::triggered, this, &MainWindow::extractSelected);
    connect(m_actCheck, &QAction::triggered, this, &MainWindow::checkImage);
    connect(m_actNewImage, &QAction::triggered, this, &MainWindow::newImageDialog);
    connect(m_actBootOpt, &QAction::triggered, this, &MainWindow::bootOptionDialog);
    connect(m_actReadIl, &QAction::triggered, this, [this] { readWithSideOrder(false); });
    connect(m_actReadSeq, &QAction::triggered, this, [this] { readWithSideOrder(true); });
    connect(m_actConvertSides, &QAction::triggered, this, [this] {
        if (m_img.convertPending()) {                     // the action reads "Cancel ..." while one is staged
            QString e;
            m_img.setConvertOnSave(m_img.sideLayout().sequential, &e);
            updateUi();
        } else {
            convertSideOrder(!m_img.sideLayout().sequential);
        }
    });
    connect(m_tree, &DropTree::pathsDropped, this, &MainWindow::onDropped);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateUi);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_dosTree, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_dosTree, &DropTree::pathsDropped, this, &MainWindow::onDosDropped);
    connect(m_dosTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateUi);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateUi);
    connect(m_actView, &QAction::triggered, this, &MainWindow::viewSelected);
    connect(m_actRename, &QAction::triggered, this, &MainWindow::renameSelected);
    connect(m_actRenameVol, &QAction::triggered, this, &MainWindow::renameVolume);
    auto dbl = [this](QTreeWidgetItem *it) {
        if (it && !it->data(0, RoleIsDir).toBool()) viewSelected();
    };
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, dbl);
    connect(m_dosTree, &QTreeWidget::itemDoubleClicked, this, dbl);
    connect(m_actNewWin, &QAction::triggered, this, [] {
        auto *w = new MainWindow;
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });
    for (int s = 0; s < 2; ++s) {
        DropTree *t = m_dfsTree[s];
        connect(t, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
        connect(t, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateUi);
        connect(t, &QTreeWidget::itemDoubleClicked, this, dbl);
        connect(t, &DropTree::pathsDropped, this, [this, s](const QStringList &p, QTreeWidgetItem *) {
            if (!dropImages(s ? PaneDfs1 : PaneDfs0, p, [this, s](const QStringList &r) { dfsImportPaths(s, r); }))
                dfsImportPaths(s, p);
        });
    }
    // items dragged in from another disc/window (resolved now, handled after the drop returns)
    for (DropTree *t : {m_tree, m_dosTree, m_dfsTree[0], m_dfsTree[1]})
        connect(t, &DropTree::internalDrop, this, [this, t](const QByteArray &payload, QTreeWidgetItem *it) {
            int pane = t->pane();
            quint32 dir = dropTarget(pane, it);
            QTimer::singleShot(0, this, [this, pane, dir, payload] { onInternalDrop(pane, dir, payload); });
        });
    for (DropTree *t : {m_tree, m_dosTree, m_dfsTree[0], m_dfsTree[1]}) {
        connect(t, &DropTree::retroFilesDropped, this, [this, t](const QByteArray &json, QTreeWidgetItem *it) {
            int pane = t->pane();
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
    }
    // a save is vetoed if the DOS partition was touched and fails its own consistency check
    m_img.setExtraValidator([this]() -> QStringList {
        QStringList out;
        if (m_dos.attached() && m_img.pendingTouches(m_dos.sin(), Adfs::sectorsFor(m_dos.length())))
            for (const QString &e : m_dos.check().errors) out << "DOS partition: " + e;
        for (int s = 0; s < 2; ++s)
            if (m_dfs[s].attached() && m_img.pendingTouches(0, quint32(m_img.fullSize() / Adfs::SectorSize)))
                for (const QString &e : m_dfs[s].check().errors) out << QString("DFS drive %1: %2").arg(s * 2).arg(e);
        return out;
    });
    for (QAction *a : {m_optReadInf, m_optWriteInf, m_optBackup})
        connect(a, &QAction::toggled, this, [this] {
            QSettings s;
            s.setValue("readInf", m_optReadInf->isChecked());
            s.setValue("writeInf", m_optWriteInf->isChecked());
            s.setValue("backup", m_optBackup->isChecked());
        });
    updateUi();
    statusBar()->showMessage("Open a disc image (File > Open), or drop one onto the window.");
}

// ---------------------------------------------------------------- open/save

bool MainWindow::confirmDiscard() {
    if (!m_img.dirty()) return true;
    auto b = QMessageBox::question(this, "Unsaved changes",
                                   "The image has unsaved changes. Save them first?",
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                   QMessageBox::Save);
    if (b == QMessageBox::Cancel) return false;
    if (b == QMessageBox::Save) return save();
    return true;
}

void MainWindow::openDialog() {
    QSettings s;
    QString p = QFileDialog::getOpenFileName(
        this, "Open disc image", s.value("lastDir").toString(),
        "Disc images (*.dat *.img *.ima *.dsk *.hdf *.adf *.adl *.adm *.ads *.ssd *.dsd *.bin);;All files (*)");
    if (p.isEmpty()) return;
    MainWindow *target = this;
    if (m_img.isOpen()) {                       // each disc lives in its own window, so they can be dragged between
        target = new MainWindow;
        target->setAttribute(Qt::WA_DeleteOnClose);
        target->show();
    }
    if (!target->openImage(p) && target != this) target->close();
}

bool MainWindow::openImage(const QString &path, int force) {
    if (!confirmDiscard()) return false;
    DiskInfo info = detectDisk(path, force);
    // the contents can't always tell (e.g. a blank disc): then detection uses the format's usual order
    QString sidesHow = force >= 0 ? "set by hand" : (info.sides.doubleSided() && !info.sidesConfirmed ? "assumed" : "");
    if (info.kind == DiskInfo::AdfsNewMap) {
        QMessageBox::information(this, "Not supported",
                                 info.description + "\n\nADFS D/E (new map) discs are not supported yet.");
        return false;
    }
    QString err;
    bool raw = info.kind == DiskInfo::Dfs || info.kind == DiskInfo::DosPc || info.kind == DiskInfo::DosAcorn800;
    bool ok = raw ? m_img.openRaw(path, info.fullSize, &err, info.sides)
                  : m_img.open(path, &err, info.kind == DiskInfo::AdfsDosHybrid ? info.fullSize : 0, info.sides);
    if (!ok) {
        QMessageBox::critical(this, "Cannot open image", err);
        m_info = DiskInfo();
        refreshTree(); updateUi();
        return false;
    }
    m_info = info;
    m_sidesHow = info.sides.doubleSided() ? sidesHow : QString();
    if (info.kind == DiskInfo::AdfsDosHybrid) m_img.setAdfsLocked(true);
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    m_dosOk = false;
    m_dfsOk[0] = m_dfsOk[1] = false;
    refreshTree();
    if (m_dos.attached()) {
        Report dr = m_dos.check();
        m_dosOk = dr.ok();
        if (!dr.ok())
            showReport("DOS volume has errors",
                       "The DOS filesystem has structural errors, so it can be browsed but not changed.", dr, true);
    }
    for (int s = 0; s < 2; ++s)
        if (m_dfs[s].attached()) {
            Report dr = m_dfs[s].check();
            m_dfsOk[s] = dr.ok();
            if (!dr.ok())
                showReport(QString("DFS drive %1 has errors").arg(s * 2),
                           "This DFS catalogue has errors, so the drive can be browsed but not changed.", dr, true);
        }
    m_tabs->setCurrentIndex(0);
    updateUi();

    QString pad;
    if (m_img.needsPadding())
        pad = QString("  NOTE: the image file is %1 bytes shorter than a full disc; it will be padded with zeros when saved.")
                  .arg(m_img.fullSize() - m_img.fileSize());
    const Report &r = m_img.openReport();
    if (m_img.isAdfs() && !r.ok()) {
        showReport("Image failed validation",
                   "This image's free space map / directory structure is damaged, so it has been opened "
                   "READ-ONLY. Nothing can be added or changed until it has been repaired.", r, true);
    } else if (!m_img.editable()) {
        QMessageBox::information(this, "Read-only", "The image file is not writable, so it has been opened read-only.");
    } else if (m_img.isAdfs() && !r.warnings.isEmpty() && m_info.kind != DiskInfo::AdfsDosHybrid) {
        statusBar()->showMessage("Opened with warnings - see Tools > Check." + pad, 15000);
    } else {
        QString sides;
        if (m_sidesHow == "assumed")
            sides = "  The side order can't be told from the contents, so the usual order (interleaved) is used - Tools > Side order to change.";
        statusBar()->showMessage("Opened " + path + " - " + (info.description.isEmpty() ? QString("ADFS") : info.description) + "." + pad + sides, 15000);
    }
    return true;
}

// ---------------------------------------------------------------- side order

bool MainWindow::readWithSideOrder(bool sequential) {
    if (!m_img.isOpen() || !m_img.sideLayout().doubleSided()) return false;
    if (m_img.sideLayout().sequential == sequential && !m_img.convertPending()) { updateUi(); return true; }
    bool ok = openImage(m_img.path(), sequential ? 1 : 0);
    updateUi();
    return ok;
}

bool MainWindow::convertSideOrder(bool toSequential, bool confirm) {
    if (!m_img.isOpen() || !m_img.sideLayout().doubleSided()) return false;
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
    if (!m_img.setConvertOnSave(toSequential, &err)) {
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
    if (ds >= 0) return m_dfs[ds].attached() ? m_dfs[ds].bootOption() : -1;
    return m_img.isAdfs() ? m_img.bootOption() : -1;
}

bool MainWindow::setBootOption(int opt) {
    QString err;
    int ds = dfsSide();
    bool ok;
    if (dosActive() || !m_img.isOpen()) { err = "DOS discs have no boot option"; ok = false; }
    else if (ds >= 0) ok = dfsEditable(ds) ? m_dfs[ds].setBootOption(opt, &err) : (err = "this DFS drive is read-only", false);
    else ok = m_img.setBootOption(opt, &err);
    if (!ok) QMessageBox::warning(this, "Boot option", err);
    refreshTree();
    updateUi();
    return ok;
}

void MainWindow::bootOptionDialog() {
    int cur = bootOption();
    if (cur < 0) return;
    QStringList items;
    for (int i = 0; i < 4; ++i) items << Adfs::bootOptionName(i);
    if (cur > 3) items << Adfs::bootOptionName(cur);           // keep an odd value visible rather than hide it
    bool ok = false;
    int ds = dfsSide();
    QString which = ds >= 0 ? QString("DFS drive %1").arg(ds * 2) : QString("this disc");
    QString pick = QInputDialog::getItem(this, "Boot option",
                                         "What SHIFT+BREAK does with " + which + " (*OPT 4):",
                                         items, cur > 3 ? 4 : cur, false, &ok);
    if (!ok) return;
    int opt = items.indexOf(pick);
    if (opt != cur && opt <= 3) setBootOption(opt);
}

// ---------------------------------------------------------------- new image

void MainWindow::newImageDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("New disc image");
    auto *form = new QFormLayout;
    auto *type = new QComboBox;
    for (const NewDisc::TypeInfo &t : NewDisc::types()) type->addItem(t.name, int(t.type));
    QSettings st;
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
                                                 "DOS Plus 640K disc or DOSBOOT (*.adl *.adf *.img *.dsk dosboot*);;All files (*)");
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
        sideBox->setEnabled(t.doubleSided);
        bool hyb = t.type == NewDisc::Acorn640;
        bootLabel->setVisible(hyb);
        bootBox->setVisible(hyb);
        note->setText(hyb ? "Set up like Acorn's DOS Plus 640K discs, with no DOS files.\n"
                            "DOSBOOT is Acorn's boot code, so it is copied from a disc you have:\n"
                            "a DOS Plus 640K image (its boot track is copied exactly) or a DOSBOOT file." : QString());
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
    QString base = title->text().trimmed().isEmpty() ? QString("untitled") : hostSafeName(title->text().trimmed());
    QString path = QFileDialog::getSaveFileName(this, "Create disc image", st.value("lastDir").toString() + "/" + base + "." + ext,
                                                QString("Disc image (*.%1);;All files (*)").arg(ext));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += "." + ext;
    QString abs = QFileInfo(path).absoluteFilePath();
    for (MainWindow *w : windows())
        if (w->m_img.isOpen() && QFileInfo(w->m_img.path()).absoluteFilePath() == abs) {
            QMessageBox::warning(this, "New disc image", "That file is open in another window. Close it first, or choose another name.");
            return;
        }
    bool sequential = t.doubleSided && seq->isChecked();
    QString err;
    if (!NewDisc::create(path, t.type, title->text(), sequential, &err, boot->text())) {
        QMessageBox::critical(this, "New disc image", "Could not create the image:\n" + err);
        return;
    }
    st.setValue("lastDir", QFileInfo(path).absolutePath());
    MainWindow *target = this;
    if (m_img.isOpen()) {
        target = new MainWindow;
        target->setAttribute(Qt::WA_DeleteOnClose);
        target->show();
    }
    if (!target->openImage(path) && target != this) target->close();
}

bool MainWindow::save() {
    if (!m_img.isOpen() || !(m_img.dirty() || m_img.needsPadding())) return true;
    QString err;
    Report post;
    bool converting = m_img.convertPending();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_img.commit(&err, m_optBackup->isChecked(), &post);
    QApplication::restoreOverrideCursor();
    if (converting && !m_img.convertPending()) {      // the file's side order changed, whether or not all else went well
        m_info.sides = m_img.sideLayout();
        m_sidesHow.clear();
    }
    if (!ok) {
        QMessageBox::critical(this, "Save failed", err);
        refreshTree(); updateUi();
        return false;
    }
    refreshTree(); updateUi();
    statusBar()->showMessage("Saved. Free space map and directories re-checked: OK.", 6000);
    return true;
}

void MainWindow::revert() {
    if (!m_img.dirty()) return;
    if (QMessageBox::question(this, "Revert", "Discard all unsaved changes?") != QMessageBox::Yes) return;
    m_img.revert();
    refreshTree(); updateUi();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (confirmDiscard()) e->accept(); else e->ignore();
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
                           "<p>Uses Qt 5 under the LGPL v3. Shares its drag-and-drop format (retro-files) with CP/M Disk "
                           "Wrangler. See THIRD-PARTY-NOTICES.md for everything consulted or used.</p>");
    box.exec();
}

void MainWindow::checkImage() {
    if (!m_img.isOpen()) return;
    Report r = m_img.validate();
    if (m_dos.attached()) {
        Report dr = m_dos.check();
        for (const QString &s : dr.errors) r.errors << "DOS: " + s;
        for (const QString &s : dr.warnings) r.warnings << "DOS: " + s;
        r.info += dr.info;
    }
    for (int s = 0; s < 2; ++s)
        if (m_dfs[s].attached()) {
            Report dr = m_dfs[s].check();
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

void MainWindow::refreshTree() {
    QSet<quint32> expanded;
    std::function<void(QTreeWidgetItem *)> gather = [&](QTreeWidgetItem *it) {
        if (it->isExpanded() && it->data(0, RoleIsDir).toBool()) expanded.insert(it->data(0, RoleSector).toUInt());
        for (int i = 0; i < it->childCount(); ++i) gather(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) gather(m_tree->topLevelItem(i));
    bool first = m_tree->topLevelItemCount() == 0;

    m_tree->clear();
    ensureTab(m_tree, m_img.adfsLocked() ? "ADFS (read-only)" : "ADFS", !m_img.isOpen() || m_img.isAdfs());
    if (!m_img.isOpen() || !m_img.isAdfs()) { refreshDos(); refreshDfs(); return; }

    Dir root;
    QString err;
    QTreeWidgetItem *r = new QTreeWidgetItem(m_tree);
    if (m_img.readDir(RootSector, &root, &err)) {
        QString t = root.title();
        r->setText(0, rootLabel("$", t));
    } else {
        r->setText(0, rootLabel("$", "unreadable root directory"));
    }
    r->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
    r->setData(0, RoleParent, 0);
    r->setData(0, RoleName, "$");
    r->setData(0, RoleIsDir, true);
    r->setData(0, RoleSector, RootSector);
    r->setData(0, RoleIsRoot, true);
    QSet<quint32> seen;
    populate(r, RootSector, seen, 0);
    expanded.insert(RootSector);

    std::function<void(QTreeWidgetItem *)> restore = [&](QTreeWidgetItem *it) {
        if (it->data(0, RoleIsDir).toBool() && (first ? it == r : expanded.contains(it->data(0, RoleSector).toUInt())))
            it->setExpanded(true);
        for (int i = 0; i < it->childCount(); ++i) restore(it->child(i));
    };
    restore(r);
    refreshDos();
    refreshDfs();
}

void MainWindow::populate(QTreeWidgetItem *parentItem, quint32 dirSector, QSet<quint32> &seen, int depth) {
    if (depth > 64 || seen.contains(dirSector)) return;
    seen.insert(dirSector);
    Dir d;
    QString err;
    if (!m_img.readDir(dirSector, &d, &err)) {
        parentItem->setToolTip(0, err);
        parentItem->setForeground(0, Qt::red);
        return;
    }
    for (const Entry &e : d.entries) {
        auto *it = new QTreeWidgetItem(parentItem);
        QString nm = e.name();
        it->setText(0, nm.isEmpty() ? "<bad name>" : nm);
        it->setText(1, e.attrString());
        setNumCell(it, 5, QString::number(e.sin));
        it->setData(0, RoleParent, dirSector);
        it->setData(0, RoleName, nm);
        it->setData(0, RoleIsDir, e.isDir());
        it->setData(0, RoleSector, e.sin);
        it->setData(0, RoleIsRoot, false);
        if (e.isDir()) {
            it->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            it->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
            setNumCell(it, 2, hex8(e.load));
            setNumCell(it, 3, hex8(e.exec));
            setNumCell(it, 4, QString::number(e.length));
            it->setToolTip(4, QString("%1 bytes (&%2), %3 sectors").arg(e.length).arg(e.length, 0, 16).arg(sectorsFor(e.length)));
            if (e.hasStamp()) {
                QDateTime t = e.stamp();
                if (t.date().year() >= 1981 && t.date().year() <= 2100)   // else it's just an address like FFFF1900
                    it->setText(6, QString("%1   %2").arg(e.fileType(), 3, 16, QChar('0')).toUpper().arg(t.toString("yyyy-MM-dd hh:mm")));
            }
        }
        if (e.isDir()) populate(it, e.sin, seen, depth + 1);
    }
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

void MainWindow::updateUi() {
    bool has = m_img.isOpen(), ed = has && m_img.editable();
    int ds = dfsSide();
    bool dosPane = dosActive();
    bool canEdit = dosPane ? dosEditable() : (ds >= 0 ? dfsEditable(ds) : (ed && m_img.isAdfs() && !m_img.adfsLocked()));
    QTreeWidget *ct = dosPane ? static_cast<QTreeWidget *>(m_dosTree)
                              : (ds >= 0 ? static_cast<QTreeWidget *>(m_dfsTree[ds]) : static_cast<QTreeWidget *>(m_tree));
    bool changed = m_img.dirty() || m_img.needsPadding();
    m_actSave->setEnabled(ed && changed);
    m_actRevert->setEnabled(ed && m_img.dirty());
    m_actAddFiles->setEnabled(canEdit);
    m_actAddFolder->setEnabled(canEdit);
    m_actNewDir->setEnabled(canEdit && ds < 0);      // DFS has no directories
    m_actCheck->setEnabled(has);
    m_actExtract->setEnabled(has);
    bool sel = dosPane ? !dosSelected().isEmpty() : (ds >= 0 ? !dfsSelected(ds).isEmpty() : !selectedObjects().isEmpty());
    m_actDelete->setEnabled(canEdit && sel);
    m_actProps->setEnabled(canEdit && sel);
    m_actView->setEnabled(has && sel);
    m_actRename->setEnabled(canEdit && !ct->selectedItems().isEmpty());
    m_actRenameVol->setEnabled(canEdit);
    m_actBootOpt->setEnabled(canEdit && !dosPane);
    const Adfs::SideLayout &sl = m_img.sideLayout();
    bool twoSides = has && sl.doubleSided();
    m_actReadIl->setEnabled(twoSides);
    m_actReadSeq->setEnabled(twoSides);
    m_actReadIl->setChecked(twoSides && !sl.sequential);
    m_actReadSeq->setChecked(twoSides && sl.sequential);
    m_actConvertSides->setEnabled(twoSides && ed);
    m_actConvertSides->setText(m_img.convertPending() ? "&Cancel the pending side order change"
                                                       : (sl.sequential ? "&Convert to interleaved..." : "&Convert to sequential..."));

    QString title = "Acorn Disk Wrangler";
    if (has) {
        title = QFileInfo(m_img.path()).fileName() + (changed ? " *" : "") + " - " + title;
        if (!m_img.editable()) title += "  [READ-ONLY]";
    }
    setWindowTitle(title);
    if (!has) { m_status->setText(""); return; }
    QString s;
    if (dosPane) {
        s = QString("DOS: free %1 of %2").arg(fmtBytes(quint64(m_dos.freeClusters()) * m_dos.clusterBytes()),
                                              fmtBytes(quint64(m_dos.totalClusters()) * m_dos.clusterBytes()));
        if (!m_dosOk) s += "   (volume has errors: read-only)";
    } else if (ds >= 0) {
        s = QString("DFS drive %1: free %2 of %3").arg(ds * 2).arg(fmtSectors(m_dfs[ds].freeSectors()), fmtSectors(m_dfs[ds].sectors()));
        s += "   Boot: " + Adfs::bootOptionName(m_dfs[ds].bootOption()).mid(4);
        if (!m_dfsOk[ds]) s += "   (catalogue has errors: read-only)";
    } else if (m_img.isAdfs()) {
        s = QString("Free: %1 of %2   (largest block %3)")
                .arg(fmtSectors(m_img.freeSectors()), fmtSectors(m_img.totalSectors()), fmtSectors(m_img.largestFreeRun()));
        s += "   Boot: " + Adfs::bootOptionName(m_img.bootOption()).mid(4);
        if (m_img.adfsLocked()) s += "   (hybrid disc: ADFS side is read-only)";
    }
    if (twoSides) {
        s += QString("   Sides: %1").arg(sl.sequential ? "sequential" : "interleaved");
        if (m_img.convertPending()) s += QString(" -> %1 on save").arg(sl.sequential ? "interleaved" : "sequential");
        else if (!m_sidesHow.isEmpty()) s += " (" + m_sidesHow + ")";
    }
    if (!m_img.editable()) s += "   READ-ONLY";
    else if (m_img.dirty()) s += "   UNSAVED CHANGES";
    else if (m_img.needsPadding()) s += "   (truncated image: will be padded on save)";
    m_status->setText(s);
}

void MainWindow::showContextMenu(const QPoint &pos) {
    QMenu m(this);
    m.addAction(m_actView);
    m.addSeparator();
    m.addActions({m_actAddFiles, m_actAddFolder, m_actNewDir});
    m.addSeparator();
    m.addActions({m_actRename, m_actProps, m_actExtract, m_actDelete});
    QTreeWidget *t = qobject_cast<QTreeWidget *>(sender());
    m.exec((t ? t : m_tree)->viewport()->mapToGlobal(pos));
}

// ------------------------------------------------------------------ importing

void MainWindow::onDropped(const QStringList &paths, QTreeWidgetItem *target) {
    quint32 dir = targetDirForItem(target);
    if (!dropImages(PaneAdfs, paths, [this, dir](const QStringList &r) { importPaths(r, dir); })) importPaths(paths, dir);
}

bool MainWindow::isHardDisc() const {
    return m_img.isOpen() && m_info.kind == DiskInfo::Adfs && m_img.fullSize() > 655360;
}

MainWindow *MainWindow::openInNewWindow(const QString &path) {
    auto *w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
    if (w->openImage(path)) return w;
    w->close();
    return nullptr;
}

// Files dropped from the host that are disc images (as CP/M Disk Wrangler does). An empty window opens whatever is
// dropped on it: the first file here, the rest in new windows. A window holding a floppy opens disc images in new
// windows and adds the other files as usual. A hard disc may be where an image is meant to be kept, so it asks
// whether to open the images or copy them onto the disc as files. Returns true if it took the drop over; the work
// is queued, so no dialog or new window appears inside the drop handler. (Add files never opens images.)
bool MainWindow::dropImages(int pane, const QStringList &paths, std::function<void(const QStringList &)> importRest) {
    if (!m_img.isOpen()) {
        QTimer::singleShot(0, this, [this, paths] {
            for (const QString &p : paths) {
                if (QFileInfo(p).isDir() || p.endsWith(".inf", Qt::CaseInsensitive)) continue;
                if (!m_img.isOpen()) openImage(p);
                else openInNewWindow(p);
            }
        });
        return true;
    }
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
    QSettings s;
    QStringList f = QFileDialog::getOpenFileNames(this, "Add files to the image", s.value("addDir").toString());
    if (f.isEmpty()) return;
    s.setValue("addDir", QFileInfo(f.first()).absolutePath());
    if (dfsSide() >= 0) dfsImportPaths(dfsSide(), f);
    else if (dosActive()) dosImportPaths(f, dosTargetDir(m_dosTree->currentItem()));
    else importPaths(f, targetDirForItem(m_tree->currentItem()));
}

void MainWindow::addFolderDialog() {
    QSettings s;
    QString d = QFileDialog::getExistingDirectory(this, "Add folder to the image", s.value("addDir").toString());
    if (d.isEmpty()) return;
    s.setValue("addDir", d);
    if (dfsSide() >= 0) dfsImportPaths(dfsSide(), {d});
    else if (dosActive()) dosImportPaths({d}, dosTargetDir(m_dosTree->currentItem()));
    else importPaths({d}, targetDirForItem(m_tree->currentItem()));
}

bool MainWindow::isSidecar(const QString &p) const {
    return m_optReadInf->isChecked() && p.endsWith(".inf", Qt::CaseInsensitive) &&
           QFileInfo::exists(p.left(p.size() - 4));
}

void MainWindow::importPaths(const QStringList &paths, quint32 dir) {
    if (!m_img.isOpen()) {
        if (!paths.isEmpty()) openImage(paths.first());
        return;
    }
    if (!m_img.editable()) {
        QMessageBox::warning(this, "Read-only", "This image is read-only, so nothing can be added.");
        return;
    }
    ImportCtx c;
    for (const QString &p : paths) {
        if (c.cancelled) break;
        if (QFileInfo(p).isDir()) importDir(p, dir, c);
        else if (!isSidecar(p)) importFile(p, dir, c);
    }
    refreshTree();
    updateUi();

    QString msg = QString("Added %1 file(s) and %2 folder(s)").arg(c.files).arg(c.dirs);
    if (c.replaced) msg += QString(", replaced %1").arg(c.replaced);
    if (c.skipped) msg += QString(", skipped %1").arg(c.skipped);
    msg += c.errors.isEmpty() ? ". Not saved yet - press Ctrl+S to write to the image."
                              : QString(". %1 problem(s).").arg(c.errors.size());
    statusBar()->showMessage(msg, 10000);
    if (!c.errors.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, "Some items could not be added", msg, QMessageBox::Ok, this);
        box.setDetailedText(c.errors.join("\n"));
        box.exec();
    }
}

void MainWindow::importFile(const QString &path, quint32 dir, ImportCtx &c) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        c.errors << QString("%1: cannot read (%2)").arg(path, f.errorString());
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    QString name = sanitizeName(QFileInfo(path).fileName());
    quint32 load = 0, exec = 0;
    quint8 flags = FlagR | FlagW;
    bool haveInf = false;
    if (m_optReadInf->isChecked()) {
        InfData inf;
        if (readInf(path, &inf)) {
            haveInf = true;
            if (!inf.name.isEmpty()) name = sanitizeName(inf.name);
            load = inf.load; exec = inf.exec; flags = inf.flags;
        }
    }
    addOne(dir, name, data, load, exec, flags, haveInf, path, c);
}

void MainWindow::addOne(quint32 dir, QString name, const QByteArray &data, quint32 load, quint32 exec,
                        quint8 flags, bool haveAttrs, const QString &src, ImportCtx &c) {
    QString err;
    Dir d;
    if (!m_img.readDir(dir, &d, &err)) { c.errors << QString("%1: %2").arg(src, err); return; }
    if (c.created.contains(keyFor(dir, name))) name = uniqueName(name, d, c, dir);   // e.g. truncation clash
    int idx = d.indexOf(name);
    bool replace = false;
    if (idx >= 0) {
        const Entry ex = d.entries[idx];
        if (ex.isDir()) {
            c.errors << QString("%1: a directory called '%2' already exists there").arg(src, name);
            return;
        }
        int pol = c.policy;
        if (pol == 0) {
            auto b = QMessageBox::question(
                this, "File already exists",
                QString("'%1' already exists in the target directory%2.\nReplace it with\n%3 ?")
                    .arg(name, ex.flag(Entry::L) ? " and is LOCKED" : "", src),
                QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll | QMessageBox::Cancel,
                QMessageBox::No);
            if (b == QMessageBox::Cancel) { c.cancelled = true; return; }
            if (b == QMessageBox::YesToAll) c.policy = pol = 1;
            else if (b == QMessageBox::NoToAll) c.policy = pol = 2;
            else pol = (b == QMessageBox::Yes) ? 1 : 2;
        }
        if (pol == 2) { c.skipped++; return; }
        replace = true;
        if (!haveAttrs) {       // keep the existing file's attributes and addresses
            load = ex.load; exec = ex.exec;
            flags = quint8((ex.flag(Entry::R) ? FlagR : 0) | (ex.flag(Entry::W) ? FlagW : 0) |
                           (ex.flag(Entry::L) ? FlagL : 0) | (ex.flag(Entry::E) ? FlagE : 0));
        }
    }
    if (!m_img.addFile(dir, name, data, load, exec, flags, replace, &err)) {
        c.errors << QString("%1: %2").arg(src, err);
        return;
    }
    c.created.insert(keyFor(dir, name));
    if (replace) c.replaced++; else c.files++;
}

void MainWindow::importDir(const QString &path, quint32 parent, ImportCtx &c) {
    QFileInfo fi(path);
    QString name = sanitizeName(fi.fileName().isEmpty() ? QString("_") : fi.fileName());
    QString err;
    Dir d;
    if (!m_img.readDir(parent, &d, &err)) { c.errors << QString("%1: %2").arg(path, err); return; }
    if (c.created.contains(keyFor(parent, name))) name = uniqueName(name, d, c, parent);
    int idx = d.indexOf(name);
    quint32 sector = 0;
    if (idx >= 0) {
        if (!d.entries[idx].isDir()) {
            c.errors << QString("%1: a file called '%2' already exists there").arg(path, name);
            return;
        }
        sector = d.entries[idx].sin;           // merge into the existing directory
    } else {
        if (!m_img.makeDir(parent, name, &err, &sector)) { c.errors << QString("%1: %2").arg(path, err); return; }
        c.dirs++;
    }
    c.created.insert(keyFor(parent, name));
    const QFileInfoList list = QDir(path).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &e : list) {
        if (c.cancelled) return;
        if (e.isDir()) importDir(e.absoluteFilePath(), sector, c);
        else if (!isSidecar(e.absoluteFilePath())) importFile(e.absoluteFilePath(), sector, c);
    }
}

// ----------------------------------------------------------- new / delete / props

void MainWindow::newDirectory() {
    if (dfsSide() >= 0) { statusBar()->showMessage("DFS discs have no directories.", 4000); return; }
    bool ok = false;
    QString n = QInputDialog::getText(this, "New directory", "Name (max 10 characters):", QLineEdit::Normal, "", &ok);
    if (!ok || n.isEmpty()) return;
    if (dosActive()) { dosNewDir(n.trimmed()); return; }
    QString err;
    if (!m_img.makeDir(targetDirForItem(m_tree->currentItem()), n.trimmed(), &err)) {
        QMessageBox::warning(this, "Cannot create directory", err);
        return;
    }
    refreshTree(); updateUi();
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

void MainWindow::deleteSelected() {
    if (dfsSide() >= 0) { dfsDelete(dfsSide()); return; }
    if (dosActive()) { dosDelete(); return; }
    QVector<Sel> sel = selectedObjects(), todo;
    for (const Sel &s : sel) {          // skip items whose ancestor is also being deleted
        bool covered = false;
        for (QTreeWidgetItem *p = s.item->parent(); p; p = p->parent())
            if (p->isSelected()) { covered = true; break; }
        if (!covered) todo.append(s);
    }
    if (todo.isEmpty()) return;
    int files = 0, dirs = 0, locked = 0;
    for (const Sel &s : todo) {
        if (s.isDir) { ++dirs; countTree(s.sector, files, dirs, locked, 0); }
        else ++files;
        if (s.item->text(1).contains('L')) ++locked;
    }
    QString msg = QString("Delete %1 file(s) and %2 director%3?").arg(files).arg(dirs).arg(dirs == 1 ? "y" : "ies");
    if (locked) msg += QString("\n\n%1 of these are LOCKED.").arg(locked);
    if (QMessageBox::question(this, "Delete", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QStringList errors;
    for (const Sel &s : todo) {
        QString err;
        if (!m_img.deleteEntry(s.parent, s.name, &err)) errors << QString("%1: %2").arg(s.name, err);
    }
    refreshTree(); updateUi();
    if (!errors.isEmpty()) QMessageBox::warning(this, "Delete", errors.join("\n"));
}

void MainWindow::editProperties() {
    if (dfsSide() >= 0) { dfsProps(dfsSide()); return; }
    if (dosActive()) { dosProps(); return; }
    QVector<Sel> sel = selectedObjects();
    if (sel.isEmpty()) return;
    QVector<Entry> ents;
    QString err;
    for (const Sel &s : sel) {
        Dir d;
        if (!m_img.readDir(s.parent, &d, &err) || d.indexOf(s.name) < 0) {
            QMessageBox::warning(this, "Properties", err.isEmpty() ? "Entry not found" : err);
            return;
        }
        ents.append(d.entries[d.indexOf(s.name)]);
    }
    bool single = ents.size() == 1;
    bool allDirs = true, anyDir = false;
    for (const Entry &e : ents) { allDirs = allDirs && e.isDir(); anyDir = anyDir || e.isDir(); }

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
    if (dlg.exec() != QDialog::Accepted) return;

    QStringList errors;
    for (int i = 0; i < ents.size(); ++i) {
        Entry e = ents[i];
        if (nameEd && nameEd->text().trimmed() != e.name()) e.setName(nameEd->text().trimmed());
        auto apply = [&](QCheckBox *cb, int idx) {
            if (cb->checkState() != Qt::PartiallyChecked) e.setFlag(idx, cb->checkState() == Qt::Checked);
        };
        apply(cbR, Entry::R); apply(cbW, Entry::W); apply(cbL, Entry::L);
        if (!e.isDir()) apply(cbE, Entry::E);
        if (loadEd) e.load = loadEd->text().toUInt(nullptr, 16);
        if (execEd) e.exec = execEd->text().toUInt(nullptr, 16);
        if (!m_img.replaceEntry(sel[i].parent, sel[i].name, e, &err)) errors << QString("%1: %2").arg(sel[i].name, err);
    }
    (void)anyDir;
    refreshTree(); updateUi();
    if (!errors.isEmpty()) QMessageBox::warning(this, "Properties", errors.join("\n"));
}

// ------------------------------------------------------------------ extracting

void MainWindow::extractEntry(quint32 parent, const Entry &e, const QString &hostDir, QStringList &errors) {
    QString host = hostSafeName(e.name());
    if (e.isDir()) {
        QString sub = hostDir + "/" + host;
        if (!QDir().mkpath(sub)) { errors << "cannot create folder " + sub; return; }
        Dir d;
        QString err;
        if (!m_img.readDir(e.sin, &d, &err)) { errors << QString("%1: %2").arg(e.name(), err); return; }
        for (const Entry &c : d.entries) extractEntry(e.sin, c, sub, errors);
        return;
    }
    (void)parent;
    QByteArray data;
    QString err;
    if (!m_img.readFile(e, &data, &err)) { errors << QString("%1: %2").arg(e.name(), err); return; }
    QFile f(hostDir + "/" + host);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
        errors << QString("%1: %2").arg(host, f.errorString());
        return;
    }
    f.close();
    if (m_optWriteInf->isChecked()) {
        QFile inf(hostDir + "/" + host + ".inf");
        if (inf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            unsigned attr = (e.flag(Entry::R) ? 1 : 0) | (e.flag(Entry::W) ? 2 : 0) | (e.flag(Entry::E) ? 4 : 0) |
                            (e.flag(Entry::L) ? 8 : 0);
            inf.write(QString::asprintf("%-10s %08X %08X %08X %02X\n", qPrintable(e.name()), e.load, e.exec,
                                        e.length, attr).toLatin1());
        }
    }
}

void MainWindow::extractSelected() {
    if (!m_img.isOpen()) return;
    if (dfsSide() >= 0) { dfsExtract(dfsSide()); return; }
    if (dosActive()) { dosExtract(); return; }
    QSettings s;
    QString dest = QFileDialog::getExistingDirectory(this, "Extract to folder", s.value("extractDir").toString());
    if (dest.isEmpty()) return;
    s.setValue("extractDir", dest);

    QVector<Sel> sel = selectedObjects();
    QVector<Entry> list;
    QString err;
    if (sel.isEmpty()) {            // nothing selected: extract the whole disc
        Dir root;
        if (!m_img.readDir(RootSector, &root, &err)) { QMessageBox::warning(this, "Extract", err); return; }
        list = root.entries;
        sel.resize(list.size());
        for (Sel &x : sel) x.parent = RootSector;
    } else {
        for (const Sel &x : sel) {
            Dir d;
            if (m_img.readDir(x.parent, &d, &err) && d.indexOf(x.name) >= 0) list.append(d.entries[d.indexOf(x.name)]);
        }
    }
    for (const Entry &e : list)
        if (QFileInfo::exists(dest + "/" + hostSafeName(e.name()))) {
            if (QMessageBox::question(this, "Extract", "Some files already exist in that folder. Overwrite them?") !=
                QMessageBox::Yes)
                return;
            break;
        }
    QStringList errors;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int i = 0; i < list.size(); ++i) extractEntry(sel[i].parent, list[i], dest, errors);
    QApplication::restoreOverrideCursor();
    if (errors.isEmpty()) statusBar()->showMessage(QString("Extracted %1 item(s) to %2").arg(list.size()).arg(dest), 6000);
    else QMessageBox::warning(this, "Extract", errors.join("\n"));
}
