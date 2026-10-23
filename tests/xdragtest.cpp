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
// Real drag-and-drop out of a disc window, on a real X server, driven by moving the real pointer with xdotool.
// Linux / X11 only:   xvfb-run -a -s "-screen 0 1280x800x24" xdragtest <outdir>       (needs Xvfb and xdotool)
// "xdragtest explorer <accept> <log>" is a stand-in for Explorer in a separate process: it asks for the dragged
// files as soon as a drag enters it (as Explorer does on Windows) and accepts or refuses the drop.
// Scenarios: dropped on it (files arrive at the drop, status bar says "Dragged out"); passed over it and let go back
// on the disc window (files were written, but no "Dragged out"); dropped on it while it refuses (on X11 Qt reports
// the action from the last hover reply, so the message can't be relied on here - only printed).
// It caught a bug the offscreen tests can't: on X11 the target fetches the files after QDrag::exec() returns, so the
// drag must not be deleted when exec() returns.
#include "mainwindow.h"
#include "newdisc.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTimer>
#include <cstdio>
#include <functional>
using namespace Adfs;
static void logLine(const QString &f, const QString &s) { QFile l(f); l.open(QIODevice::Append); l.write((s + "\n").toUtf8()); }
namespace { namespace {
struct FakeExplorer : QWidget {
    bool accept; QString log;
    FakeExplorer(bool a, QString l) : accept(a), log(l) { setAcceptDrops(true); }
    void dragEnterEvent(QDragEnterEvent *e) override { logLine(log, QString("asked %1").arg(e->mimeData()->urls().size())); e->acceptProposedAction(); }
    void dragMoveEvent(QDragMoveEvent *e) override { e->acceptProposedAction(); }
    void dropEvent(QDropEvent *e) override {
        if (accept) { QByteArray raw = e->mimeData()->data("text/uri-list"); logLine(log, QString("dropped %1 (%2 bytes of text/uri-list)").arg(e->mimeData()->urls().size()).arg(raw.size())); e->setDropAction(Qt::CopyAction); e->accept(); }
        else { logLine(log, "refused"); e->ignore(); }
    }
};
} }
static QTreeWidgetItem *findIn(QTreeWidgetItem *i, const QString &n) {
    if (i->data(0, RoleName).toString() == n && !i->data(0, RoleIsRoot).toBool()) return i;
    for (int k = 0; k < i->childCount(); ++k) if (QTreeWidgetItem *r = findIn(i->child(k), n)) return r;
    return nullptr;
}
static QTreeWidgetItem *findItemNamed(QTreeWidget *t, const QString &n) { return t->topLevelItemCount() ? findIn(t->topLevelItem(0), n) : nullptr; }
static void wait(int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20); }
int main(int argc, char **argv) {
    QApplication app(argc, argv); setvbuf(stdout, nullptr, _IONBF, 0);
    if (QString(argv[1]) == "explorer") {
        FakeExplorer ex(QString(argv[2]) == "1", argv[3]); ex.setGeometry(700, 50, 300, 300); ex.show();
        return app.exec();
    }
    QString dir = argv[1], e, adl = dir + "/d.adl";
    NewDisc::create(adl, NewDisc::AdfsL, "X", false, &e);
    { Image im; im.open(adl, &e, 0, detectDisk(adl).sides); im.addFile(RootSector, "Elite", QByteArray(5000, 'x'), 0, 0, FlagR | FlagW, false, &e); im.commit(&e, false, nullptr); }
    int fails = 0;
    const char *what[] = {"dropped on 'Explorer'", "passed over 'Explorer', let go back on the disc window", "dropped on 'Explorer' that refuses it"};
    for (int sc = 0; sc < 3; ++sc) {
        QString log = dir + QString("/log%1.txt").arg(sc); QFile::remove(log);
        QProcess ex; ex.start(argv[0], {"explorer", sc == 2 ? "0" : "1", log}); wait(1500);
        MainWindow w; w.setGeometry(20, 50, 600, 400); w.show(); w.openImage(adl); wait(800);
        QTreeWidgetItem *it = findItemNamed(w.tree(), "Elite"); w.tree()->setCurrentItem(it); it->setSelected(true);
        QPoint p = w.tree()->viewport()->mapToGlobal(w.tree()->visualItemRect(it).center());
        QString msg; QObject::connect(w.tree(), &DropTree::dragOutFinished, [&](int f, int, const QStringList &) { msg = QString("\"Dragged out %1 file(s)\"").arg(f); });
        // press on the row and move the real pointer: the tree starts the drag itself, as for a person
        QString path = QString("xdotool mousemove %1 %2 mousedown 1; sleep 0.3; xdotool mousemove %3 %4; sleep 0.3; ").arg(p.x()).arg(p.y()).arg(p.x() + 30).arg(p.y() + 5) +
                       "xdotool mousemove 850 200; sleep 0.5; xdotool mousemove 860 210; sleep 0.5; " +
                       (sc == 1 ? QString("xdotool mousemove %1 %2; sleep 0.5; xdotool mousemove %3 %4; sleep 0.5; ").arg(p.x()).arg(p.y() + 40).arg(p.x() + 5).arg(p.y() + 45) : QString()) +
                       "xdotool mouseup 1";
        QProcess drv; drv.start("sh", {"-c", path});
        while (drv.state() != QProcess::NotRunning) wait(50);
        wait(500);
        ex.kill(); ex.waitForFinished();
        QFile l(log); l.open(QIODevice::ReadOnly); QString got = QString::fromUtf8(l.readAll()).simplified();
        bool asked = got.contains("asked 2");
        printf("%-58s Explorer: %-22s status bar: %s\n", what[sc], qPrintable(got), msg.isEmpty() ? "(nothing)" : qPrintable(msg));
        if (!asked || (sc < 2 && (sc == 0) != !msg.isEmpty()) || (sc == 0 && !got.contains("dropped 2"))) ++fails;
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails;
}
