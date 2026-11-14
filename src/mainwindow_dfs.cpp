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
// mainwindow_dfs.cpp - DFS panes, tab management, window registry and copying between discs.
#include "mainwindow.h"
#include <diskwrangler/app/appsettings.h>
#include "mainwindow_p.h"
#include "viewer.h"
#include "dragout.h"
#include <diskwrangler/fs/dfs/dfsvolume.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QMimeData>
#include <QUrl>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QPair>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <algorithm>

using namespace Adfs;

// ---------------------------------------------------------------------- panes

// The DFS tab shows both sides of a .dsd in one tree, a root row per side ("Drive 0", "Drive 2"), as CP/M Disk
// Wrangler shows a disc with a filesystem on each side; every row carries its side (RoleSide, and RoleParent for drags).
int MainWindow::dfsSide() const {
    if (m_tabs->currentWidget() != m_dfsTree || !(m_doc.dfs(0).attached() || m_doc.dfs(1).attached())) return -1;
    QList<QTreeWidgetItem *> sel = m_dfsTree->selectedItems();
    return dfsTargetSide(!sel.isEmpty() ? sel.first() : m_dfsTree->currentItem());
}

int MainWindow::dfsTargetSide(QTreeWidgetItem *it) const {
    int s = it ? it->data(0, RoleSide).toInt() : 0;
    if (s == 1 && m_doc.dfs(1).attached()) return 1;
    return m_doc.dfs(0).attached() ? 0 : (m_doc.dfs(1).attached() ? 1 : 0);
}

QTreeWidgetItem *MainWindow::dfsRoot(int side) const {
    for (int i = 0; i < m_dfsTree->topLevelItemCount(); ++i)
        if (m_dfsTree->topLevelItem(i)->data(0, RoleSide).toInt() == side) return m_dfsTree->topLevelItem(i);
    return nullptr;
}

bool MainWindow::dfsEditable(int s) const { return m_img.editable() && m_doc.dfs(s).attached() && m_doc.dfsOk(s); }

int MainWindow::dropPane(const DropTree *t, QTreeWidgetItem *it) const {
    if (t == m_dfsTree) return dfsTargetSide(it) ? PaneDfs1 : PaneDfs0;
    return t->pane();
}

bool MainWindow::paneEditable(int pane) const {
    switch (pane) {
    case PaneAdfs: return m_img.editable() && m_img.isAdfs() && !m_img.adfsLocked();
    case PaneDos: return m_dos.attached() && dosEditable();
    default: return dfsEditable(pane == PaneDfs1 ? 1 : 0);
    }
}

quint32 MainWindow::dropTarget(int pane, QTreeWidgetItem *it) const {
    if (pane == PaneAdfs) return targetDirForItem(it);
    if (pane == PaneDos) return dosTargetDir(it);
    return 0;
}

// ----------------------------------------------------------------- DFS tree

// The DFS pane's rows again (the window kit's pane: the selection, the current row and closed sides are kept)
void MainWindow::refreshDfs() { refreshPane(m_dfsTree); }

QVector<MainWindow::FSel> MainWindow::dfsSelected() const {
    QVector<FSel> v;
    for (QTreeWidgetItem *it : m_dfsTree->selectedItems()) {
        if (it->data(0, RoleIsRoot).toBool()) continue;
        QString full = it->data(0, RoleName).toString();
        if (full.size() < 3) continue;
        v.append({dfsTargetSide(it), full[0], full.mid(2), it});
    }
    return v;
}

// ----------------------------------------------------------------- importing

void MainWindow::dfsImportPaths(int side, const QStringList &paths) {
    if (!dfsEditable(side)) {
        QMessageBox::warning(this, "Read-only", "This DFS drive is read-only (image read-only, or its catalogue has errors).");
        return;
    }
    hostImport(side ? PaneDfs1 : PaneDfs0, 0, paths, m_optReadInf->isChecked());
}

// ------------------------------------------------- delete / props / rename / extract

// The files of the selection a move into directory letter would rename (indexes into sel), the rest that would clash
// listed in *clashes ("$.NAME: D.NAME is taken"): a name taken there by a file not being moved, or by another selected
// file going to the same place. Files already in that directory stay.
QVector<int> MainWindow::planDfsMove(const QVector<FSel> &sel, QChar letter, QStringList *clashes) const {
    auto key = [](int side, QChar dir, const QString &name) {
        return QString("F%1:%2.%3").arg(side).arg(QString(dir.toUpper())).arg(name.toUpper());
    };
    clashes->clear();
    QSet<QString> mine, taken;
    for (const FSel &s : sel) {
        mine << key(s.side, s.dir, s.name);
        if (s.dir == letter) taken << key(s.side, s.dir, s.name);       // stays where it is
    }
    QVector<int> todo;
    for (int k = 0; k < sel.size(); ++k) {
        const FSel &s = sel[k];
        if (s.dir == letter) continue;
        QString to = key(s.side, letter, s.name);
        const dw::dfs::Disc &d = m_doc.dfs(s.side);
        int i = d.indexOf(letter, s.name);
        bool occupied = i >= 0 && !mine.contains(key(s.side, d.files()[i].dir, d.files()[i].name));
        if (occupied || taken.contains(to)) {
            *clashes << QString("%1.%2: %3.%2 is taken%4").arg(QString(s.dir), s.name, QString(letter))
                            .arg(m_doc.dfs(1).attached() ? QString(" (drive %1)").arg(s.side * 2) : QString());
            continue;
        }
        taken << to;
        todo << k;
    }
    return todo;
}

