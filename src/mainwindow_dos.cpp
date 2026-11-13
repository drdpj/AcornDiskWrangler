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
#include <diskwrangler/app/appsettings.h>
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

// View, on the window kit's pattern for copying out: each selected file is read by exportItems (one at a time, so a
// file that can't be read says so and the rest still open). At most 8 viewers, as ever.
void MainWindow::viewSelected() {
    int opened = 0;
    for (QTreeWidgetItem *it : currentTree()->selectedItems()) {
        if (opened >= 8) break;
        const dw::ui::PaneRow r = dw::ui::VolumePane::rowOf(it);
        if (!r.isFile()) continue;
        const int pane = paneOf(r.mount);
        const bool dfs = pane == PaneDfs0 || pane == PaneDfs1;
        const QString leaf = r.path.name();                  // DFS: "D.NAME"
        QVector<dw::TransferItem> one;
        QString err;
        if (!exportItems({Item(r.mount, r.path)}, &one, &err)) {
            QMessageBox::warning(this, "View", QString("%1: %2").arg(dfs ? leaf.mid(2) : leaf, err));
            continue;
        }
        (new ViewerDialog(pane == PaneDos ? "C:\\" + leaf : leaf, one[0].data, this))->show();
        ++opened;
    }
    if (!opened) statusBar()->showMessage("Select a file to view.", 4000);
}

// --------------------------------------------------------------- importing

void MainWindow::onDosDropped(const QStringList &paths, QTreeWidgetItem *target) {
    quint32 dir = dosTargetDir(target);
    if (!dropImages(PaneDos, paths, [this, dir](const QStringList &r) { dosImportPaths(r, dir); }))
        dosImportPaths(paths, dir);
}

void MainWindow::dosImportPaths(const QStringList &paths, quint32 dir) {
    if (!m_dos.attached()) return;
    if (!dosEditable()) {
        QMessageBox::warning(this, "Read-only", "The DOS partition is read-only (image read-only, or the partition has errors).");
        return;
    }
    hostImport(PaneDos, dir, paths, false);            // DOS has never read .inf files: they are files like any other
}

// ------------------------------------------------ new dir / delete / properties

void MainWindow::dosCount(quint32 dirCluster, int &files, int &dirs, int &ro, int depth) const {
    if (depth > 32) return;
    QVector<dw::fat::DEntry> v;
    QString err;
    if (!m_dos.listDir(dirCluster, &v, &err)) return;
    for (const dw::fat::DEntry &e : v) {
        if (e.name == "." || e.name == "..") continue;
        if (e.attr & dw::fat::AttrR) ++ro;
        if (e.isDir()) { ++dirs; dosCount(e.cluster, files, dirs, ro, depth + 1); }
        else ++files;
    }
}

bool MainWindow::dosProperties(const QVector<Item> &items, PropertiesEdit *edit) {
    QVector<dw::fat::DEntry> ents;
    QString err;
    for (const Item &i : items) {
        const QTreeWidgetItem *row = m_dosTree->findRow(i.mount, i.path);
        const quint32 parent = row ? row->data(0, RoleParent).toUInt() : 0;
        const QString name = row ? row->data(0, RoleName).toString() : i.path.name();
        QVector<dw::fat::DEntry> L;
        bool found = false;
        if (m_dos.listDir(parent, &L, &err))
            for (const dw::fat::DEntry &e : L) if (e.name == name) { ents.append(e); found = true; break; }
        if (!found) { QMessageBox::warning(this, "Properties", err.isEmpty() ? "Entry not found" : err); return false; }
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
        for (const dw::fat::DEntry &e : ents) on += (e.attr & bit) ? 1 : 0;
        if (on == 0) cb->setCheckState(Qt::Unchecked);
        else if (on == ents.size()) cb->setCheckState(Qt::Checked);
        else { cb->setTristate(true); cb->setCheckState(Qt::PartiallyChecked); }
        return cb;
    };
    QCheckBox *cbR = mk("R  - read-only", dw::fat::AttrR), *cbH = mk("H  - hidden", dw::fat::AttrH),
              *cbS = mk("S  - system", dw::fat::AttrS), *cbA = mk("A  - archive", dw::fat::AttrA);
    form->addRow("Attributes:", cbR);
    form->addRow("", cbH);
    form->addRow("", cbS);
    form->addRow("", cbA);
    if (single && !ents[0].isDir()) form->addRow("Size:", new QLabel(QString("%1 bytes").arg(ents[0].size)));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;

    for (int i = 0; i < ents.size(); ++i) {
        PropertyChange c;
        c.item = items[i];
        auto bit = [&](QCheckBox *cb, int b) { return cb->checkState() == Qt::PartiallyChecked ? (ents[i].attr & b) != 0 : cb->checkState() == Qt::Checked; };
        c.attrs["readonly"] = bit(cbR, dw::fat::AttrR);
        c.attrs["hidden"] = bit(cbH, dw::fat::AttrH);
        c.attrs["system"] = bit(cbS, dw::fat::AttrS);
        c.attrs["archive"] = bit(cbA, dw::fat::AttrA);
        if (nameEd && nameEd->text().trimmed() != ents[i].name) c.to = items[i].path.parent().child(nameEd->text().trimmed());
        edit->changes << c;
    }
    return true;
}

// ------------------------------------------------------------------ renaming

// Rename, on the kit's pattern for a change: this program's dialog and its checks (ADFS names, DOS 8.3, a DFS name or
// D.NAME), then one Disc::transact over Volume::rename, and the pane's rows again.
void MainWindow::renameSelected() {
    if (dfsSide() >= 0) { dfsRename(); return; }
    bool dos = dosActive();
    QList<QTreeWidgetItem *> items = (dos ? m_dosTree : m_tree)->selectedItems();
    if (items.size() == 1 && items[0]->data(0, RoleIsRoot).toBool()) { renameVolume(); return; }
    if (items.size() != 1) {
        statusBar()->showMessage("Select a single file or directory to rename.", 4000);
        return;
    }
    const dw::ui::PaneRow row = dw::ui::VolumePane::rowOf(items[0]);
    QString old = items[0]->data(0, RoleName).toString(), why;
    bool ok = false;
    QString n = QInputDialog::getText(this, "Rename", dos ? "New name (8.3):" : "New name (max 10 characters):",
                                      QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || n.isEmpty() || n == old) return;
    if (!dos && !isValidName(n, &why)) { QMessageBox::warning(this, "Rename", QString("'%1': %2").arg(n, why)); return; }
    renameRow(row, row.path.parent().child(n));
    updateUi();
}

// The change itself: the row's entry moved to `to` (the same folder, another name), in one transaction. A refusal is
// said with the kit's message and the rows are left alone; after a change they are made again.
bool MainWindow::renameRow(const dw::ui::PaneRow &row, const dw::fs::Path &to) {
    if (!row.valid()) return false;
    QString err;
    bool ok = true;
    if (!writable(row.mount)) {
        err = "read-only: " + readOnlyReason(row.mount);
        ok = false;
    } else {
        ok = m_doc.disc().transact([&](QString *e) {
            dw::fs::Volume *v = m_doc.disc().volume(row.mount);
            if (!v) { *e = "no such volume"; return false; }
            return v->rename(row.path, to, e);
        }, &err);
    }
    if (!ok) { message("Rename", err); return false; }       // nothing changed: the rows stay as they are
    refreshChanged({row.mount});
    return true;
}
