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
// saveastest <work dir> <acorndw> <DOS Plus 640K disc, for its boot track> [<folder of .td0 files>]
//
// File > Save As in the main window (step 5j, deliberate change 8): a floppy can be saved as ImageDisk, standard or
// extended DSK, or a raw image, and the file written is byte for byte what `acorndw convert` writes, because both go
// through Adfs::OpenedDisc::writeCopy. For every New-disc preset (the double-sided ones in both side orders):
//  - a change is staged in the window, then Save As to each kind; the reference is the same change saved into a raw
//    copy by Adfs::OpenedDisc and converted by acorndw; the bytes must be equal, the window must then show the copy
//    (that kind, nothing unsaved, writable), the original must be untouched, and the copy must be detected as the
//    same kind of disc as the original;
//  - an ImageDisk copy opened in the window, saved as each other kind with nothing staged, against acorndw convert.
// Also: TeleDisk files (read-only) are offered only the writable kinds and become writable once saved as one; a hard
// disc keeps the old Save As dialog (its own kind only); a copy of another kind over the image's own file is refused;
// the document's own saveAs (dw::app::Document) gives the same bytes. The clock is fixed (ImageDisk headers carry it).
#include "mainwindow.h"
#include "newdisc.h"
#include <diskwrangler/app/clock.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <cstdio>
#include <functional>

using dw::device::Kind;
static int fails = 0;
static int checks = 0;
#define CHECK(c, what) do { ++checks; if (!(c)) { printf("FAIL line %d: %s  [%s]\n", __LINE__, #c, qPrintable(QString(what))); ++fails; } } while (0)

static const qint64 Epoch = 946684800;          // 2000-01-01, as the golden cases use (SOURCE_DATE_EPOCH)

static QByteArray slurp(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
static void pump() { for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); } }

// Runs act on the first modal dialog of type T to appear within 3 s.
template <class T> static void whenModal(std::function<void(T *)> act, const char *missing) {
    auto *t = new QTimer; auto *waited = new int(0); t->setInterval(30);
    QObject::connect(t, &QTimer::timeout, [t, waited, act, missing] {
        if (T *w = qobject_cast<T *>(QApplication::activeModalWidget())) { t->stop(); t->deleteLater(); delete waited; act(w); return; }
        if ((*waited += 30) >= 3000) { printf("  (%s)\n", missing); ++fails; t->stop(); t->deleteLater(); delete waited; } });
    t->start();
}

static QString g_acorndw;
static bool cli(const QStringList &args, QString *out = nullptr) {
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SOURCE_DATE_EPOCH", QString::number(Epoch));
    env.insert("TZ", "UTC");
    p.setProcessEnvironment(env);
    p.start(g_acorndw, args);
    p.waitForFinished(60000);
    if (out) *out = QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError());
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

struct KindDef { Kind kind; QString opt; QString filter; QString ext; };
static const QVector<KindDef> kinds = {
    {Kind::Raw, "raw", "Raw image", "img"}, {Kind::Imd, "imd", "ImageDisk", "imd"},
    {Kind::Dsk, "dsk", "Standard DSK", "dsk"}, {Kind::Edsk, "edsk", "Extended DSK", "dsk"}};

// File > Save As, the dialog answered: the filter that starts with filterStart, the file path. Returns the filters
// the dialog offered and the one it started on.
static QStringList g_offered;
static QString g_startFilter, g_suggested;
static void saveAs(MainWindow &w, const QString &path, const QString &filterStart) {
    g_offered.clear(); g_startFilter.clear(); g_suggested.clear();
    whenModal<QFileDialog>([path, filterStart](QFileDialog *fd) {
        g_offered = fd->nameFilters();
        g_startFilter = fd->selectedNameFilter();
        g_suggested = fd->selectedFiles().value(0);
        if (filterStart.isEmpty()) { fd->reject(); return; }
        for (const QString &f : fd->nameFilters()) if (f.startsWith(filterStart)) fd->selectNameFilter(f);
        QTimer::singleShot(60, fd, [fd, path] {     // after the dialog has put the filter's suffix on its name
            if (auto *name = fd->findChild<QLineEdit *>("fileNameEdit")) name->setText(path);
            else fd->selectFile(path);
            QMetaObject::invokeMethod(fd, "accept");
        });
    }, "no Save as dialog");
    QMetaObject::invokeMethod(&w, "saveAsDialog", Qt::DirectConnection);
    pump();
}

// A file written to the first writable mount, staged (nothing saved).
static bool stageHello(dw::fs::Disc &disc, const std::function<bool(int)> &writable, QString *err) {
    for (int m = 0; m < disc.mounts().size(); ++m) {
        if (!writable(m)) continue;
        dw::fs::Volume *v = disc.volume(m);
        return disc.transact([&](QString *e) {
            return v->write(dw::fs::Path(), "HELLO", QByteArray("Hello from saveastest\r", 22), dw::fs::AttrValues(), dw::now(), false, e);
        }, err);
    }
    *err = "no writable mount";
    return false;
}