// A DFS drive's Properties (step 5l, on the kit's pattern: this dialog says what changes, the kit applies each file's
// change on its own and lists what failed). One file: its name, letter, lock and addresses. Several: the lock, and a
// letter to move them all into (asking first when some names are taken there).
bool MainWindow::dfsProperties(const QVector<Item> &items, PropertiesEdit *edit) {
    QVector<dw::dfs::File> files;
    QVector<int> sides;
    for (const Item &it : items) {
        const int side = paneOf(it.mount) == PaneDfs1 ? 1 : 0;
        QChar dir;
        QString name;
        dw::dfs::splitName(it.path.name(), &dir, &name);
        int i = m_doc.dfs(side).indexOf(dir, name);
        if (i < 0) { QMessageBox::warning(this, "Properties", "Entry not found"); return false; }
        files.append(m_doc.dfs(side).files()[i]);
        sides.append(side);
    }
    bool single = files.size() == 1;
    QDialog dlg(this);
    dlg.setWindowTitle(single ? "Properties - " + files[0].fullName() : QString("Properties - %1 files").arg(files.size()));
    auto *form = new QFormLayout(&dlg);
    QLineEdit *nameEd = nullptr, *dirEd = nullptr, *loadEd = nullptr, *execEd = nullptr;
    if (single) {
        nameEd = new QLineEdit(files[0].name);
        nameEd->setMaxLength(dw::dfs::MaxName);
        dirEd = new QLineEdit(QString(files[0].dir));
        dirEd->setMaxLength(1);
        form->addRow("Name (max 7 characters):", nameEd);
        form->addRow("Directory letter:", dirEd);
    } else {                                    // several files: move them all into one directory (a rename)
        dirEd = new QLineEdit;
        dirEd->setMaxLength(1);
        dirEd->setPlaceholderText("unchanged");
        dirEd->setToolTip("Puts every selected file into this DFS directory, keeping its name");
        form->addRow("Directory letter:", dirEd);
    }
    auto *cbL = new QCheckBox("L  - locked");
    int on = 0;
    for (const dw::dfs::File &f : files) on += f.locked ? 1 : 0;
    if (on == 0) cbL->setCheckState(Qt::Unchecked);
    else if (on == files.size()) cbL->setCheckState(Qt::Checked);
    else { cbL->setTristate(true); cbL->setCheckState(Qt::PartiallyChecked); }
    form->addRow("Attributes:", cbL);
    if (single) {
        auto *v = new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]{1,8}"), &dlg);
        loadEd = new QLineEdit(dw::dfs::addrText(files[0].load));
        execEd = new QLineEdit(dw::dfs::addrText(files[0].exec));
        loadEd->setValidator(v);
        execEd->setValidator(v);
        loadEd->setToolTip("18-bit DFS address: up to 3FFFF; FFxxxx (or FFFFxxxx) is the I/O processor, the same as 3xxxx");
        execEd->setToolTip(loadEd->toolTip());
        form->addRow("Load address (hex):", loadEd);
        form->addRow("Exec address (hex):", execEd);
        form->addRow("Length:", new QLabel(QString("%1 bytes").arg(files[0].length)));
        form->addRow("Start sector:", new QLabel(QString::number(files[0].start)));
        if (m_doc.dfs(1).attached()) form->addRow("Drive:", new QLabel(QString::number(sides[0] * 2)));
    }
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;


    // DFS keeps 18 bits: accept up to 3FFFF, or FFxxxx / FFFFxxxx for 3xxxx; nothing else
    quint32 newLoad = 0, newExec = 0;
    for (QLineEdit *ed : {loadEd, execEd}) {
        if (ed && !dw::dfs::parseAddr(ed->text(), ed == loadEd ? &newLoad : &newExec)) {
            QMessageBox::warning(this, "Properties", QString("%1 is not a DFS address: use up to 3FFFF, or FFxxxx "
                                                         "(the same as 3xxxx).").arg(ed->text().toUpper()));
            return false;
        }
    }
    QChar letter = dirEd && !dirEd->text().isEmpty() ? dirEd->text()[0] : QChar();
    if (!letter.isNull() && !dw::dfs::validDir(letter)) {
        QMessageBox::warning(this, "Properties", QString("'%1' is not a valid DFS directory character.").arg(letter));
        return false;
    }
    edit->eachOnItsOwn = true;
    for (int i = 0; i < files.size(); ++i) {
        const dw::dfs::File &f = files[i];
        QString nn = nameEd ? nameEd->text().trimmed() : f.name;
        QChar nd = single && !letter.isNull() ? letter : f.dir;     // several files: moved below, all checked first
        bool locked = cbL->checkState() == Qt::PartiallyChecked ? f.locked : cbL->checkState() == Qt::Checked;
        quint32 load = (loadEd ? newLoad : f.load) & 0x3FFFF;
        quint32 exec = (execEd ? newExec : f.exec) & 0x3FFFF;
        PropertyChange c;
        c.item = items[i];
        const bool moves = nd != f.dir || nn != f.name;
        // every file is written, as the dialog always did; a rename alone is one write (the attributes go with it)
        if (!moves || locked != f.locked || load != f.load || exec != f.exec)
            c.attrs = {{"locked", locked}, {"load", load}, {"exec", exec}};
        if (moves) c.to = dw::fs::Path({QString(nd) + "." + nn});
        edit->changes << c;
    }
    if (!single && !letter.isNull()) {
        QVector<FSel> sel;
        for (int i = 0; i < files.size(); ++i) sel.append({sides[i], files[i].dir, files[i].name, nullptr});
        QStringList clashes;
        const QVector<int> todo = planDfsMove(sel, letter, &clashes);
        bool move = clashes.isEmpty();
        if (!clashes.isEmpty()) {
            int n = sel.size();
            if (clashes.size() >= n) {
                QMessageBox::warning(this, "Properties", QString("No file was moved into %1: the names are taken.\n\n").arg(letter) +
                                                             clashes.join("\n"));
            } else {
                QMessageBox box(QMessageBox::Question, "Properties",
                                QString("%1 of the %2 files can't go into %3, as the name is taken there:\n\n%4\n\nMove the others?")
                                    .arg(clashes.size()).arg(n).arg(letter).arg(clashes.join("\n")),
                                QMessageBox::Yes | QMessageBox::Cancel, this);
                box.button(QMessageBox::Yes)->setText("Move the others");
                move = box.exec() == QMessageBox::Yes;
            }
        }
        if (move)
            for (int k : todo) edit->changes[k].to = dw::fs::Path({QString(letter) + "." + files[k].name});
    }
    return true;
}

