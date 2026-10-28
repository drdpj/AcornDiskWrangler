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
#include "appsettings.h"
#include "mainwindow_p.h"
#include "viewer.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
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
#include <QTimer>
#include <algorithm>

using namespace Adfs;

// ------------------------------------------------------- registry / tabs / panes

QList<MainWindow *> &MainWindow::windows() {
    static QList<MainWindow *> l;
    return l;
}

MainWindow *MainWindow::byId(quint64 id) {
    for (MainWindow *w : windows())
        if (w->id() == id) return w;
    return nullptr;
}

MainWindow::~MainWindow() { windows().removeAll(this); }

void MainWindow::ensureTab(QWidget *w, const QString &text, bool want) {
    int idx = m_tabs->indexOf(w);
    if (!want) {
        if (idx >= 0) m_tabs->removeTab(idx);
        return;
    }
    if (idx < 0) {
        QWidget *order[] = {m_tree, m_dosTree, m_dfsTree[0], m_dfsTree[1]};
        int pos = 0;
        for (QWidget *o : order) {
            if (o == w) break;
            if (m_tabs->indexOf(o) >= 0) ++pos;
        }
        idx = m_tabs->insertTab(pos, w, text);
    }
    m_tabs->setTabText(idx, text);
}

int MainWindow::dfsSide() const {
    for (int s = 0; s < 2; ++s)
        if (m_dfs[s].attached() && m_tabs->currentWidget() == m_dfsTree[s]) return s;
    return -1;
}

bool MainWindow::dfsEditable(int s) const { return m_img.editable() && m_dfs[s].attached() && m_dfsOk[s]; }

int MainWindow::paneOf(const QTreeWidget *t) const {
    if (t == m_tree) return PaneAdfs;
    if (t == m_dosTree) return PaneDos;
    return t == m_dfsTree[0] ? PaneDfs0 : PaneDfs1;
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

void MainWindow::refreshDfs() {
    for (int s = 0; s < 2; ++s) {
        DropTree *t = m_dfsTree[s];
        t->clear();
        bool have = false;
        if (m_img.isOpen() && m_info.kind == DiskInfo::Dfs && (s == 0 || m_info.dfs.doubleSided)) {
            QString err;
            have = m_dfs[s].attach(&m_img, s, m_info.dfs, &err);
            if (!have) { m_dfs[s].detach(); m_dfsErr[s] = err; }
        } else {
            m_dfs[s].detach();
        }
        ensureTab(t, m_info.dfs.doubleSided ? (s == 0 ? "DFS drive 0" : "DFS drive 2") : "DFS", have);
        if (!have) continue;

        auto *r = new QTreeWidgetItem(t);
        QString title = m_dfs[s].title();
        r->setText(0, rootLabel(QString("Drive %1").arg(s * 2), title));
        r->setIcon(0, style()->standardIcon(QStyle::SP_DriveFDIcon));
        r->setData(0, RoleParent, 0);
        r->setData(0, RoleName, "$");
        r->setData(0, RoleIsDir, true);
        r->setData(0, RoleSector, 0);
        r->setData(0, RoleIsRoot, true);
        QVector<Dfs::File> v = m_dfs[s].files();
        std::stable_sort(v.begin(), v.end(), [](const Dfs::File &a, const Dfs::File &b) {
            if (a.dir.toUpper() != b.dir.toUpper()) return a.dir.toUpper() < b.dir.toUpper();
            return a.name.toUpper() < b.name.toUpper();
        });
        for (const Dfs::File &f : v) {
            auto *it = new QTreeWidgetItem(r);
            it->setText(0, f.fullName());
            it->setText(1, f.locked ? "L" : "");
            setNumCell(it, 2, QString("%1").arg(f.load, 6, 16, QChar('0')).toUpper());
            setNumCell(it, 3, QString("%1").arg(f.exec, 6, 16, QChar('0')).toUpper());
            setNumCell(it, 4, QString::number(f.length));
            setNumCell(it, 5, QString::number(f.start));
            it->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
            it->setData(0, RoleParent, 0);
            it->setData(0, RoleName, f.fullName());
            it->setData(0, RoleIsDir, false);
            it->setData(0, RoleSector, f.start);
            it->setData(0, RoleIsRoot, false);
        }
        r->setExpanded(true);
    }
}

QVector<MainWindow::FSel> MainWindow::dfsSelected(int side) const {
    QVector<FSel> v;
    for (QTreeWidgetItem *it : m_dfsTree[side]->selectedItems()) {
        if (it->data(0, RoleIsRoot).toBool()) continue;
        QString full = it->data(0, RoleName).toString();
        if (full.size() < 3) continue;
        v.append({full[0], full.mid(2), it});
    }
    return v;
}

// ----------------------------------------------------------------- importing

void MainWindow::dfsImportPaths(int side, const QStringList &paths) {
    if (!dfsEditable(side)) {
        QMessageBox::warning(this, "Read-only", "This DFS drive is read-only (image read-only, or its catalogue has errors).");
        return;
    }
    ImportCtx c;
    for (const QString &p : paths) {
        if (c.cancelled) break;
        if (QFileInfo(p).isDir()) dfsImportDir(side, p, c);
        else if (!isSidecar(p)) dfsImportFile(side, p, c);
    }
    refreshDfs();
    updateUi();
    importSummary(c, true);
}

void MainWindow::dfsImportDir(int side, const QString &path, ImportCtx &c) {
    // DFS has no subdirectories: everything inside the folder is added flat
    const QFileInfoList list = QDir(path).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &e : list) {
        if (c.cancelled) return;
        if (e.isDir()) dfsImportDir(side, e.absoluteFilePath(), c);
        else if (!isSidecar(e.absoluteFilePath())) dfsImportFile(side, e.absoluteFilePath(), c);
    }
}

