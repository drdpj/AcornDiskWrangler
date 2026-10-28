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
// mainwindow_dos.cpp - the DOS Plus partition pane ($.DOS.DRIVE_C) and the file viewer hookup.
#include "mainwindow.h"
#include "appsettings.h"
#include "mainwindow_p.h"
#include "viewer.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <functional>

using namespace Adfs;

bool MainWindow::dosActive() const { return m_dos.attached() && m_tabs->currentWidget() == m_dosTree; }

bool MainWindow::findDriveC(Entry *out) const {
    Dir root, d;
    QString err;
    if (!m_img.readDir(RootSector, &root, &err)) return false;
    int i = root.indexOf("DOS");
    if (i < 0 || !root.entries[i].isDir() || !m_img.readDir(root.entries[i].sin, &d, &err)) return false;
    int j = d.indexOf("DRIVE_C");
    if (j < 0 || d.entries[j].isDir()) return false;
    *out = d.entries[j];
    return true;
}

void MainWindow::refreshDos() {
    QSet<quint32> expanded;
    std::function<void(QTreeWidgetItem *)> gather = [&](QTreeWidgetItem *it) {
        if (it->isExpanded() && it->data(0, RoleIsDir).toBool()) expanded.insert(it->data(0, RoleSector).toUInt());
        for (int i = 0; i < it->childCount(); ++i) gather(it->child(i));
    };
    for (int i = 0; i < m_dosTree->topLevelItemCount(); ++i) gather(m_dosTree->topLevelItem(i));
    bool first = m_dosTree->topLevelItemCount() == 0;
    m_dosTree->clear();

    bool have = false;
    QString tabText = "DOS Plus (DRIVE_C)";
    if (m_img.isOpen()) {
        QString err;
        const quint32 len = quint32(m_img.fullSize());
        switch (m_info.kind) {
        case DiskInfo::AdfsDosHybrid:
            have = m_dos.attachFormat(&m_img, 0, len, Dos::FmtAcorn640, &err);
            tabText = "DOS Plus (640K hybrid)";
            break;
        case DiskInfo::DosAcorn800:
            have = m_dos.attachFormat(&m_img, 0, len, Dos::FmtAcorn800, &err);
            tabText = "DOS Plus (800K)";
            break;
        case DiskInfo::DosPc:
            have = m_info.dosFromTable ? m_dos.attachFormat(&m_img, 0, len, m_info.dosFormat, &err)
                                       : m_dos.attach(&m_img, 0, len, &err);
            tabText = "PC FAT";
            break;
        case DiskInfo::Dfs:
            break;
        default: {                       // ADFS: the partition file $.DOS.DRIVE_C
            Entry e;
            if (findDriveC(&e) && m_dos.attach(&m_img, e.sin, e.length, &err)) have = true;
        }
        }
        if (!have) m_dos.detach();
    } else {
        m_dos.detach();
    }
    ensureTab(m_dosTree, tabText, have);
    if (!have) return;

    auto *r = new QTreeWidgetItem(m_dosTree);
    r->setText(0, rootLabel("C:\\", m_dos.label()));
    r->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
    r->setData(0, RoleParent, 0);
    r->setData(0, RoleName, "\\");
    r->setData(0, RoleIsDir, true);
    r->setData(0, RoleSector, 0);
    r->setData(0, RoleIsRoot, true);
    QSet<quint32> seen;
    populateDos(r, 0, seen, 0);
    expanded.insert(0);
    std::function<void(QTreeWidgetItem *)> restore = [&](QTreeWidgetItem *it) {
        if (it->data(0, RoleIsDir).toBool() && (first ? it == r : expanded.contains(it->data(0, RoleSector).toUInt())))
            it->setExpanded(true);
        for (int i = 0; i < it->childCount(); ++i) restore(it->child(i));
    };
    restore(r);
}