// A DFS file's change that failed: "D.NAME: <why>", as the dialog always said it (a side that can't be changed, and an
// empty new name, in its words).
QString MainWindow::propertiesItemError(const PropertyChange &c, const QString &err) const {
    const int pane = paneOf(c.item.mount);
    if (pane == PaneDfs0 || pane == PaneDfs1) {
        if (err.startsWith("read-only: ")) return c.item.path.name() + ": this side is read-only";
        if (c.moves() && c.to.name().size() == 2 && c.to.name()[1] == '.') {
            QString why;
            dw::dfs::validName(QString(), &why);
            return c.item.path.name() + ": '': " + why;
        }
    }
    return DiscWindow::propertiesItemError(c, err);
}

// Moves the selected DFS files (either side) into directory `letter`, keeping their names: on DFS that is only a
// rename. A name taken there - by a file not being moved, or by another selected file going to the same place - is
// a clash, listed in *clashes ("$.NAME: D.NAME is taken"); with any clash nothing is moved unless moveOthers is set,
// when the rest are. Files already in that directory stay (a change of the letter's case only is made). Returns the
// number moved. Each move is the kit's change (applyProperties), on its own.
int MainWindow::dfsMoveSelected(QChar letter, bool moveOthers, QStringList *clashes, QStringList *errors) {
    const QVector<FSel> sel = dfsSelected();
    const QVector<int> todo = planDfsMove(sel, letter, clashes);
    if (!clashes->isEmpty() && !moveOthers) return 0;
    int moved = 0;
    for (int k : todo) {
        const FSel &s = sel[k];
        int i = m_doc.dfs(s.side).indexOf(s.dir, s.name);
        QString err = "not found";
        if (!dfsEditable(s.side)) err = "this side is read-only";
        else if (i >= 0) {
            PropertyChange c;
            c.item = Item(m_doc.dfsMount(s.side), dw::fs::Path({QString(s.dir) + "." + s.name}));
            c.to = dw::fs::Path({QString(letter) + "." + s.name});
            if (applyProperties({c}, &err)) { ++moved; continue; }
        }
        *errors << QString("%1.%2: %3").arg(QString(s.dir), s.name, err);
    }
    return moved;
}

void MainWindow::dfsRename() {
    QList<QTreeWidgetItem *> items = m_dfsTree->selectedItems();
    if (items.size() == 1 && items[0]->data(0, RoleIsRoot).toBool()) { editVolume(m_doc.dfsMount(dfsTargetSide(items[0])), "title"); return; }
    QVector<FSel> sel = dfsSelected();
    if (sel.size() != 1) { statusBar()->showMessage("Select a single file to rename.", 4000); return; }
    int side = sel[0].side;
    bool ok = false;
    QString n = QInputDialog::getText(this, "Rename", "New name (NAME, or D.NAME to change the directory letter too):",
                                      QLineEdit::Normal, sel[0].name, &ok).trimmed();
    if (!ok || n.isEmpty()) return;
    QChar nd = sel[0].dir;
    QString nn = n;
    if (n.size() >= 3 && n[1] == '.') { nd = n[0]; nn = n.mid(2); }
    if (m_doc.dfs(side).indexOf(sel[0].dir, sel[0].name) < 0) return;
    renameRow(dw::ui::VolumePane::rowOf(sel[0].item), dw::fs::Path({QString(nd) + "." + nn}));
    updateUi();
}

// ================================================ copying between discs

// Copying out, on the kit's pattern (5d's CP/M exportForCopy): the items are read by exportItems, then what this
// program's copies have always carried is put right: a date only where the tool showed one (ADFS files, 1981-2100; FAT;
// never DFS), as a local date and time.
static void copyDates(dw::TransferItem *t) {
    const QDateTime w = t->when;
    t->when = QDateTime();
    const bool plausible = w.isValid() && w.date().year() >= 1981 && w.date().year() <= 2100;
    if (t->sourceFs == "fat" ? w.isValid() : (t->sourceFs == "adfs" && !t->isDir && plausible)) t->when = QDateTime(w.date(), w.time());
    for (dw::TransferItem &k : t->kids) copyDates(&k);
}

bool MainWindow::exportForCopy(const QVector<Item> &items, QVector<dw::TransferItem> *out, QString *err) const {
    if (!exportItems(items, out, err)) return false;
    for (dw::TransferItem &t : *out) copyDates(&t);
    return true;
}

// A drag's reference as the kit's address: the row the drag was made from, or (a reference that came in a drop's
// payload) the row of the source tree it names, or for a DFS file its name in its side.
bool MainWindow::refItems(int pane, const QVector<DragRef> &refs, QVector<Item> *out, QString *err) const {
    out->clear();
    for (const DragRef &r : refs) {
        if (r.mount >= 0) { out->append(Item(r.mount, r.path)); continue; }
        if (pane == PaneDfs0 || pane == PaneDfs1) {         // a DFS row's parent is its side (one tree shows both)
            if (r.name.size() < 3) { *err = "bad item"; return false; }
            out->append(Item(m_doc.dfsMount(pane == PaneDfs1 || r.parent == 1 ? 1 : 0), dw::fs::Path({r.name})));
            continue;
        }
        QTreeWidget *t = pane == PaneDos ? static_cast<QTreeWidget *>(m_dosTree) : static_cast<QTreeWidget *>(m_tree);
        QList<QTreeWidgetItem *> todo;
        for (int i = 0; i < t->topLevelItemCount(); ++i) todo << t->topLevelItem(i);
        bool found = false;
        while (!todo.isEmpty() && !found) {
            QTreeWidgetItem *it = todo.takeFirst();
            if (!it->data(0, RoleIsRoot).toBool() && it->data(0, RoleParent).toUInt() == r.parent &&
                it->data(0, RoleName).toString() == r.name) {
                const dw::ui::PaneRow row = dw::ui::VolumePane::rowOf(it);
                if (row.valid()) { out->append(Item(row.mount, row.path)); found = true; }
            }
            for (int i = 0; i < it->childCount(); ++i) todo << it->child(i);
        }
        if (!found) { *err = QString("'%1' not found").arg(r.name); return false; }
    }
    return true;
}

