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
// Boot option (*OPT 4,n) tests.   boottest <outdir> [<copy of a hard disc image, e.g. scsi0.dat>]
// ADFS S/M/L (new blank images) and the hard disc: each option 0-3 is set, saved and read back; on disc only byte
// &FD of map sector 1 (and the two map checksums) may change. DFS: one side of a .dsd at a time, the other side
// untouched; catalogue sector 1 byte 6 bits 4-5. Refusals: option 4, the locked ADFS side of a 640K DOS Plus disc.
// MainWindow (offscreen): the option follows the current pane; DOS has none. Images are left in <outdir> for an
// independent check (see AGENTS.md).
#include "mainwindow.h"
#include "adfs.h"
#include "dfs.h"
#include "diskformat.h"
#include "newdisc.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <cstdio>
using namespace Adfs;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)
static QByteArray head(const QString &p, int n) { QFile f(p); f.open(QIODevice::ReadOnly); return f.read(n); }
static quint64 fsize(const QString &p) { return quint64(QFileInfo(p).size()); }

static bool openAdfs(Image &img, const QString &path) {
    DiskInfo d = detectDisk(path); QString e;
    bool ok = img.open(path, &e, d.kind == DiskInfo::AdfsDosHybrid ? d.fullSize : 0, d.sides);
    if (ok && d.kind == DiskInfo::AdfsDosHybrid) img.setAdfsLocked(true);
    if (!ok) printf("  open: %s\n", qPrintable(e));
    return ok;
}

