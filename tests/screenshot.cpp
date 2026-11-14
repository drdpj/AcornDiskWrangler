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
// Renders every kind of pane and saves PNGs, for eyeballing layout/alignment.
//   screenshot <outdir> <dsd> [<image> ...]      (images are opened read/write: pass copies)
// The DSD gets some host files dropped on drive 0 (dfs.png), and every dialog the main window opens for it is
// saved as dialog_<slot>.png. Every tab of every further image is saved
// as <outdir>/<image file name>_<tab>.png, at a wide (1300px) and at the default window size, so
// both the column stretch and the narrow-window case can be checked.
#include "mainwindow.h"
#include "windowhelp.h"
#include <QApplication>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeData>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

static void pump() { for (int i = 0; i < 5; ++i) QCoreApplication::processEvents(); }
static void dropFiles(MainWindow &w, QTreeWidget *tree, const QStringList &paths) {
    w.tabWidget()->setCurrentWidget(tree);
    QMimeData m; QList<QUrl> u; for (auto &p : paths) u << QUrl::fromLocalFile(p); m.setUrls(u);
    QPoint pt(5, tree->viewport()->height() - 5);
    QDragEnterEvent en(pt, Qt::CopyAction, &m, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(tree->viewport(), &en);
    QDragMoveEvent mv(pt, Qt::CopyAction, &m, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(tree->viewport(), &mv);
    QDropEvent ev(QPointF(pt), Qt::CopyAction, &m, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(tree->viewport(), &ev);
    pump();
}
static void shoot(MainWindow &w, QWidget *tab, const QString &file) {
    w.tabWidget()->setCurrentWidget(tab); pump();
    w.grab().save(file); printf("wrote %s\n", qPrintable(file));
}
static QString slug(QString t) {
    for (QChar &c : t) if (!c.isLetterOrNumber()) c = '_';
    return t.toLower();
}
// The first file row of a tree (not a root or a directory row), selected so that a slot acting on the
// selection has something to act on.
static void selectFirstFile(QTreeWidget *t) {
    QList<QTreeWidgetItem *> q;
    for (int i = 0; t && i < t->topLevelItemCount(); ++i) q << t->topLevelItem(i);
    while (!q.isEmpty()) {
        QTreeWidgetItem *it = q.takeFirst();
        if (!it->data(0, RoleIsDir).toBool() && !it->data(0, RoleIsRoot).toBool()) { t->setCurrentItem(it); return; }
        for (int i = 0; i < it->childCount(); ++i) q << it->child(i);
    }
}
static QList<QDialog *> shownDialogs() {
    QList<QDialog *> l;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (QDialog *d = qobject_cast<QDialog *>(w)) if (d->isVisible()) l << d;
    return l;
}
static void saveDialog(QWidget *d, const QString &file) {
    QDialog *dlg = qobject_cast<QDialog *>(d);
    d->resize(dlg ? 1120 : 1000, 640);
    pump();
    d->grab().save(file); printf("wrote %s\n", qPrintable(file));
    if (dlg) dlg->reject(); else d->close();
}
// Calls the slot and saves the dialog it opens as dialog_<slot>.png, then closes it. A modal dialog is caught
// while the slot waits for it; the viewer opens without waiting, so it is caught once the slot has returned.
// Returns false if the slot opened no dialog.
static bool dialogShot(MainWindow &w, const char *slot, const QString &outdir) {
    const QString file = outdir + "/dialog_" + slot + ".png";
    QTimer t; t.setInterval(20);
    bool taken = false;
    QObject::connect(&t, &QTimer::timeout, [&] {
        QWidget *d = QApplication::activeModalWidget();
        if (!d || taken) return;
        taken = true;
        saveDialog(d, file);
    });
    const QList<QDialog *> before = shownDialogs();
    t.start();
    QMetaObject::invokeMethod(&w, slot);
    t.stop();
    if (!taken)
        for (QDialog *d : shownDialogs())
            if (!taken && !before.contains(d)) { taken = true; saveDialog(d, file); }
    if (!taken) printf("FAIL: %s opened no dialog\n", slot);
    pump();
    return taken;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("acorndiskwrangler-shots"); QApplication::setApplicationName("s");
    if (argc < 3) { printf("usage: screenshot <outdir> <dsd> [<image> ...]\n"); return 2; }
    QString out = argv[1];
    MainWindow a; a.resize(1300, 560); a.show();
    a.openFile(argv[2]); pump();
    dropFiles(a, wt::dfsTree(a), {"/tmp/drop/proj/hello.txt", "/tmp/drop/proj/hello.txt.inf", "/tmp/drop/proj/prog.bas", "/tmp/drop/proj/prog.bas.inf", "/tmp/drop/proj/test.bas", "/tmp/drop/proj/sub"});
    shoot(a, wt::dfsTree(a), out + "/dfs.png");
    // the dialogs, for the first image. A DFS disc's files are in the DFS tab, not tree() (the ADFS tab), so the
    // first file row is selected in the visible tab
    bool dialogsOk = true;
    selectFirstFile(qobject_cast<QTreeWidget *>(a.tabWidget()->currentWidget()));
    for (const char *slot : {"newImageDialog", "editProperties", "viewSelected", "bootOptionDialog", "checkImage", "about"})
        dialogsOk = dialogShot(a, slot, out) && dialogsOk;
    for (int i = 3; i < argc; ++i) {
        MainWindow w; w.resize(1300, 560); w.show();
        w.openFile(argv[i]); pump();
        QString base = out + "/" + QFileInfo(argv[i]).fileName();
        for (int t = 0; t < w.tabWidget()->count(); ++t)
            shoot(w, w.tabWidget()->widget(t), base + "_" + slug(w.tabWidget()->tabText(t)) + ".png");
        MainWindow n; n.show();                  // default size, clamped to the minimum width
        n.openFile(argv[i]); pump();
        for (int t = 0; t < n.tabWidget()->count(); ++t)
            shoot(n, n.tabWidget()->widget(t), base + "_" + slug(n.tabWidget()->tabText(t)) + "_narrow.png");
    }
    return dialogsOk ? 0 : 1;
}