bool MainWindow::exportRefs(int pane, const QVector<DragRef> &refs, QVector<dw::TransferItem> *out, QString *err) {
    QVector<Item> items;
    if (!refItems(pane, refs, &items, err)) return false;
    return exportForCopy(items, out, err);
}

// ================================================ importing, on the kit's import session (step 5l)
//
// Every import ends in importAll, which runs the kit's DiscWindow::importItems (an ImportSession: the core's Importer,
// one transaction per file, nothing undone) with this program's hooks. The rules are the ones this window has always
// had, per pane: names, attributes and the DFS letter rules (placeDfsLetter for ADFS, the single-letter directory
// question for DFS), the "File already exists" questions and the summary with its "Some items could not be added" box,
// every text as before. Host files are read by the core's one host reader (hostImport). What an item is, the hooks ask
// of the item: hostPath set = a host file (its .inf, if any, read again for the numbers, as Add files always did),
// otherwise a file of a disc, read as the old code read it (asX). m_run holds what one import needs besides.

// A file or directory of a disc as the import hooks read it: the attributes of whichever family the core's item has
// ("acorn" for ADFS and DFS, "fat"), the DFS letter taken off its name.
struct MainWindow::XItem {
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
    QDateTime when;             // the date the source records (invalid if none)
};

// The item as this program's copy code reads it, and for a DFS file copied in this program (a drop from one of its
// windows, its clipboard) the letter put back when it is one the disc shouldn't have.
MainWindow::XItem MainWindow::asX(const dw::TransferItem &t) const {
    XItem x;
    x.isDir = t.isDir;
    x.name = t.name;
    x.data = t.data;
    x.when = t.when;
    const QString &fs = t.sourceFs;
    x.src = fs == "adfs" ? PaneAdfs : fs == "dfs" ? PaneDfs0 : PaneDos;      // anything else: plain host files
    const QJsonObject acorn = t.attrs.value("acorn");
    const QString access = acorn["access"].toString();
    if (x.src == PaneAdfs || x.src == PaneDfs0) {
        x.hasAddr = !x.isDir && RetroFiles::parseHex(acorn["load"], &x.load) && RetroFiles::parseHex(acorn["exec"], &x.exec);
        x.locked = access.contains('L');       // case-sensitive: lower case letters are public access
        x.adfsFlags = quint8((access.contains('R') ? Adfs::FlagR : 0) | (access.contains('W') ? Adfs::FlagW : 0) |
                             (access.contains('L') ? Adfs::FlagL : 0) | (access.contains('E') ? Adfs::FlagE : 0));
        if (x.src == PaneDfs0) {                // "D.NAME": the directory is part of a DFS name
            if (x.name.size() >= 3 && x.name[1] == '.' && dw::dfs::validDir(x.name[0])) {
                x.dfsDir = x.name[0];
                x.name = x.name.mid(2);
            } else {
                x.dfsDir = '$';
            }
        }
    } else {
        const QJsonObject fat = t.attrs.value("fat");
        x.locked = fat["readonly"].toBool() || access.contains('L');
        for (auto ns = t.attrs.begin(); ns != t.attrs.end(); ++ns)          // any namespace's "readonly" (e.g. cpm)
            if (ns.value()["readonly"].toBool()) x.locked = true;
        x.dosAttr = quint8((x.locked ? dw::fat::AttrR : 0) | (fat["hidden"].toBool() ? dw::fat::AttrH : 0) |
                           (fat["system"].toBool() ? dw::fat::AttrS : 0) | (fat["archive"].toBool() ? dw::fat::AttrA : 0));
    }
    if (m_run.internal && t.sourceFs == "dfs") {
        x.src = PaneDfs0;
        if (x.dfsDir == '$' && t.name.size() >= 3 && t.name[1] == '.' && t.name[0] != '$') {
            x.dfsDir = t.name[0];
            x.name = t.name.mid(2);
        }
    }
    return x;
}

// A file of an ADFS directory the single-letter question put into a DFS letter carries the letter in front of its name,
// after a mark that no name has (DirChoice::Into's prefix): the letter (null if none) and the item without it.
static const QChar LetterMark(1);
static QChar takeLetter(dw::TransferItem *t) {
    if (t->name.size() < 2 || t->name[0] != LetterMark) return QChar();
    const QChar l = t->name[1];
    t->name = t->name.mid(2);
    return l;
}

// How messages name an item: a host file by its path, a disc's file by its name there (a DFS file without its letter).
QString MainWindow::importSrc(const dw::TransferItem &item) const {
    if (!item.hostPath.isEmpty()) return item.hostPath;
    dw::TransferItem t = item;
    takeLetter(&t);
    return asX(t).name;
}

// The Importer's "from" of each item (froms[i], then "/kid" for a folder's contents) -> how this program names it.
void MainWindow::mapImportItem(const QString &from, const dw::TransferItem &t) {
    m_run.src[from] = importSrc(t);
    m_run.isDir[from] = t.isDir;
    if (t.hostPath.isEmpty() && !t.isDir) {               // placeDfsLetter named a DFS file "D.NAME"
        const XItem x = asX(t);
        if (x.src == PaneDfs0 || x.src == PaneDfs1) m_run.letterSrc[from] = QString("%1.%2").arg(QString(x.dfsDir), x.name);
    }
    for (const dw::TransferItem &k : t.kids) mapImportItem(from + "/" + k.name, k);
}