static void adfsCase(const QString &path, const char *what) {
    printf("== ADFS %s\n", what);
    quint64 size = fsize(path);
    for (int opt : {3, 1, 2, 0}) {
        QByteArray before = head(path, 512);
        Image img; CHECK(openAdfs(img, path)); CHECK(img.openReport().ok());
        QString e;
        CHECK(img.setBootOption(opt, &e)); CHECK(img.bootOption() == opt);
        CHECK(img.dirty() == (quint8(before[256 + 0xFD]) != opt));
        CHECK(img.commit(&e, false, nullptr)); img.close();
        QByteArray after = head(path, 512);
        CHECK(quint8(after[256 + 0xFD]) == opt);
        // nothing else in the map changes but the checksums - apart from unused free-list slots past the end-of-list
        // pointer, which every save clears (a hard disc can have stale entries there)
        int used = quint8(before[256 + 0xFE]);
        for (int i = 0; i < 512; ++i) {
            int k = i % 256;
            bool unusedSlot = k >= used && k < 0xF6;
            if (i != 256 + 0xFD && k != 0xFF && !unusedSlot && before[i] != after[i]) { CHECK(!"map byte changed"); printf("  byte %d\n", i); break; }
        }
        CHECK(quint8(after[0xFF]) == mapChecksum(after.constData()));
        CHECK(quint8(after[256 + 0xFF]) == mapChecksum(after.constData() + 256));
        CHECK(fsize(path) == size);
        Image again; CHECK(openAdfs(again, path)); CHECK(again.bootOption() == opt); CHECK(again.openReport().ok());
    }
    Image img; CHECK(openAdfs(img, path)); QString e;
    CHECK(!img.setBootOption(4, &e)); CHECK(!img.dirty()); printf("  option 4 refused: %s\n", qPrintable(e));
    CHECK(img.setBootOption(3, &e)); img.revert(); CHECK(img.bootOption() == 0); CHECK(!img.dirty());
    printf("  %s: boot option set/saved/read back for 0-3\n", what);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("acorndiskwrangler-boottest"); QApplication::setApplicationName("t");
    if (argc < 2) { printf("usage: boottest <outdir> [<copy of a hard disc image>]\n"); return 2; }
    QString out = argv[1]; QDir().mkpath(out); QString e;

    for (auto t : {NewDisc::AdfsS, NewDisc::AdfsM, NewDisc::AdfsL}) {
        QString p = out + QString("/boot_adfs%1.%2").arg(int(t)).arg(NewDisc::info(t).suffix);
        CHECK(NewDisc::create(p, t, "BOOTTEST", false, &e));
        adfsCase(p, NewDisc::info(t).name);
    }
    { QString p = out + "/boot_adfsL_seq.adl"; CHECK(NewDisc::create(p, NewDisc::AdfsL, "SEQ", true, &e)); adfsCase(p, "ADFS L, sequential file"); }
    if (argc > 2) adfsCase(argv[2], "hard disc");

    // ---- DFS: each side of a .dsd separately, and an .ssd
    {
        printf("== DFS\n");
        QString p = out + "/boot_dfs.dsd"; CHECK(NewDisc::create(p, NewDisc::DfsDs80, "BOOT", false, &e));
        auto openDfs = [&](Image &img, Dfs::Disc *d) {
            DiskInfo i = detectDisk(p); CHECK(img.openRaw(p, i.fullSize, &e, i.sides));
            for (int s = 0; s < 2; ++s) CHECK(d[s].attach(&img, s, i.dfs, &e));
        };
        QByteArray f0 = head(p, 12 * 256);
        { Image img; Dfs::Disc d[2]; openDfs(img, d); CHECK(d[0].bootOption() == 0 && d[1].bootOption() == 0);
          CHECK(d[0].setBootOption(3, &e)); CHECK(!d[0].setBootOption(7, &e)); CHECK(d[0].bootOption() == 3);
          CHECK(d[0].check().ok()); CHECK(img.commit(&e, false, nullptr)); }
        QByteArray f1 = head(p, 12 * 256);
        CHECK(((quint8(f1[256 + 6]) >> 4) & 3) == 3);                               // side 0: catalogue at file sectors 0-1
        CHECK((quint8(f1[256 + 6]) & 3) == (quint8(f0[256 + 6]) & 3));              // sector count bits kept
        CHECK(f1[256 + 4] != f0[256 + 4]);                                          // cycle number bumped
        CHECK(f1.mid(10 * 256, 512) == f0.mid(10 * 256, 512));                      // side 1 catalogue (file sectors 10-11) untouched
        { Image img; Dfs::Disc d[2]; openDfs(img, d); CHECK(d[0].bootOption() == 3); CHECK(d[1].bootOption() == 0);
          CHECK(d[1].setBootOption(2, &e)); CHECK(img.commit(&e, false, nullptr)); }
        QByteArray f2 = head(p, 12 * 256);
        CHECK(((quint8(f2[10 * 256 + 256 + 6]) >> 4) & 3) == 2); CHECK(f2.left(512) == f1.left(512));
        QString q = out + "/boot_dfs.ssd"; CHECK(NewDisc::create(q, NewDisc::DfsSs40, "", false, &e));
        { DiskInfo i = detectDisk(q); Image img; CHECK(img.openRaw(q, i.fullSize, &e, i.sides)); Dfs::Disc d; CHECK(d.attach(&img, 0, i.dfs, &e));
          CHECK(d.setBootOption(1, &e)); CHECK(img.commit(&e, false, nullptr)); }
        CHECK(((quint8(head(q, 512)[256 + 6]) >> 4) & 3) == 1);
        printf("  DFS sides set independently; .ssd too\n");
    }

    // ---- refused: the ADFS side of a DOS Plus 640K disc is locked
    {
        QString boot = out + "/dosboot"; { QFile f(boot); f.open(QIODevice::WriteOnly); f.write(QByteArray(2303, char(0x90))); }
        QString p = out + "/boot_hyb.adl"; CHECK(NewDisc::create(p, NewDisc::Acorn640, "", false, &e, boot));
        Image img; CHECK(openAdfs(img, p)); CHECK(!img.setBootOption(2, &e)); CHECK(!img.dirty());
        printf("  640K DOS Plus disc refused: %s\n", qPrintable(e));
        // Acorn's ROM gives an all-zero map sector 1 the check byte &FF; earlier versions wrote 00 there
        CHECK(quint8(head(p, 512)[511]) == 0xFF);
        QString old = out + "/boot_hyb_old.adl"; QFile::remove(old); CHECK(QFile::copy(p, old));
        { QFile f(old); CHECK(f.open(QIODevice::ReadWrite)); f.seek(511); f.putChar(0); }
        CHECK(detectDisk(old).kind == DiskInfo::AdfsDosHybrid);
        { Image o; CHECK(openAdfs(o, old)); CHECK(o.openReport().ok()); CHECK(o.editable());
          CHECK(o.openReport().warnings.join(" ").contains("earlier version")); CHECK(o.dirty());
          CHECK(o.commit(&e, false, nullptr)); }
        CHECK(head(old, 4096) == head(p, 4096));                                    // byte for byte as a new disc
        { Image o; CHECK(openAdfs(o, old)); CHECK(!o.dirty()); CHECK(!o.openReport().warnings.join(" ").contains("earlier version")); }
        // any other wrong check byte is still an error
        { QFile f(old); CHECK(f.open(QIODevice::ReadWrite)); f.seek(511); f.putChar(0x12); }
        { Image o; CHECK(openAdfs(o, old)); CHECK(!o.openReport().ok()); CHECK(!o.dirty()); }
        printf("  map check byte of an earlier version's 640K disc corrected\n");
    }

    // ---- MainWindow: the option follows the current pane
    {
        printf("== MainWindow\n");
        QString p = out + "/boot_gui.dsd"; CHECK(NewDisc::create(p, NewDisc::DfsDs40, "", false, &e));
        { MainWindow w; CHECK(w.openImage(p)); w.tabs()->setCurrentWidget(w.dfsTree(1)); QApplication::processEvents();
          CHECK(w.bootOption() == 0); CHECK(w.setBootOption(3)); CHECK(w.bootOption() == 3); CHECK(w.image().dirty());
          w.tabs()->setCurrentWidget(w.dfsTree(0)); QApplication::processEvents(); CHECK(w.bootOption() == 0);
          CHECK(w.save()); }
        { MainWindow w; CHECK(w.openImage(p)); w.tabs()->setCurrentWidget(w.dfsTree(1)); QApplication::processEvents(); CHECK(w.bootOption() == 3); }
        QString l = out + "/boot_adfs2.adl";
        { MainWindow w; CHECK(w.openImage(l)); CHECK(w.bootOption() == 0); CHECK(w.setBootOption(2)); CHECK(w.save()); }
        { MainWindow w; CHECK(w.openImage(l)); CHECK(w.bootOption() == 2); }
        QString pc = out + "/boot_pc.img"; CHECK(NewDisc::create(pc, NewDisc::Pc720, "", false, &e));
        { MainWindow w; CHECK(w.openImage(pc)); CHECK(w.bootOption() == -1); }
    }
    QSettings().clear();
    printf(fails ? "FAILED (%d failures)\n" : "ALL OK (%d failures)\n", fails);
    return fails ? 1 : 0;
}