static bool hasHello(dw::fs::Disc &disc) {
    for (int m = 0; m < disc.mounts().size(); ++m) {
        QVector<dw::fs::Entry> l; QString e;
        if (disc.volume(m) && disc.volume(m)->list(dw::fs::Path(), &l, &e))
            for (const dw::fs::Entry &x : l) if (x.name.section('.', -1).compare("HELLO", Qt::CaseInsensitive) == 0) return true;   // DFS: $.HELLO
    }
    return false;
}

// what detection makes of a file: the kind of disc and its size (the description also names the container)
static QString what(const QString &p, int force = -1) {
    const DiskInfo i = detectDisk(p, force);
    return QString("%1 %2 %3").arg(int(i.kind)).arg(i.fullSize).arg(i.description.section(", in a", 0, 0).section(", sequential", 0, 0));
}

int main(int argc, char **argv) {
    qputenv("TZ", "UTC");
    QApplication app(argc, argv);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setOrganizationName("acorndiskwrangler-test"); QApplication::setApplicationName("saveastest");
    if (argc < 4) { printf("usage: saveastest <work dir> <acorndw> <DOS Plus 640K disc> [<folder of .td0 files>]\n"); return 2; }
    const QString work = QFileInfo(argv[1]).absoluteFilePath();
    g_acorndw = QFileInfo(argv[2]).absoluteFilePath();
    const QString boot640 = QFileInfo(argv[3]).absoluteFilePath();
    const QString td0dir = argc > 4 ? QString(argv[4]) : QString();
    QDir().mkpath(work);
    dw::setNow(QDateTime::fromSecsSinceEpoch(Epoch));
    QTimer guard; guard.setInterval(3000);       // a modal box still there a tick later is a failure, never a hang
    QPointer<QWidget> seen;
    QObject::connect(&guard, &QTimer::timeout, [&seen] {
        QWidget *m = QApplication::activeModalWidget();
        if (m && m == seen) {
            QString t = m->windowTitle(); if (auto *mb = qobject_cast<QMessageBox *>(m)) t += ": " + mb->text();
            printf("  !! unexpected modal dialog [%s]\n", qPrintable(t.replace('\n', ' '))); ++fails; m->close(); m = nullptr;
        }
        seen = m; });
    guard.start();

    struct Preset { NewDisc::Type type; QString name; QString suffix; bool seq; };
    QVector<Preset> presets;
    for (const NewDisc::TypeInfo &t : NewDisc::types()) {
        presets.append({t.type, t.name, t.suffix, false});
        if (t.doubleSided) presets.append({t.type, t.name, t.suffix, true});
    }
    int saved = 0;
    for (const Preset &pr : presets) {
        const QString tag = QString("%1%2").arg(int(pr.type)).arg(pr.seq ? "s" : "i");
        const QString dir = work + "/" + tag;
        QDir(dir).removeRecursively(); QDir().mkpath(dir);
        const QString base = dir + "/base." + pr.suffix;
        QString err;
        if (!NewDisc::create(base, pr.type, "SAVEAS", pr.seq, &err, pr.type == NewDisc::Acorn640 ? boot640 : QString())) {
            CHECK(false, pr.name + ": create: " + err); continue;
        }
        const int force = pr.seq ? 1 : -1;            // a blank disc can't show its side order: say it
        const QStringList sidesOpt = pr.seq ? QStringList{"--sides=sequential"} : QStringList{};
        const QString kindOfBase = what(base, force);
        printf("%s (%s): %s\n", qPrintable(pr.name), pr.seq ? "sequential" : "interleaved", qPrintable(kindOfBase));

        // the reference: the same change saved into a raw copy by the document the command line uses
        const QString ref = dir + "/ref." + pr.suffix;
        QFile::copy(base, ref);
        {
            Adfs::OpenedDisc od;
            CHECK(od.open(ref, detectDisk(ref, force), &err), pr.name + ": ref open: " + err);
            CHECK(stageHello(od.disc(), [&](int m) { return !od.disc().mounts()[m].readOnly; }, &err), pr.name + ": ref stage: " + err);
            CHECK(od.save(&err), pr.name + ": ref save: " + err);
        }
        for (const KindDef &k : kinds) {
            const QString src = dir + "/src-" + k.opt + "." + pr.suffix;      // the window's file (must stay as it was)
            const QString gui = dir + "/gui-" + k.opt + "." + k.ext, cl = dir + "/cli-" + k.opt + "." + k.ext;
            QFile::remove(src); QFile::copy(base, src);
            QStringList cargs = {"convert", ref, cl, "--container=" + k.opt};
            if (k.kind == Kind::Raw && NewDisc::info(pr.type).doubleSided) cargs << QString("--to-sides=") + (pr.seq ? "sequential" : "interleaved");
            QString out;
            CHECK(cli(sidesOpt + cargs, &out), pr.name + " acorndw " + cargs.join(' ') + ": " + out);
            MainWindow w; w.resize(900, 600); w.show();
            CHECK(w.openFile(src, force), pr.name + ": the window opens it");
            pump();
            auto *doc = static_cast<Adfs::OpenedDisc *>(w.document());
            CHECK(stageHello(doc->disc(), [&](int m) { return w.writable(m); }, &err), pr.name + ": stage: " + err);
            w.refresh();
            CHECK(w.document()->image().dirty(), pr.name + ": a change is staged");
            saveAs(w, gui, k.filter);
            CHECK(g_offered.size() == 4 && g_offered[0].startsWith("ImageDisk") && g_offered[3].startsWith("Raw image"),
                  pr.name + ": the four kinds offered: " + g_offered.join(" | "));
            CHECK(g_startFilter.startsWith("Raw image") && QFileInfo(g_suggested).fileName() == "src-" + k.opt + "-copy." + pr.suffix,
                  pr.name + ": starts on the image's own kind and name: " + g_startFilter + ", " + g_suggested + " (want src-" + k.opt + "-copy." + pr.suffix + ")");
            CHECK(slurp(gui) == slurp(cl) && !slurp(gui).isEmpty(), pr.name + " -> " + k.opt + ": the bytes acorndw convert gives");
            CHECK(QFileInfo(w.document()->image().path()).absoluteFilePath() == gui && w.document()->image().containerKind() == k.kind && !w.document()->image().dirty(),
                  pr.name + " -> " + k.opt + ": the window shows the copy: " + w.document()->image().path());
            CHECK(w.writable(0) || w.document()->info().kind == DiskInfo::AdfsDosHybrid, pr.name + " -> " + k.opt + ": the copy can be changed: " + w.readOnlyReason(0));
            CHECK(hasHello(doc->disc()), pr.name + " -> " + k.opt + ": the change is in the copy");
            CHECK(slurp(src) == slurp(base), pr.name + " -> " + k.opt + ": the original is untouched");
            CHECK(what(gui, k.kind == Kind::Raw ? force : -1) == kindOfBase, pr.name + " -> " + k.opt + ": detected as " + what(gui));
            ++saved;
        }
        // an ImageDisk copy in the window, saved as each other kind (nothing staged)
        const QString imd = dir + "/in.imd";
        CHECK(cli(sidesOpt + QStringList{"convert", base, imd, "--container=imd"}), pr.name + ": acorndw makes the ImageDisk copy");
        for (const KindDef &k : kinds) {
            if (k.kind == Kind::Imd) continue;
            const QString gui = dir + "/fromimd-gui-" + k.opt + "." + k.ext, cl = dir + "/fromimd-cli-" + k.opt + "." + k.ext;
            CHECK(cli({"convert", imd, cl, "--container=" + k.opt}), pr.name + ": acorndw convert from ImageDisk to " + k.opt);
            MainWindow w; w.show();
            CHECK(w.openFile(imd), pr.name + ": the window opens the ImageDisk copy");
            saveAs(w, gui, k.filter);
            CHECK(g_startFilter.startsWith("ImageDisk") && QFileInfo(g_suggested).fileName() == "in-copy.imd", pr.name + ": starts on ImageDisk: " + g_suggested);
            CHECK(slurp(gui) == slurp(cl) && !slurp(gui).isEmpty(), pr.name + ": ImageDisk -> " + k.opt + ": the bytes acorndw convert gives");
            CHECK(w.document()->image().containerKind() == k.kind, pr.name + ": ImageDisk -> " + k.opt + ": the window shows the copy");
            ++saved;
        }
    }

    // the document's own saveAs (dw::app::Document) makes the same file, and is then the copy
    {
        const QString dir = work + "/doc";
        QDir(dir).removeRecursively(); QDir().mkpath(dir);
        const QString base = dir + "/l.adl", out = dir + "/l.imd", cl = dir + "/cli.imd";
        QString err;
        NewDisc::create(base, NewDisc::AdfsL, "DOC", false, &err);
        CHECK(cli({"convert", base, cl, "--container=imd"}), "acorndw convert");
        Adfs::OpenedDisc od;
        od.setWindowRules(true);
        CHECK(od.open(base, detectDisk(base), &err), err);
        CHECK(od.saveAs(out, Kind::Imd, &err), "Document::saveAs: " + err);
        CHECK(slurp(out) == slurp(cl), "Document::saveAs gives acorndw's bytes");
        CHECK(od.path() == out && od.image().containerKind() == Kind::Imd && od.image().editable() && od.disc().mounts().size() == 1,
              "Document::saveAs: the document is the copy");
        Adfs::OpenedDisc hd;
        QFile f(dir + "/hd.dat"); f.open(QIODevice::WriteOnly); f.write(Adfs::makeBlankImage(8192, "HARD")); f.close();
        CHECK(hd.open(dir + "/hd.dat", detectDisk(dir + "/hd.dat"), &err), err);
        QString why;
        CHECK(!hd.convertible(&why) && hd.saveAs(dir + "/hd.imd", Kind::Imd, &err) == false, "a hard disc is not converted: " + why);
        CHECK(!QFile::exists(dir + "/hd.imd"), "and nothing is written");
    }

    // a hard disc: the Save As dialog it always had (its own kind only)
    {
        const QString p = work + "/doc/hd.dat";
        MainWindow w; w.show();
        CHECK(w.openFile(p), "the hard disc opens");
        saveAs(w, QString(), QString());
        CHECK(g_offered == QStringList({"Disc image (*.dat)", "All files (*)"}), "a hard disc: the old dialog: " + g_offered.join(" | "));
        CHECK(QFileInfo(g_suggested).fileName() == "hd-copy.dat", "and its suggested name: " + g_suggested);
    }

    // another kind over the image's own file: refused, nothing changes
    {
        const QString p = work + "/doc/own.adl";
        QString err;
        NewDisc::create(p, NewDisc::AdfsL, "OWN", false, &err);
        const QByteArray before = slurp(p);
        MainWindow w; w.show();
        CHECK(w.openFile(p), "opens");
        QString box;
        whenModal<QMessageBox>([&box](QMessageBox *m) {          // the file dialog's own "already exists": replace it
            if (QAbstractButton *yes = m->button(QMessageBox::Yes)) yes->click();
            whenModal<QMessageBox>([&box](QMessageBox *r) { box = r->windowTitle() + ": " + r->text(); r->accept(); }, "no refusal");
        }, "no question from the file dialog");
        saveAs(w, p, "ImageDisk");
        pump();
        CHECK(box == "Save as: That is the image's own file. Choose another name for the ImageDisk (.IMD).", "refused: " + box);
        CHECK(slurp(p) == before && w.document()->image().path() == p && w.document()->image().containerKind() == Kind::Raw, "and nothing changed");
    }

    // TeleDisk: read-only; Save As offers only the writable kinds, starting on ImageDisk, and the copy can be changed
    int td0s = 0;
    if (!td0dir.isEmpty())
        for (const QFileInfo &fi : QDir(td0dir).entryInfoList({"*.td0"}, QDir::Files, QDir::Name)) {
            const QString dir = work + "/td0";
            QDir().mkpath(dir);
            const QString gui = dir + "/" + fi.completeBaseName() + "-gui.imd", cl = dir + "/" + fi.completeBaseName() + "-cli.imd";
            QFile::remove(gui); QFile::remove(cl);
            CHECK(cli({"convert", fi.absoluteFilePath(), cl, "--container=imd"}), "acorndw convert " + fi.fileName());
            MainWindow w; w.show();
            QString box;
            whenModal<QMessageBox>([&box](QMessageBox *m) { box = m->windowTitle(); m->accept(); }, "no read-only notice");
            CHECK(w.openFile(fi.absoluteFilePath()), "opens " + fi.fileName());
            pump();
            CHECK(box == "Read-only" && !w.writable(0), fi.fileName() + ": read-only, and says so: " + box);
            saveAs(w, gui, "ImageDisk");
            CHECK(g_offered.size() == 4 && !g_offered.join(' ').contains("td0", Qt::CaseInsensitive), fi.fileName() + ": only writable kinds: " + g_offered.join(" | "));
            CHECK(g_startFilter.startsWith("ImageDisk") && QFileInfo(g_suggested).fileName() == fi.completeBaseName() + ".imd",
                  fi.fileName() + ": starts on ImageDisk, " + g_suggested);
            CHECK(slurp(gui) == slurp(cl) && !slurp(gui).isEmpty(), fi.fileName() + " -> imd: the bytes acorndw convert gives");
            CHECK(w.document()->image().containerKind() == Kind::Imd && w.document()->image().editable(), fi.fileName() + ": the window shows the ImageDisk copy, writable");
            ++td0s;
        }
    printf("%d copies saved from %d presets, %d TeleDisk files; %d checks\n", saved, int(presets.size()), td0s, checks);
    printf(fails ? "FAILED (%d failures)\n" : "ALL OK (%d failures)\n", fails);
    return fails ? 1 : 0;
}