// A sector (ADFS) or cluster (DOS) the window's rows give, as the kit's path: found by walking the disc's directories.
dw::fs::Path MainWindow::dirPath(int pane, quint32 dir) const {
    if (pane == PaneAdfs ? dir == RootSector : (pane != PaneDos || dir == 0)) return dw::fs::Path();
    QVector<QPair<quint32, dw::fs::Path>> todo{{pane == PaneAdfs ? quint32(RootSector) : 0u, dw::fs::Path()}};
    QSet<quint32> seen;
    while (!todo.isEmpty()) {
        const QPair<quint32, dw::fs::Path> at = todo.takeFirst();
        if (seen.contains(at.first) || at.second.parts.size() > 64) continue;
        seen.insert(at.first);
        QString err;
        if (pane == PaneAdfs) {
            Dir d;
            if (!m_img.readDir(at.first, &d, &err)) continue;
            for (const Entry &e : d.entries) {
                if (!e.isDir()) continue;
                if (e.sin == dir) return at.second.child(e.name());
                todo.append({e.sin, at.second.child(e.name())});
            }
        } else {
            QVector<dw::fat::DEntry> l;
            if (!m_dos.listDir(at.first, &l, &err)) continue;
            for (const dw::fat::DEntry &e : l) {
                if (!e.isDir() || e.name == "." || e.name == "..") continue;
                if (e.cluster == dir) return at.second.child(e.name);
                todo.append({e.cluster, at.second.child(e.name)});
            }
        }
    }
    return dw::fs::Path();
}

void MainWindow::hostImport(int pane, quint32 dir, const QStringList &paths, bool useInf) {
    QVector<dw::xfer::HostReadError> problems;
    const QVector<dw::TransferItem> items = dw::xfer::readHost(paths, acornHostRead(useInf), &problems);
    importAll(pane, dir, items, problems, false, false);
}

// One import into a pane: what couldn't be read first ("<path>: cannot read (<reason>)"), then the items through the
// kit's session. internal: the items were copied in this program; refreshAll: every pane is made again before the
// summary (the drops between discs, retro-files drops and pastes always did that).
void MainWindow::importAll(int pane, quint32 dir, const QVector<dw::TransferItem> &items,
                           const QVector<dw::xfer::HostReadError> &readProblems, bool internal, bool refreshAll) {
    m_run = ImportRun();
    m_run.pane = pane;
    m_run.internal = internal;
    m_run.refreshAll = refreshAll;
    QVector<dw::xfer::ImportError> readErrors;
    for (const dw::xfer::HostReadError &p : readProblems) {
        dw::xfer::ImportError e;
        e.kind = dw::xfer::ImportError::Failed;
        e.message = e.why = QString("%1: cannot read (%2)").arg(p.path, p.reason);
        e.from = p.path;
        readErrors << e;
        m_run.problems << e.message;
    }
    m_run.readErrors = readErrors.size();
    QStringList froms;
    for (int i = 0; i < items.size(); ++i) {
        froms << QString::number(i);
        mapImportItem(froms[i], items[i]);
    }
    importItems(mountOf(pane), dirPath(pane, dir), items, froms, readErrors);
}

// A DFS file letter.NAME added under an ADFS directory: in a subdirectory named after the letter (its case kept, one
// already there in either case reused), or (letters ADFS can't have) as X_NAME with a warning, or with Tools > "DFS
// directories become ADFS directories" off, as NAME (DiskWranglerCore's dw::dfs::placeLetter).
void MainWindow::placeDfsLetter(QChar letter, dw::fs::Placement *p) {
    const dw::dfs::LetterPlace place = dw::dfs::placeLetter(letter, p->name, !m_optLetterDirs->isChecked(),
                                                            [](QChar ch) { return isValidNameChar(ch); });
    if (place.how == dw::dfs::LetterPlace::Flat) {
        p->name = sanitizeName(place.name);
        m_run.problems << place.warning;
    }
    if (place.how == dw::dfs::LetterPlace::Subdir) p->dir = p->dir.child(place.subdir);
}

// The words of the summary for an error the Importer met (the old code's texts).
QString MainWindow::importErrorText(const dw::xfer::ImportError &e) const {
    const QString src = m_run.src.value(e.from, e.from), letterSrc = m_run.letterSrc.value(e.from, src);
    const bool dir = m_run.isDir.value(e.from);
    const QString reason = e.message.startsWith(e.path + ": ") ? e.message.mid(e.path.size() + 2) : e.why;
    switch (e.cause) {
    case dw::xfer::ImportError::DirInTheWay:
        return QString("%1: a directory called '%2' already exists there").arg(src, e.name);
    case dw::xfer::ImportError::FileInTheWay:
        return dir ? QString("%1: a file called '%2' already exists there").arg(src, e.name)
                   : QString("%1: a file called '%2' is in the way of directory %2").arg(letterSrc, e.name);
    case dw::xfer::ImportError::MakeDir:
        return QString("%1: %2").arg(dir ? src : letterSrc, reason);
    case dw::xfer::ImportError::Write:
        return QString("%1: %2").arg(src, e.why);
    default:
        return e.message;
    }
}