void MainWindow::populateDos(QTreeWidgetItem *parentItem, quint32 dirCluster, QSet<quint32> &seen, int depth) {
    if (depth > 32 || (dirCluster && seen.contains(dirCluster))) return;
    if (dirCluster) seen.insert(dirCluster);
    QVector<Dos::DEntry> v;
    QString err;
    if (!m_dos.listDir(dirCluster, &v, &err)) {
        parentItem->setToolTip(0, err);
        parentItem->setForeground(0, Qt::red);
        return;
    }
    for (const Dos::DEntry &e : v) {
        if (e.name == "." || e.name == "..") continue;
        auto *it = new QTreeWidgetItem(parentItem);
        it->setText(0, e.name);
        it->setText(1, e.attrString());
        setNumCell(it, 3, QString::number(e.cluster));
        QDateTime w = e.when();
        if (w.isValid()) it->setText(4, w.toString("yyyy-MM-dd hh:mm"));
        it->setData(0, RoleParent, dirCluster);
        it->setData(0, RoleName, e.name);
        it->setData(0, RoleIsDir, e.isDir());
        it->setData(0, RoleSector, e.cluster);
        it->setData(0, RoleIsRoot, false);
        if (e.isDir()) {
            it->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            it->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
            setNumCell(it, 2, QString::number(e.size));
        }
        if (e.isDir()) populateDos(it, e.cluster, seen, depth + 1);
    }
}

QVector<MainWindow::DSel> MainWindow::dosSelected() const {
    QVector<DSel> v;
    for (QTreeWidgetItem *it : m_dosTree->selectedItems()) {
        if (it->data(0, RoleIsRoot).toBool()) continue;
        v.append({it->data(0, RoleParent).toUInt(), it->data(0, RoleName).toString(), it->data(0, RoleIsDir).toBool(),
                  it->data(0, RoleSector).toUInt(), it});
    }
    return v;
}

quint32 MainWindow::dosTargetDir(QTreeWidgetItem *it) const {
    if (!it) return 0;
    if (it->data(0, RoleIsDir).toBool()) return it->data(0, RoleSector).toUInt();
    return it->data(0, RoleParent).toUInt();
}

// ----------------------------------------------------------------- viewing

void MainWindow::viewSelected() {
    int opened = 0;
    QString err;
    int ds = dfsSide();
    if (ds >= 0) {
        for (const FSel &s : dfsSelected(ds)) {
            if (opened >= 8) break;
            int i = m_dfs[ds].indexOf(s.dir, s.name);
            QByteArray data;
            if (i < 0 || !m_dfs[ds].readFile(m_dfs[ds].files()[i], &data, &err)) {
                QMessageBox::warning(this, "View", QString("%1: %2").arg(s.name, err));
                continue;
            }
            (new ViewerDialog(QString(s.dir) + "." + s.name, data, this))->show();
            ++opened;
        }
    } else if (dosActive()) {
        for (const DSel &s : dosSelected()) {
            if (s.isDir || opened >= 8) continue;
            QVector<Dos::DEntry> L;
            QByteArray data;
            bool ok = false;
            if (m_dos.listDir(s.parent, &L, &err))
                for (const Dos::DEntry &e : L)
                    if (e.name == s.name) { ok = m_dos.readFile(e, &data, &err); break; }
            if (!ok) { QMessageBox::warning(this, "View", QString("%1: %2").arg(s.name, err)); continue; }
            (new ViewerDialog("C:\\" + s.name, data, this))->show();
            ++opened;
        }
    } else {
        for (const Sel &s : selectedObjects()) {
            if (s.isDir || opened >= 8) continue;
            Dir d;
            QByteArray data;
            bool ok = m_img.readDir(s.parent, &d, &err) && d.indexOf(s.name) >= 0 &&
                      m_img.readFile(d.entries[d.indexOf(s.name)], &data, &err);
            if (!ok) { QMessageBox::warning(this, "View", QString("%1: %2").arg(s.name, err)); continue; }
            (new ViewerDialog(s.name, data, this))->show();
            ++opened;
        }
    }
    if (!opened) statusBar()->showMessage("Select a file to view.", 4000);
}

