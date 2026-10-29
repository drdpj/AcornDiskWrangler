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
// The DSD gets some host files dropped on drive 0 (dfs.png). Every tab of every further image is saved
// as <outdir>/<image file name>_<tab>.png, at a wide (1300px) and at the default window size, so
// both the column stretch and the narrow-window case can be checked.
#include "mainwindow.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QTabWidget>
#include <cstdio>

static void pump() { for (int i = 0; i < 5; ++i) QCoreApplication::processEvents(); }
static void dropFiles(MainWindow &w, QTreeWidget *tree, const QStringList &paths) {
    w.tabs()->setCurrentWidget(tree);
    QMimeData m; QList<QUrl> u; for (auto &p : paths) u << QUrl::fromLocalFile(p); m.setUrls(u);
    QPoint pt(5, tree->viewport()->height() - 5);
    QDragEnterEvent en(pt, Qt::CopyAction, &m, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(tree->viewport(), &en);
    QDragMoveEvent mv(pt, Qt::CopyAction, &m, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(tree->viewport(), &mv);
    QDropEvent ev(QPointF(pt), Qt::CopyAction, &m, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(tree->viewport(), &ev);
    pump();
}
static void shoot(MainWindow &w, QWidget *tab, const QString &file) {
    w.tabs()->setCurrentWidget(tab); pump();
    w.grab().save(file); printf("wrote %s\n", qPrintable(file));
}
static QString slug(QString t) {
    for (QChar &c : t) if (!c.isLetterOrNumber()) c = '_';
    return t.toLower();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("acorndiskwrangler-shots"); QApplication::setApplicationName("s");
    if (argc < 3) { printf("usage: screenshot <outdir> <dsd> [<image> ...]\n"); return 2; }
    QString out = argv[1];
    MainWindow a; a.resize(1300, 560); a.show();
    a.openImage(argv[2]); pump();
    dropFiles(a, a.dfsTree(), {"/tmp/drop/proj/hello.txt", "/tmp/drop/proj/hello.txt.inf", "/tmp/drop/proj/prog.bas", "/tmp/drop/proj/prog.bas.inf", "/tmp/drop/proj/test.bas", "/tmp/drop/proj/sub"});
    shoot(a, a.dfsTree(), out + "/dfs.png");
    for (int i = 3; i < argc; ++i) {
        MainWindow w; w.resize(1300, 560); w.show();
        w.openImage(argv[i]); pump();
        QString base = out + "/" + QFileInfo(argv[i]).fileName();
        for (int t = 0; t < w.tabs()->count(); ++t)
            shoot(w, w.tabs()->widget(t), base + "_" + slug(w.tabs()->tabText(t)) + ".png");
        MainWindow n; n.show();                  // default size, clamped to the minimum width
        n.openImage(argv[i]); pump();
        for (int t = 0; t < n.tabs()->count(); ++t)
            shoot(n, n.tabs()->widget(t), base + "_" + slug(n.tabs()->tabText(t)) + "_narrow.png");
    }
    return 0;
}