dw::ui::ImportHooks MainWindow::importHooks(int mount) {
    dw::ui::ImportHooks h;
    h.options.unlockBeforeReplace = false;           // ADFS, DFS and FAT replace a locked / read-only file themselves
    h.errorHook = [this](const dw::xfer::ImportError &e) {
        m_run.problems << importErrorText(e);
        ++m_run.hooked;
    };
    const int pane = paneOf(mount);
    if (pane == PaneAdfs) {
        h.place = [this](const dw::TransferItem &it, const dw::fs::Path &target) {
            dw::fs::Placement p;
            p.dir = target;
            if (!it.hostPath.isEmpty()) {             // a host file: its name, or its .inf's (which may name a letter)
                QString n = it.name;
                QChar letter;
                if (it.sourceFs == "dfs") { letter = n[0]; n = n.mid(2); }
                p.name = sanitizeName(n);
                if (!letter.isNull() && !it.isDir) placeDfsLetter(letter, &p);
                return p;
            }
            const XItem x = asX(it);
            p.name = x.src == PaneAdfs ? x.name : sanitizeName(x.name);
            if (!it.isDir && (x.src == PaneDfs0 || x.src == PaneDfs1)) placeDfsLetter(x.dfsDir, &p);
            return p;
        };
        h.attrs = [this](const dw::TransferItem &it, const dw::fs::Entry *replacing) {
            quint32 load = 0, exec = 0;
            quint8 flags = FlagR | FlagW;
            bool given = true;
            if (!it.hostPath.isEmpty()) {             // a host file: its .inf's numbers as they are, else none
                InfData inf;
                given = m_optReadInf->isChecked() && readInf(it.hostPath, &inf);
                if (given) { load = inf.load; exec = inf.exec; flags = inf.flags; }
            } else {
                const XItem x = asX(it);
                if (x.src == PaneAdfs) {
                    load = x.load; exec = x.exec; flags = x.adfsFlags;
                } else {
                    if (x.locked) flags |= FlagL;
                    if (x.src == PaneDfs0 || x.src == PaneDfs1) { load = dw::dfs::extendAddr(x.load); exec = dw::dfs::extendAddr(x.exec); }
                }
            }
            if (!given && replacing) {                // a plain file over a file keeps its attributes, L too
                const dw::fs::AttrValues &a = replacing->attrs;
                load = a.value("load").toUInt(); exec = a.value("exec").toUInt();
                flags = quint8((a.value("read").toBool() ? FlagR : 0) | (a.value("write").toBool() ? FlagW : 0) |
                               (a.value("locked").toBool() ? FlagL : 0) | (a.value("execute").toBool() ? FlagE : 0));
            }
            return dw::fs::AttrValues{{"load", load}, {"exec", exec}, {"read", bool(flags & FlagR)}, {"write", bool(flags & FlagW)},
                                      {"locked", bool(flags & FlagL)}, {"execute", bool(flags & FlagE)}};
        };
    } else if (pane == PaneDos) {
        h.place = [this](const dw::TransferItem &it, const dw::fs::Path &target) {
            dw::fs::Placement p;
            p.dir = target;
            p.name = dw::fat::Volume::fromHost(it.hostPath.isEmpty() ? asX(it).name : it.name, it.isDir);
            return p;
        };
        h.attrs = [this](const dw::TransferItem &it, const dw::fs::Entry *) {       // a new entry, read-only if it was
            const bool ro = it.hostPath.isEmpty() && asX(it).locked;
            return dw::fs::AttrValues{{"readonly", ro}, {"hidden", false}, {"system", false}, {"archive", true}};
        };
    } else {
        h.place = [this](const dw::TransferItem &in, const dw::fs::Path &) {
            dw::TransferItem it = in;
            const QChar into = takeLetter(&it);
            QChar dir = '$';
            QString n;
            if (!it.hostPath.isEmpty()) {
                n = it.name;
                if (it.sourceFs == "dfs") { dir = n[0]; n = n.mid(2); }
            } else {
                const XItem x = asX(it);
                n = x.name;
                dir = (x.src == PaneDfs0 || x.src == PaneDfs1) ? x.dfsDir : into.isNull() ? QChar('$') : into;
            }
            dw::fs::Placement p;
            p.name = QString(dir) + "." + dw::dfs::sanitizeName(n);
            return p;
        };
        h.attrs = [this](const dw::TransferItem &in, const dw::fs::Entry *replacing) {
            dw::TransferItem it = in;
            takeLetter(&it);
            quint32 load = 0, exec = 0;
            bool locked = false;
            if (!it.hostPath.isEmpty()) {
                if (!it.sourceFs.isEmpty()) {         // its .inf
                    const QJsonObject a = it.attrs.value("acorn");
                    RetroFiles::parseHex(a["load"], &load);
                    RetroFiles::parseHex(a["exec"], &exec);
                    load &= 0x3FFFF; exec &= 0x3FFFF;
                    locked = a["access"].toString().contains('L');
                } else if (replacing) {               // a plain file over a file keeps its addresses and lock
                    load = replacing->attrs.value("load").toUInt();
                    exec = replacing->attrs.value("exec").toUInt();
                    locked = replacing->attrs.value("locked").toBool();
                }
            } else {
                const XItem x = asX(it);
                // DFS keeps 18 bits. Narrowed the same way for every file: a RISC OS stamp can't be told from an address
                // for sure (docs/retro-files-format.md), so never zero one on a guess.
                if (x.hasAddr) { load = x.load & 0x3FFFF; exec = x.exec & 0x3FFFF; }
                locked = x.locked;
            }
            return dw::fs::AttrValues{{"load", load}, {"exec", exec}, {"locked", locked}};
        };
        // An ADFS directory with a one-character name (e.g. $.F) may stand for a DFS directory letter: ask whether its
        // files should go into DFS directory F (F.NAME) rather than $. Other folders: their files go where theirs would.
        h.dirHook = [this](const dw::TransferItem &d, const dw::fs::Path &) {
            dw::xfer::DirChoice ch;
            if (!d.hostPath.isEmpty()) return ch;
            const XItem x = asX(d);
            if (!(x.src == PaneAdfs && x.name.size() == 1 && x.name[0] != '$' && dw::dfs::validDir(x.name[0]))) return ch;
            bool keep = m_run.letterPolicy == 1;
            if (m_run.letterPolicy == 0) {
                QMessageBox mb(QMessageBox::Question, "DFS directory letter",
                               QString("'%1' is a single-letter directory.\n\n"
                                       "Put its files in DFS directory %1 (%1.NAME)? If not, they go into $.")
                                   .arg(x.name),
                               QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll |
                                   QMessageBox::Cancel, this);
                mb.setDefaultButton(QMessageBox::Yes);
                int b = mb.exec();
                if (b == QMessageBox::Cancel) { ch.action = dw::xfer::DirChoice::Cancel; return ch; }
                keep = b == QMessageBox::Yes || b == QMessageBox::YesToAll;
                if (b == QMessageBox::YesToAll) m_run.letterPolicy = 1;
                if (b == QMessageBox::NoToAll) m_run.letterPolicy = 2;
            }
            ch.action = dw::xfer::DirChoice::Into;
            ch.prefix = QString(LetterMark) + (keep ? x.name[0] : QChar('$'));
            return ch;
        };
    }
    return h;
}