// --------------------------------------------------------------- importing

void MainWindow::onDosDropped(const QStringList &paths, QTreeWidgetItem *target) {
    quint32 dir = dosTargetDir(target);
    if (!dropImages(PaneDos, paths, [this, dir](const QStringList &r) { dosImportPaths(r, dir); }))
        dosImportPaths(paths, dir);
}

void MainWindow::importSummary(const ImportCtx &c, bool) {
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

void MainWindow::dosImportPaths(const QStringList &paths, quint32 dir) {
    if (!m_dos.attached()) return;
    if (!dosEditable()) {
        QMessageBox::warning(this, "Read-only", "The DOS partition is read-only (image read-only, or the partition has errors).");
        return;
    }
    ImportCtx c;
    for (const QString &p : paths) {
        if (c.cancelled) break;
        if (QFileInfo(p).isDir()) dosImportDir(p, dir, c);
        else dosImportFile(p, dir, c);
    }
    refreshDos();
    updateUi();
    importSummary(c, true);
}

void MainWindow::dosImportFile(const QString &path, quint32 dir, ImportCtx &c) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        c.errors << QString("%1: cannot read (%2)").arg(path, f.errorString());
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    dosAddOne(dir, Dos::Volume::fromHost(QFileInfo(path).fileName()), data, path, c);
}

void MainWindow::dosAddOne(quint32 dir, QString name, const QByteArray &data, const QString &src, ImportCtx &c,
                           bool readOnly) {
    QString err;
    QVector<Dos::DEntry> L;
    if (!m_dos.listDir(dir, &L, &err)) { c.errors << QString("%1: %2").arg(src, err); return; }
    QString key = QString("D%1:%2").arg(dir).arg(name.toUpper());
    if (c.created.contains(key)) { name = uniqueDosName(name, L, c, dir); key = QString("D%1:%2").arg(dir).arg(name.toUpper()); }
    bool replace = false;
    for (const Dos::DEntry &e : L) {
        if (e.name.compare(name, Qt::CaseInsensitive) != 0) continue;
        if (e.isDir()) { c.errors << QString("%1: a directory called '%2' already exists there").arg(src, name); return; }
        int pol = c.policy;
        if (pol == 0) {
            auto b = QMessageBox::question(
                this, "File already exists",
                QString("'%1' already exists in the target directory%2.\nReplace it with\n%3 ?")
                    .arg(name, (e.attr & Dos::AttrR) ? " and is READ-ONLY" : "", src),
                QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll | QMessageBox::Cancel,
                QMessageBox::No);
            if (b == QMessageBox::Cancel) { c.cancelled = true; return; }
            if (b == QMessageBox::YesToAll) c.policy = pol = 1;
            else if (b == QMessageBox::NoToAll) c.policy = pol = 2;
            else pol = (b == QMessageBox::Yes) ? 1 : 2;
        }
        if (pol == 2) { c.skipped++; return; }
        replace = true;
        break;
    }
    if (!m_dos.addFile(dir, name, data, replace, &err)) { c.errors << QString("%1: %2").arg(src, err); return; }
    if (readOnly) m_dos.modify(dir, name, name, Dos::AttrA | Dos::AttrR, &err);      // locked/read-only in the source
    c.created.insert(key);
    if (replace) c.replaced++; else c.files++;
}