void MainWindow::dfsImportFile(int side, const QString &path, ImportCtx &c) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        c.errors << QString("%1: cannot read (%2)").arg(path, f.errorString());
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    QString name = Dfs::sanitizeName(QFileInfo(path).fileName());
    QChar dir = '$';
    quint32 load = 0, exec = 0;
    bool locked = false, haveInf = false;
    if (m_optReadInf->isChecked()) {
        InfData inf;
        if (readInf(path, &inf)) {
            haveInf = true;
            if (!inf.name.isEmpty()) name = Dfs::sanitizeName(inf.name);
            if (!inf.dir.isNull() && Dfs::validDir(inf.dir)) dir = inf.dir;
            load = inf.load & 0x3FFFF;
            exec = inf.exec & 0x3FFFF;
            locked = (inf.flags & FlagL) != 0;
        }
    }
    dfsAddOne(side, dir, name, data, load, exec, locked, haveInf, path, c);
}

void MainWindow::dfsAddOne(int side, QChar dir, QString name, const QByteArray &data, quint32 load, quint32 exec,
                           bool locked, bool haveAttrs, const QString &src, ImportCtx &c) {
    QString err;
    if (c.created.contains(dfsKey(side, dir, name))) name = uniqueDfsName(name, dir, m_dfs[side], c, side);
    int idx = m_dfs[side].indexOf(dir, name);
    bool replace = false;
    if (idx >= 0) {
        const Dfs::File ex = m_dfs[side].files()[idx];
        int pol = c.policy;
        if (pol == 0) {
            auto b = QMessageBox::question(
                this, "File already exists",
                QString("'%1.%2' already exists on this disc%3.\nReplace it with\n%4 ?")
                    .arg(QString(dir), name, ex.locked ? " and is LOCKED" : "", src),
                QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll | QMessageBox::Cancel,
                QMessageBox::No);
            if (b == QMessageBox::Cancel) { c.cancelled = true; return; }
            if (b == QMessageBox::YesToAll) c.policy = pol = 1;
            else if (b == QMessageBox::NoToAll) c.policy = pol = 2;
            else pol = (b == QMessageBox::Yes) ? 1 : 2;
        }
        if (pol == 2) { c.skipped++; return; }
        replace = true;
        if (!haveAttrs) { load = ex.load; exec = ex.exec; locked = ex.locked; }
    }
    if (!m_dfs[side].addFile(dir, name, data, load, exec, locked, replace, &err)) {
        c.errors << QString("%1: %2").arg(src, err);
        return;
    }
    c.created.insert(dfsKey(side, dir, name));
    if (replace) c.replaced++; else c.files++;
}