// "File already exists": Yes, Yes to all, No, No to all, Cancel, No the default, in each pane's words.
dw::xfer::Conflict MainWindow::askReplace(int mount, const dw::fs::Path &target, const dw::fs::Entry &existing,
                                          const dw::TransferItem &incoming) {
    const int pane = paneOf(mount);
    const QString src = importSrc(incoming);
    QString text;
    if (pane == PaneDfs0 || pane == PaneDfs1) {
        QChar d;
        QString n;
        dw::dfs::splitName(target.name(), &d, &n);
        text = QString("'%1.%2' already exists on this disc%3.\nReplace it with\n%4 ?")
                   .arg(QString(d), n, existing.attrs.value("locked").toBool() ? " and is LOCKED" : "", src);
    } else if (pane == PaneDos) {
        text = QString("'%1' already exists in the target directory%2.\nReplace it with\n%3 ?")
                   .arg(target.name(), existing.attrs.value("readonly").toBool() ? " and is READ-ONLY" : "", src);
    } else {
        text = QString("'%1' already exists in the target directory%2.\nReplace it with\n%3 ?")
                   .arg(target.name(), existing.attrs.value("locked").toBool() ? " and is LOCKED" : "", src);
    }
    auto b = QMessageBox::question(this, "File already exists", text,
                                   QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll | QMessageBox::Cancel,
                                   QMessageBox::No);
    if (b == QMessageBox::Cancel) return dw::xfer::Conflict::Cancel;
    if (b == QMessageBox::YesToAll) return dw::xfer::Conflict::ReplaceAll;
    if (b == QMessageBox::NoToAll) return dw::xfer::Conflict::SkipAll;
    return b == QMessageBox::Yes ? dw::xfer::Conflict::Replace : dw::xfer::Conflict::Skip;
}

// After an import (the kit has made the pane's rows again): the status line, and the problems in a box.
void MainWindow::importSummary(const dw::xfer::ImportResult &r) {
    if (m_run.refreshAll && m_run.pane != PaneAdfs) refresh();
    QStringList problems = m_run.problems;
    for (int i = m_run.readErrors; i < r.errors.size() - m_run.hooked; ++i) problems << r.errors[i].message;   // the session's own
    QString msg = QString("Added %1 file(s) and %2 folder(s)").arg(r.added).arg(r.dirs);
    if (r.replaced) msg += QString(", replaced %1").arg(r.replaced);
    if (r.skipped) msg += QString(", skipped %1").arg(r.skipped);
    msg += problems.isEmpty() ? ". Not saved yet - press Ctrl+S to write to the image."
                              : QString(". %1 problem(s).").arg(problems.size());
    statusBar()->showMessage(msg, 10000);
    if (!problems.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, "Some items could not be added", msg, QMessageBox::Ok, this);
        box.setDetailedText(problems.join("\n"));
        box.exec();
    }
}

// Files dragged from another copy of this program or from another tool, described by the retro-files JSON
// (docs/retro-files-format.md): imported like a copy between two of this program's windows.
void MainWindow::onRetroFilesDrop(int pane, quint32 dir, const QByteArray &json) {
    if (!paneEditable(pane)) {
        QMessageBox::warning(this, "Read-only", "The destination is read-only, so nothing was copied.");
        return;
    }
    QVector<dw::TransferItem> items;
    QString err;
    if (!dw::xfer::readJson(json, &items, &err)) { QMessageBox::warning(this, "Copy", err); return; }
    importAll(pane, dir, items, {}, false, true);
}

void MainWindow::onInternalDrop(int pane, quint32 dir, const QByteArray &payload) {
    QDataStream ds(payload);
    quint64 win = 0;
    qint32 srcPane = 0, n = 0;
    ds >> win >> srcPane >> n;
    QVector<DragRef> refs;
    for (int i = 0; i < n && ds.status() == QDataStream::Ok; ++i) {
        DragRef r;
        quint32 par = 0;
        QString nm;
        bool isDir = false;
        ds >> par >> nm >> isDir;
        r.parent = par; r.name = nm; r.isDir = isDir;
        refs.append(r);
    }
    MainWindow *src = byId(win);
    if (!src || refs.isEmpty()) { QMessageBox::warning(this, "Copy", "The source window is no longer open."); return; }
    if (src == this && (srcPane == PaneDfs0 || srcPane == PaneDfs1) && (pane == PaneDfs0 || pane == PaneDfs1)) {
        int to = pane == PaneDfs1 ? 1 : 0;                // within one window: only from one side to the other
        QVector<DragRef> other;
        for (const DragRef &r : refs) if (int(r.parent) != to) other << r;
        if (other.isEmpty()) { statusBar()->showMessage(QString("Those files are already on drive %1.").arg(to * 2), 6000); return; }
        refs = other;
        srcPane = PaneDfs0;                               // the side comes from each ref
    }
    if (!paneEditable(pane)) {
        QMessageBox::warning(this, "Read-only", "The destination is read-only, so nothing was copied.");
        return;
    }
    QVector<dw::TransferItem> items;
    QString err;
    if (!src->exportRefs(srcPane, refs, &items, &err)) { QMessageBox::warning(this, "Copy", err); return; }
    importAll(pane, dir, items, {}, true, true);
}