void MainWindow::dosImportDir(const QString &path, quint32 parent, ImportCtx &c) {
    QString name = Dos::Volume::fromHost(QFileInfo(path).fileName().isEmpty() ? QString("_") : QFileInfo(path).fileName(), true);
    QString err;
    QVector<Dos::DEntry> L;
    if (!m_dos.listDir(parent, &L, &err)) { c.errors << QString("%1: %2").arg(path, err); return; }
    QString key = QString("D%1:%2").arg(parent).arg(name.toUpper());
    if (c.created.contains(key)) { name = uniqueDosName(name, L, c, parent); key = QString("D%1:%2").arg(parent).arg(name.toUpper()); }
    quint32 cluster = 0;
    bool found = false;
    for (const Dos::DEntry &e : L)
        if (e.name.compare(name, Qt::CaseInsensitive) == 0) {
            if (!e.isDir()) { c.errors << QString("%1: a file called '%2' already exists there").arg(path, name); return; }
            cluster = e.cluster;
            found = true;
        }
    if (!found) {
        if (!m_dos.makeDir(parent, name, &err, &cluster)) { c.errors << QString("%1: %2").arg(path, err); return; }
        c.dirs++;
    }
    c.created.insert(key);
    const QFileInfoList list = QDir(path).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &e : list) {
        if (c.cancelled) return;
        if (e.isDir()) dosImportDir(e.absoluteFilePath(), cluster, c);
        else dosImportFile(e.absoluteFilePath(), cluster, c);
    }
}

// ------------------------------------------------ new dir / delete / properties

void MainWindow::dosNewDir(const QString &name) {
    QString err;
    if (!m_dos.makeDir(dosTargetDir(m_dosTree->currentItem()), name, &err)) {
        QMessageBox::warning(this, "Cannot create directory", err);
        return;
    }
    refreshDos(); updateUi();
}

void MainWindow::dosCount(quint32 dirCluster, int &files, int &dirs, int &ro, int depth) const {
    if (depth > 32) return;
    QVector<Dos::DEntry> v;
    QString err;
    if (!m_dos.listDir(dirCluster, &v, &err)) return;
    for (const Dos::DEntry &e : v) {
        if (e.name == "." || e.name == "..") continue;
        if (e.attr & Dos::AttrR) ++ro;
        if (e.isDir()) { ++dirs; dosCount(e.cluster, files, dirs, ro, depth + 1); }
        else ++files;
    }
}