// ------------------------------------------------- delete / props / rename / extract

void MainWindow::dfsDelete(int side) {
    QVector<FSel> sel = dfsSelected(side);
    if (sel.isEmpty()) return;
    int locked = 0;
    for (const FSel &s : sel) {
        int i = m_dfs[side].indexOf(s.dir, s.name);
        if (i >= 0 && m_dfs[side].files()[i].locked) ++locked;
    }
    QString msg = QString("Delete %1 file(s) from DFS drive %2?").arg(sel.size()).arg(side * 2);
    if (locked) msg += QString("\n\n%1 of these are LOCKED.").arg(locked);
    if (QMessageBox::question(this, "Delete", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QStringList errors;
    for (const FSel &s : sel) {
        QString err;
        if (!m_dfs[side].remove(s.dir, s.name, &err)) errors << QString("%1.%2: %3").arg(QString(s.dir), s.name, err);
    }
    refreshDfs();
    updateUi();
    if (!errors.isEmpty()) QMessageBox::warning(this, "Delete", errors.join("\n"));
}

void MainWindow::dfsProps(int side) {
    QVector<FSel> sel = dfsSelected(side);
    if (sel.isEmpty()) return;
    QVector<Dfs::File> files;
    for (const FSel &s : sel) {
        int i = m_dfs[side].indexOf(s.dir, s.name);
        if (i < 0) { QMessageBox::warning(this, "Properties", "Entry not found"); return; }
        files.append(m_dfs[side].files()[i]);
    }
    bool single = files.size() == 1;
    QDialog dlg(this);
    dlg.setWindowTitle(single ? "Properties - " + files[0].fullName() : QString("Properties - %1 files").arg(files.size()));
    auto *form = new QFormLayout(&dlg);
    QLineEdit *nameEd = nullptr, *dirEd = nullptr, *loadEd = nullptr, *execEd = nullptr;
    if (single) {
        nameEd = new QLineEdit(files[0].name);
        nameEd->setMaxLength(Dfs::MaxName);
        dirEd = new QLineEdit(QString(files[0].dir));
        dirEd->setMaxLength(1);
        form->addRow("Name (max 7 characters):", nameEd);
        form->addRow("Directory letter:", dirEd);
    }
    auto *cbL = new QCheckBox("L  - locked");
    int on = 0;
    for (const Dfs::File &f : files) on += f.locked ? 1 : 0;
    if (on == 0) cbL->setCheckState(Qt::Unchecked);
    else if (on == files.size()) cbL->setCheckState(Qt::Checked);
    else { cbL->setTristate(true); cbL->setCheckState(Qt::PartiallyChecked); }
    form->addRow("Attributes:", cbL);
    if (single) {
        auto *v = new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]{1,6}"), &dlg);
        loadEd = new QLineEdit(QString("%1").arg(files[0].load, 6, 16, QChar('0')).toUpper());
        execEd = new QLineEdit(QString("%1").arg(files[0].exec, 6, 16, QChar('0')).toUpper());
        loadEd->setValidator(v);
        execEd->setValidator(v);
        form->addRow("Load address (hex, 18-bit):", loadEd);
        form->addRow("Exec address (hex, 18-bit):", execEd);
        form->addRow("Length:", new QLabel(QString("%1 bytes").arg(files[0].length)));
        form->addRow("Start sector:", new QLabel(QString::number(files[0].start)));
    }
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    QStringList errors;
    for (int i = 0; i < files.size(); ++i) {
        const Dfs::File &f = files[i];
        QString nn = nameEd ? nameEd->text().trimmed() : f.name;
        QChar nd = (dirEd && !dirEd->text().isEmpty()) ? dirEd->text()[0] : f.dir;
        bool locked = cbL->checkState() == Qt::PartiallyChecked ? f.locked : cbL->checkState() == Qt::Checked;
        quint32 load = loadEd ? loadEd->text().toUInt(nullptr, 16) : f.load;
        quint32 exec = execEd ? execEd->text().toUInt(nullptr, 16) : f.exec;
        QString err;
        if (!m_dfs[side].modify(f.dir, f.name, nd, nn, locked, load, exec, &err))
            errors << QString("%1: %2").arg(f.fullName(), err);
    }
    refreshDfs();
    updateUi();
    if (!errors.isEmpty()) QMessageBox::warning(this, "Properties", errors.join("\n"));
}

void MainWindow::dfsRenameVolume(int side) {
    bool ok = false;
    QString n = QInputDialog::getText(this, "Rename DFS disc", "Disc title (max 12 characters):", QLineEdit::Normal,
                                      m_dfs[side].title(), &ok);
    if (!ok) return;
    QString err;
    if (!m_dfs[side].setTitle(n.trimmed(), &err)) { QMessageBox::warning(this, "Rename disc", err); return; }
    refreshDfs();
    updateUi();
}

void MainWindow::dfsRename(int side) {
    QList<QTreeWidgetItem *> items = m_dfsTree[side]->selectedItems();
    if (items.size() == 1 && items[0]->data(0, RoleIsRoot).toBool()) { dfsRenameVolume(side); return; }
    QVector<FSel> sel = dfsSelected(side);
    if (sel.size() != 1) { statusBar()->showMessage("Select a single file to rename.", 4000); return; }
    bool ok = false;
    QString n = QInputDialog::getText(this, "Rename", "New name (NAME, or D.NAME to change the directory letter too):",
                                      QLineEdit::Normal, sel[0].name, &ok).trimmed();
    if (!ok || n.isEmpty()) return;
    QChar nd = sel[0].dir;
    QString nn = n;
    if (n.size() >= 3 && n[1] == '.') { nd = n[0]; nn = n.mid(2); }
    int i = m_dfs[side].indexOf(sel[0].dir, sel[0].name);
    if (i < 0) return;
    const Dfs::File f = m_dfs[side].files()[i];
    QString err;
    if (!m_dfs[side].modify(f.dir, f.name, nd, nn, f.locked, f.load, f.exec, &err)) {
        QMessageBox::warning(this, "Rename", err);
        return;
    }
    refreshDfs();
    updateUi();
}

void MainWindow::dfsExtract(int side) {
    AppSettings s;
    QString dest = QFileDialog::getExistingDirectory(this, "Extract DFS files to folder", s.value("extractDir").toString());
    if (dest.isEmpty()) return;
    s.setValue("extractDir", dest);
    QVector<Dfs::File> list;
    QVector<FSel> sel = dfsSelected(side);
    if (sel.isEmpty()) list = m_dfs[side].files();
    else for (const FSel &x : sel) { int i = m_dfs[side].indexOf(x.dir, x.name); if (i >= 0) list.append(m_dfs[side].files()[i]); }
    QStringList errors;
    for (const Dfs::File &f : list) {
        QByteArray data;
        QString err;
        if (!m_dfs[side].readFile(f, &data, &err)) { errors << QString("%1: %2").arg(f.fullName(), err); continue; }
        QString host = hostSafeName(f.dir == '$' ? f.name : f.fullName());
        QFile out(dest + "/" + host);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size()) {
            errors << QString("%1: %2").arg(host, out.errorString());
            continue;
        }
        out.close();
        if (m_optWriteInf->isChecked()) {
            QFile inf(dest + "/" + host + ".inf");
            if (inf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                inf.write(QString::asprintf("%c.%-7s %08X %08X %08X %02X\n", f.dir.toLatin1(), qPrintable(f.name),
                                            extendDfsAddr(f.load), extendDfsAddr(f.exec), f.length,
                                            f.locked ? 8 : 3).toLatin1());
        }
    }
    if (errors.isEmpty()) statusBar()->showMessage(QString("Extracted %1 file(s) to %2").arg(list.size()).arg(dest), 6000);
    else QMessageBox::warning(this, "Extract", errors.join("\n"));
}

// ================================================ copying between discs

bool MainWindow::exportAdfs(quint32 parent, const QString &name, XItem *x, QString *err, int depth) {
    Dir d;
    if (!m_img.readDir(parent, &d, err)) return false;
    int i = d.indexOf(name);
    if (i < 0) { *err = QString("'%1' not found").arg(name); return false; }
    const Entry e = d.entries[i];
    x->name = e.name();
    x->src = PaneAdfs;
    x->isDir = e.isDir();
    x->load = e.load;
    x->exec = e.exec;
    x->hasAddr = !e.isDir();
    x->locked = e.flag(Entry::L);
    x->adfsFlags = quint8((e.flag(Entry::R) ? FlagR : 0) | (e.flag(Entry::W) ? FlagW : 0) |
                          (e.flag(Entry::L) ? FlagL : 0) | (e.flag(Entry::E) ? FlagE : 0));
    if (!e.isDir() && e.hasStamp()) {               // as the tree shows it: only plausible RISC OS dates
        QDateTime t = e.stamp();
        if (t.date().year() >= 1981 && t.date().year() <= 2100) x->when = QDateTime(t.date(), t.time(), Qt::LocalTime);
    }
    if (e.isDir()) {
        if (depth > 32) { *err = "directory nesting too deep"; return false; }
        Dir c;
        if (!m_img.readDir(e.sin, &c, err)) return false;
        for (const Entry &k : c.entries) {
            XItem kid;
            if (!exportAdfs(e.sin, k.name(), &kid, err, depth + 1)) return false;
            x->kids.append(kid);
        }
        return true;
    }
    return m_img.readFile(e, &x->data, err);
}

bool MainWindow::exportDos(quint32 parent, const QString &name, XItem *x, QString *err, int depth) {
    QVector<Dos::DEntry> L;
    if (!m_dos.listDir(parent, &L, err)) return false;
    for (const Dos::DEntry &e : L) {
        if (e.name != name) continue;
        x->name = e.name;
        x->src = PaneDos;
        x->isDir = e.isDir();
        x->locked = (e.attr & Dos::AttrR) != 0;
        x->dosAttr = e.attr;
        QDateTime w = e.when();
        if (w.isValid()) x->when = QDateTime(w.date(), w.time(), Qt::LocalTime);
        if (e.isDir()) {
            if (depth > 32) { *err = "directory nesting too deep"; return false; }
            QVector<Dos::DEntry> C;
            if (!m_dos.listDir(e.cluster, &C, err)) return false;
            for (const Dos::DEntry &k : C) {
                if (k.name == "." || k.name == "..") continue;
                XItem kid;
                if (!exportDos(e.cluster, k.name, &kid, err, depth + 1)) return false;
                x->kids.append(kid);
            }
            return true;
        }
        return m_dos.readFile(e, &x->data, err);
    }
    *err = QString("'%1' not found").arg(name);
    return false;
}

bool MainWindow::exportDfs(int side, QChar dir, const QString &name, XItem *x, QString *err) {
    int i = m_dfs[side].indexOf(dir, name);
    if (i < 0) { *err = QString("'%1.%2' not found").arg(QString(dir), name); return false; }
    const Dfs::File f = m_dfs[side].files()[i];
    x->name = f.name;
    x->dfsDir = f.dir;
    x->src = side ? PaneDfs1 : PaneDfs0;
    x->isDir = false;
    x->load = f.load;
    x->exec = f.exec;
    x->hasAddr = true;
    x->locked = f.locked;
    return m_dfs[side].readFile(f, &x->data, err);
}

bool MainWindow::exportRefs(int pane, const QVector<DragRef> &refs, QVector<XItem> *out, QString *err) {
    out->clear();
    for (const DragRef &r : refs) {
        XItem x;
        bool ok = false;
        if (pane == PaneAdfs) ok = exportAdfs(r.parent, r.name, &x, err, 0);
        else if (pane == PaneDos) ok = exportDos(r.parent, r.name, &x, err, 0);
        else if (r.name.size() >= 3) ok = exportDfs(pane == PaneDfs1 ? 1 : 0, r.name[0], r.name.mid(2), &x, err);
        else *err = "bad item";
        if (!ok) return false;
        out->append(x);
    }
    return true;
}

void MainWindow::importXAdfs(quint32 dir, const XItem &x, ImportCtx &c) {
    QString name = x.src == PaneAdfs ? x.name : sanitizeName(x.name);
    if (x.isDir) {
        QString err;
        Dir d;
        if (!m_img.readDir(dir, &d, &err)) { c.errors << QString("%1: %2").arg(x.name, err); return; }
        if (c.created.contains(keyFor(dir, name))) name = uniqueName(name, d, c, dir);
        int idx = d.indexOf(name);
        quint32 sector = 0;
        if (idx >= 0) {
            if (!d.entries[idx].isDir()) { c.errors << QString("%1: a file called '%2' already exists there").arg(x.name, name); return; }
            sector = d.entries[idx].sin;
        } else {
            if (!m_img.makeDir(dir, name, &err, &sector)) { c.errors << QString("%1: %2").arg(x.name, err); return; }
            c.dirs++;
        }
        c.created.insert(keyFor(dir, name));
        for (const XItem &k : x.kids) { if (c.cancelled) return; importXAdfs(sector, k, c); }
        return;
    }
    // A DFS file in a directory other than $ goes into a subdirectory named after the letter, so $.P.FILE
    // dropped on $.Library becomes $.Library.P.FILE. The subdirectory is reused if it is already there.
    if ((x.src == PaneDfs0 || x.src == PaneDfs1) && x.dfsDir != '$') {
        QString err, letter = sanitizeName(QString(x.dfsDir.toUpper()));
        Dir d;
        if (!m_img.readDir(dir, &d, &err)) { c.errors << QString("%1: %2").arg(x.name, err); return; }
        int idx = d.indexOf(letter);
        quint32 sector = 0;
        if (idx >= 0) {
            if (!d.entries[idx].isDir()) {
                c.errors << QString("%1.%2: a file called '%3' is in the way of directory %3")
                                .arg(QString(x.dfsDir), x.name, letter);
                return;
            }
            sector = d.entries[idx].sin;
        } else {
            if (!m_img.makeDir(dir, letter, &err, &sector)) {
                c.errors << QString("%1.%2: %3").arg(QString(x.dfsDir), x.name, err);
                return;
            }
            c.dirs++;
        }
        c.created.insert(keyFor(dir, letter));
        dir = sector;
    }
    quint32 load = 0, exec = 0;
    quint8 flags = FlagR | FlagW;
    if (x.src == PaneAdfs) {
        load = x.load; exec = x.exec; flags = x.adfsFlags;
    } else {
        if (x.locked) flags |= FlagL;
        if (x.src == PaneDfs0 || x.src == PaneDfs1) { load = extendDfsAddr(x.load); exec = extendDfsAddr(x.exec); }
    }
    addOne(dir, name, x.data, load, exec, flags, true, x.name, c);
}

void MainWindow::importXDos(quint32 dir, const XItem &x, ImportCtx &c) {
    QString name = Dos::Volume::fromHost(x.name, x.isDir);
    if (x.isDir) {
        QString err;
        QVector<Dos::DEntry> L;
        if (!m_dos.listDir(dir, &L, &err)) { c.errors << QString("%1: %2").arg(x.name, err); return; }
        QString key = QString("D%1:%2").arg(dir).arg(name.toUpper());
        if (c.created.contains(key)) { name = uniqueDosName(name, L, c, dir); key = QString("D%1:%2").arg(dir).arg(name.toUpper()); }
        quint32 cluster = 0;
        bool found = false;
        for (const Dos::DEntry &e : L)
            if (e.name.compare(name, Qt::CaseInsensitive) == 0) {
                if (!e.isDir()) { c.errors << QString("%1: a file called '%2' already exists there").arg(x.name, name); return; }
                cluster = e.cluster;
                found = true;
            }
        if (!found) {
            if (!m_dos.makeDir(dir, name, &err, &cluster)) { c.errors << QString("%1: %2").arg(x.name, err); return; }
            c.dirs++;
        }
        c.created.insert(key);
        for (const XItem &k : x.kids) { if (c.cancelled) return; importXDos(cluster, k, c); }
        return;
    }
    dosAddOne(dir, name, x.data, x.name, c, x.locked);
}

void MainWindow::importXDfs(int side, const XItem &x, ImportCtx &c, QChar keepDir) {
    if (x.isDir) {      // DFS has no directories: flatten
        // An ADFS directory with a one-character name (e.g. $.F) may stand for a DFS directory letter:
        // ask whether its files should go into DFS directory F (F.NAME) rather than $.
        if (x.src == PaneAdfs && x.name.size() == 1 && x.name[0] != '$' && Dfs::validDir(x.name[0])) {
            bool keep = c.letterPolicy == 1;
            if (c.letterPolicy == 0) {
                QMessageBox mb(QMessageBox::Question, "DFS directory letter",
                               QString("'%1' is a single-letter directory.\n\n"
                                       "Put its files in DFS directory %1 (%1.NAME)? If not, they go into $.")
                                   .arg(x.name),
                               QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll |
                                   QMessageBox::Cancel, this);
                mb.setDefaultButton(QMessageBox::Yes);
                int b = mb.exec();
                if (b == QMessageBox::Cancel) { c.cancelled = true; return; }
                keep = b == QMessageBox::Yes || b == QMessageBox::YesToAll;
                if (b == QMessageBox::YesToAll) c.letterPolicy = 1;
                if (b == QMessageBox::NoToAll) c.letterPolicy = 2;
            }
            keepDir = keep ? x.name[0] : QChar();
        }
        for (const XItem &k : x.kids) { if (c.cancelled) return; importXDfs(side, k, c, keepDir); }
        return;
    }
    QChar dir = (x.src == PaneDfs0 || x.src == PaneDfs1) ? x.dfsDir : keepDir.isNull() ? QChar('$') : keepDir;
    QString name = Dfs::sanitizeName(x.name);
    quint32 load = 0, exec = 0;
    // DFS keeps 18 bits. Narrowed the same way for every file: a RISC OS stamp can't be told from an address for
    // sure (docs/retro-files-format.md), so never zero one on a guess.
    if (x.hasAddr) { load = x.load & 0x3FFFF; exec = x.exec & 0x3FFFF; }
    dfsAddOne(side, dir, name, x.data, load, exec, x.locked, true, x.name, c);
}

// Files dragged from another copy of this program or from another tool, described by the retro-files JSON
// (docs/retro-files-format.md): imported like a copy between two of this program's windows.
void MainWindow::onRetroFilesDrop(int pane, quint32 dir, const QByteArray &json) {
    if (!paneEditable(pane)) {
        QMessageBox::warning(this, "Read-only", "The destination is read-only, so nothing was copied.");
        return;
    }
    QVector<XItem> items;
    QString err;
    if (!RetroFiles::readJson(json, &items, &err)) { QMessageBox::warning(this, "Copy", err); return; }
    ImportCtx c;
    importX(pane, dir, items, c);
    refreshTree();
    updateUi();
    importSummary(c, true);
}

void MainWindow::importX(int pane, quint32 dir, const QVector<XItem> &items, ImportCtx &c) {
    for (const XItem &x : items) {
        if (c.cancelled) return;
        if (pane == PaneAdfs) importXAdfs(dir, x, c);
        else if (pane == PaneDos) importXDos(dir, x, c);
        else importXDfs(pane == PaneDfs1 ? 1 : 0, x, c);
    }
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
    if (!paneEditable(pane)) {
        QMessageBox::warning(this, "Read-only", "The destination is read-only, so nothing was copied.");
        return;
    }
    QVector<XItem> items;
    QString err;
    if (!src->exportRefs(srcPane, refs, &items, &err)) { QMessageBox::warning(this, "Copy", err); return; }
    ImportCtx c;
    importX(pane, dir, items, c);
    refreshTree();
    updateUi();
    importSummary(c, true);
}