// ================================================ copy and paste

// Edit > Copy takes a copy of the selection there and then, so a paste copies what was selected at the time even if
// the disc has changed or its window has closed since. Inside this program a paste uses that copy (the clipboard
// carries a token naming it); for the desktop and other programs the clipboard offers the files, as a drag does
// (text/uri-list, the retro-files JSON and GNOME's list), written to a temporary folder only when something asks.
static QVector<dw::TransferItem> &clipItems() { static QVector<dw::TransferItem> v; return v; }
static QByteArray &clipToken() { static QByteArray t; return t; }

static int countItems(const QVector<dw::TransferItem> &v) {
    int n = 0;
    for (const dw::TransferItem &x : v) n += x.isDir ? countItems(x.kids) : 1;
    return n;
}

void MainWindow::copySelected() {
    auto *t = static_cast<DropTree *>(currentTree());
    QVector<DragRef> refs = t->refsFor(t->selectedItems());
    if (refs.isEmpty()) { statusBar()->showMessage("Select the files to copy.", 4000); return; }
    int pane = t == m_dfsTree ? PaneDfs0 : t->pane();      // DFS refs carry their side
    QVector<Item> picked;
    QVector<dw::TransferItem> copied;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = refItems(pane, refs, &picked, &err) && exportForCopy(picked, &copied, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, "Copy", err); return; }
    const QVector<dw::TransferItem> &items = copied;
    static int serial = 0;
    clipItems() = items;
    clipToken() = QByteArray::number(qint64(QCoreApplication::applicationPid())) + ":" + QByteArray::number(++serial);
    auto *m = new dw::DragOutMime([copied](QVector<dw::TransferItem> *out, QString *) { *out = copied; return true; },
                                  RetroFiles::dragSpec(writeInfOnExtract(), QFileInfo(m_img.path()).fileName(), pane));
    m->setForClipboard(true);
    m->setData(DW_MIME_CLIP, clipToken());
    QApplication::clipboard()->setMimeData(m);
    static bool once = false;                  // the copied files live in a folder removed at exit: don't leave
    if (!once) {                               // a clipboard behind that names them
        once = true;
        connect(qApp, &QCoreApplication::aboutToQuit, [] {
            const QMimeData *c = QApplication::clipboard()->mimeData();
            if (c && c->hasFormat(DW_MIME_CLIP) && QApplication::clipboard()->ownsClipboard()) QApplication::clipboard()->clear();
        });
    }
    int n = countItems(items);
    statusBar()->showMessage(QString("Copied %1 file%2 - Edit > Paste puts %3 into another disc, a folder on the desktop "
                                     "or another program.").arg(n).arg(n == 1 ? "" : "s").arg(n == 1 ? "it" : "them"), 8000);
}

// Local files the clipboard names: a file manager's copy (text/uri-list, or GNOME's own list).
static QStringList clipboardPaths(const QMimeData *m) {
    QStringList paths;
    for (const QUrl &u : m->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (paths.isEmpty() && m->hasFormat("x-special/gnome-copied-files")) {
        const QList<QByteArray> lines = m->data("x-special/gnome-copied-files").split('\n');
        for (int i = 1; i < lines.size(); ++i) {               // line 0 is "copy" or "cut"
            QUrl u = QUrl::fromEncoded(lines[i].trimmed());
            if (u.isLocalFile()) paths << u.toLocalFile();
        }
    }
    return paths;
}

bool MainWindow::clipboardUsable() const {
    const QMimeData *m = QApplication::clipboard()->mimeData();
    return m && (m->hasFormat(DW_MIME_CLIP) || m->hasFormat(RetroFiles::MimeType) || m->hasUrls() ||
                 m->hasFormat("x-special/gnome-copied-files"));
}

// Edit > Paste: into the directory of the current row (ADFS, DOS) or the side in use (DFS), as Add files would.
void MainWindow::paste() {
    int pane = PaneAdfs;
    quint32 dir = targetDirForItem(m_tree->currentItem());
    if (dosActive()) { pane = PaneDos; dir = dosTargetDir(m_dosTree->currentItem()); }
    else if (dfsSide() >= 0) { pane = dfsSide() ? PaneDfs1 : PaneDfs0; dir = 0; }
    if (!m_img.isOpen()) return;
    if (!paneEditable(pane)) { QMessageBox::warning(this, "Paste", "This disc is read-only: " + readOnlyReason() + "."); return; }
    const QMimeData *m = QApplication::clipboard()->mimeData();
    if (!m || !pasteItems(pane, dir, m))
        statusBar()->showMessage("Nothing to paste: copy some files first (here, in another window, or in a file manager).", 6000);
}

bool MainWindow::pasteItems(int pane, quint32 dir, const QMimeData *m) {
    if (m->hasFormat(DW_MIME_CLIP) && !clipToken().isEmpty() && m->data(DW_MIME_CLIP) == clipToken()) {
        importAll(pane, dir, clipItems(), {}, true, true);   // copied in this program: nothing written to the host
        return true;
    }
    if (m->hasFormat(RetroFiles::MimeType)) {             // another copy of this program, or another tool
        QByteArray json = m->data(RetroFiles::MimeType);
        if (!json.isEmpty()) { onRetroFilesDrop(pane, dir, json); return true; }
    }
    QStringList paths = clipboardPaths(m);                 // files copied in a file manager: as Add files
    if (paths.isEmpty()) return false;
    if (pane == PaneAdfs) importPaths(paths, dir);
    else if (pane == PaneDos) dosImportPaths(paths, dir);
    else dfsImportPaths(pane == PaneDfs1 ? 1 : 0, paths);
    return true;
}