void MainWindow::dosDelete() {
    QVector<DSel> todo;
    for (const DSel &s : dosSelected()) {
        bool covered = false;
        for (QTreeWidgetItem *p = s.item->parent(); p; p = p->parent())
            if (p->isSelected()) { covered = true; break; }
        if (!covered) todo.append(s);
    }
    if (todo.isEmpty()) return;
    int files = 0, dirs = 0, ro = 0;
    for (const DSel &s : todo) {
        if (s.isDir) { ++dirs; dosCount(s.cluster, files, dirs, ro, 0); } else ++files;
        if (s.item->text(1).contains('R')) ++ro;
    }
    QString msg = QString("Delete %1 file(s) and %2 director%3 from the DOS partition?").arg(files).arg(dirs).arg(dirs == 1 ? "y" : "ies");
    if (ro) msg += QString("\n\n%1 of these are marked read-only.").arg(ro);
    if (QMessageBox::question(this, "Delete", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QStringList errors;
    for (const DSel &s : todo) {
        QString err;
        if (!m_dos.remove(s.parent, s.name, &err)) errors << QString("%1: %2").arg(s.name, err);
    }
    refreshDos(); updateUi();
    if (!errors.isEmpty()) QMessageBox::warning(this, "Delete", errors.join("\n"));
}

void MainWindow::dosProps() {
    QVector<DSel> sel = dosSelected();
    if (sel.isEmpty()) return;
    QVector<Dos::DEntry> ents;
    QString err;
    for (const DSel &s : sel) {
        QVector<Dos::DEntry> L;
        bool found = false;
        if (m_dos.listDir(s.parent, &L, &err))
            for (const Dos::DEntry &e : L) if (e.name == s.name) { ents.append(e); found = true; break; }
        if (!found) { QMessageBox::warning(this, "Properties", err.isEmpty() ? "Entry not found" : err); return; }
    }
    bool single = ents.size() == 1;
    QDialog dlg(this);
    dlg.setWindowTitle(single ? "Properties - " + ents[0].name : QString("Properties - %1 items").arg(ents.size()));
    auto *form = new QFormLayout(&dlg);
    QLineEdit *nameEd = nullptr;
    if (single) {
        nameEd = new QLineEdit(ents[0].name);
        nameEd->setMaxLength(12);
        form->addRow("Name (8.3):", nameEd);
    }
    auto mk = [&](const QString &label, int bit) {
        auto *cb = new QCheckBox(label);
        int on = 0;
        for (const Dos::DEntry &e : ents) on += (e.attr & bit) ? 1 : 0;
        if (on == 0) cb->setCheckState(Qt::Unchecked);
        else if (on == ents.size()) cb->setCheckState(Qt::Checked);
        else { cb->setTristate(true); cb->setCheckState(Qt::PartiallyChecked); }
        return cb;
    };
    QCheckBox *cbR = mk("R  - read-only", Dos::AttrR), *cbH = mk("H  - hidden", Dos::AttrH),
              *cbS = mk("S  - system", Dos::AttrS), *cbA = mk("A  - archive", Dos::AttrA);
    form->addRow("Attributes:", cbR);
    form->addRow("", cbH);
    form->addRow("", cbS);
    form->addRow("", cbA);
    if (single && !ents[0].isDir()) form->addRow("Size:", new QLabel(QString("%1 bytes").arg(ents[0].size)));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    QStringList errors;
    for (int i = 0; i < ents.size(); ++i) {
        quint8 attr = ents[i].attr;
        auto apply = [&](QCheckBox *cb, int bit) {
            if (cb->checkState() == Qt::PartiallyChecked) return;
            attr = quint8(cb->checkState() == Qt::Checked ? (attr | bit) : (attr & ~bit));
        };
        apply(cbR, Dos::AttrR); apply(cbH, Dos::AttrH); apply(cbS, Dos::AttrS); apply(cbA, Dos::AttrA);
        QString nn = nameEd ? nameEd->text().trimmed() : ents[i].name;
        if (!m_dos.modify(sel[i].parent, sel[i].name, nn, attr, &err)) errors << QString("%1: %2").arg(sel[i].name, err);
    }
    refreshDos(); updateUi();
    if (!errors.isEmpty()) QMessageBox::warning(this, "Properties", errors.join("\n"));
}

// --------------------------------------------------------------- extracting

void MainWindow::dosExtractEntry(const Dos::DEntry &e, quint32 /*cluster*/, const QString &hostDir, QStringList &errors) {
    QString host = hostSafeName(e.name);
    QString err;
    if (e.isDir()) {
        QString sub = hostDir + "/" + host;
        if (!QDir().mkpath(sub)) { errors << "cannot create folder " + sub; return; }
        QVector<Dos::DEntry> v;
        if (!m_dos.listDir(e.cluster, &v, &err)) { errors << QString("%1: %2").arg(e.name, err); return; }
        for (const Dos::DEntry &c : v)
            if (c.name != "." && c.name != "..") dosExtractEntry(c, e.cluster, sub, errors);
        return;
    }
    QByteArray data;
    if (!m_dos.readFile(e, &data, &err)) { errors << QString("%1: %2").arg(e.name, err); return; }
    QFile f(hostDir + "/" + host);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size())
        errors << QString("%1: %2").arg(host, f.errorString());
}

void MainWindow::dosExtract() {
    AppSettings s;
    QString dest = QFileDialog::getExistingDirectory(this, "Extract DOS files to folder", s.value("extractDir").toString());
    if (dest.isEmpty()) return;
    s.setValue("extractDir", dest);
    QVector<Dos::DEntry> list;
    QString err;
    QVector<DSel> sel = dosSelected();
    if (sel.isEmpty()) {
        QVector<Dos::DEntry> v;
        if (!m_dos.listDir(0, &v, &err)) { QMessageBox::warning(this, "Extract", err); return; }
        list = v;
    } else {
        for (const DSel &x : sel) {
            QVector<Dos::DEntry> L;
            if (m_dos.listDir(x.parent, &L, &err))
                for (const Dos::DEntry &e : L) if (e.name == x.name) { list.append(e); break; }
        }
    }
    for (const Dos::DEntry &e : list)
        if (QFileInfo::exists(dest + "/" + hostSafeName(e.name))) {
            if (QMessageBox::question(this, "Extract", "Some files already exist in that folder. Overwrite them?") != QMessageBox::Yes) return;
            break;
        }
    QStringList errors;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const Dos::DEntry &e : list) dosExtractEntry(e, 0, dest, errors);
    QApplication::restoreOverrideCursor();
    if (errors.isEmpty()) statusBar()->showMessage(QString("Extracted %1 item(s) to %2").arg(list.size()).arg(dest), 6000);
    else QMessageBox::warning(this, "Extract", errors.join("\n"));
}

// ------------------------------------------------------------------ renaming

void MainWindow::renameSelected() {
    if (dfsSide() >= 0) { dfsRename(dfsSide()); return; }
    bool dos = dosActive();
    QList<QTreeWidgetItem *> items = (dos ? m_dosTree : m_tree)->selectedItems();
    if (items.size() == 1 && items[0]->data(0, RoleIsRoot).toBool()) { renameVolume(); return; }
    if (items.size() != 1) {
        statusBar()->showMessage("Select a single file or directory to rename.", 4000);
        return;
    }
    QTreeWidgetItem *it = items[0];
    quint32 parent = it->data(0, RoleParent).toUInt();
    QString old = it->data(0, RoleName).toString(), err, why;
    bool ok = false;
    QString n = QInputDialog::getText(this, "Rename", dos ? "New name (8.3):" : "New name (max 10 characters):",
                                      QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || n.isEmpty() || n == old) return;
    if (dos) {
        QVector<Dos::DEntry> L;
        quint8 attr = 0;
        if (m_dos.listDir(parent, &L, &err))
            for (const Dos::DEntry &e : L) if (e.name == old) attr = e.attr;
        if (!m_dos.modify(parent, old, n, attr, &err)) { QMessageBox::warning(this, "Rename", err); return; }
        refreshDos();
    } else {
        if (!isValidName(n, &why)) { QMessageBox::warning(this, "Rename", QString("'%1': %2").arg(n, why)); return; }
        Dir d;
        if (!m_img.readDir(parent, &d, &err) || d.indexOf(old) < 0) { QMessageBox::warning(this, "Rename", err); return; }
        Entry e = d.entries[d.indexOf(old)];
        e.setName(n);
        if (!m_img.replaceEntry(parent, old, e, &err)) { QMessageBox::warning(this, "Rename", err); return; }
        refreshTree();
    }
    updateUi();
}

void MainWindow::renameVolume() {
    if (dfsSide() >= 0) { dfsRenameVolume(dfsSide()); return; }
    QString err;
    bool ok = false;
    if (dosActive()) {
        QString n = QInputDialog::getText(this, "Rename volume", "DOS volume label (max 11 characters, blank to remove):",
                                          QLineEdit::Normal, m_dos.label(), &ok);
        if (!ok) return;
        if (!m_dos.setLabel(n, &err)) { QMessageBox::warning(this, "Rename volume", err); return; }
        refreshDos();
    } else {
        Dir root;
        if (!m_img.readDir(RootSector, &root, &err)) { QMessageBox::warning(this, "Rename volume", err); return; }
        QString n = QInputDialog::getText(this, "Rename disc", "Disc name (root directory title, max 10 characters):",
                                          QLineEdit::Normal, root.title(), &ok);
        if (!ok) return;
        if (!m_img.setDiscTitle(n.trimmed(), &err)) { QMessageBox::warning(this, "Rename disc", err); return; }
        refreshTree();
    }
    updateUi();
}
