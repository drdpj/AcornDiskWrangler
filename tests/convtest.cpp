/*
 * convtest - part of Acorn Disk Wrangler
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
// convtest <work dir> [<real 640K hybrid .adl> <real 800K DOS Plus .img>]
// acorndw's conversion between containers (QtCore only):
//  * rawLayoutFor agrees with the geometry table of containertest.cpp (kept as a copy here) for every floppy kind, and
//    refuses a hard disc;
//  * every floppy preset (acorndw's `new` list, and both side orders for the double-sided ones) goes
//    raw -> ImageDisk -> raw, raw -> extended DSK -> raw and raw -> DSK -> raw and comes back byte for byte;
//  * the same for the two real discs when they are given;
//  * a TeleDisk file converts to raw and reads the same as the raw image it was made from;
//  * a converted copy reads the same files as the original (the listing of every mount).
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>

#include "../src/adfsvolume.h"
#include "../src/cli/backend.h"
#include "../src/diskformat.h"
#include "../src/newdisc.h"

using namespace dw::cli;

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", qPrintable(QString(m))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(m))); } while (0)

static QByteArray slurp(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
static void spit(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

// containertest.cpp's table
struct Geometry { int seclen, sectrk, cyls, heads, firstId; };
static Geometry geometryOf(const QString &kind, qint64 size) {
    if (kind == "adfs" || kind == "hybrid") {
        if (size == 655360) return {256, 16, 80, 2, 0};
        return {256, 16, int(size / 4096), 1, 0};
    }
    if (kind == "ssd") return {256, 10, int(size / 2560), 1, 0};
    if (kind == "dsd") return {256, 10, int(size / 5120), 2, 0};
    if (kind == "dos800") return {1024, 5, 80, 2, 0};
    int spt = size == 368640 ? 9 : size == 737280 ? 9 : size == 1228800 ? 15 : 18;
    return {512, spt, int(size / (512 * spt * 2)), 2, 1};
}

static QByteArray teleDisk(const QByteArray &raw, const Geometry &g) {
    QByteArray o("TD\0\0\x15\x02\x03\x00\x00", 9);
    o += char(g.heads); o += '\0'; o += '\0';
    int n = 0;
    while ((128 << n) < g.seclen) ++n;
    qint64 off = 0;
    for (int c = 0; c < g.cyls; ++c)
        for (int h = 0; h < g.heads; ++h) {
            o += char(g.sectrk); o += char(c); o += char(h); o += '\0';
            for (int s = 0; s < g.sectrk; ++s) {
                o += char(c); o += char(h); o += char(g.firstId + s); o += char(n); o += '\0'; o += '\0';
                const int len = g.seclen + 1;
                o += char(len & 0xFF); o += char(len >> 8); o += '\0';
                o += raw.mid(int(off), g.seclen);
                off += g.seclen;
            }
        }
    o += char(0xFF);
    return o;
}

static AcornCli::Backend backend;

static OpenOutcome openImage(const QString &path, const QString &sides = QString()) {
    OpenOptions o;
    o.sides = sides;
    return backend.open(path, o);
}

// every file of every mount, with sizes, as lines
static QStringList listing(OpenedImage *img) {
    QStringList out;
    for (const auto &m : img->disc().mounts()) {
        std::function<void(const dw::fs::Path &)> rec = [&](const dw::fs::Path &dir) {
            QVector<dw::fs::Entry> l;
            QString e;
            if (!m.vol->list(dir, &l, &e)) return;
            for (const dw::fs::Entry &x : l) {
                const dw::fs::Path p = dir.child(x.name);
                QByteArray data;
                if (x.isDir()) { out << m.label + ":" + m.vol->displayPath(p) + "/"; rec(p); }
                else { m.vol->read(p, &data, &e); out << QString("%1:%2 %3 %4").arg(m.label, m.vol->displayPath(p)).arg(x.size).arg(qHash(data)); }
            }
        };
        rec(dw::fs::Path());
    }
    return out;
}

static bool convert(const QString &in, const QString &out, const QString &container, const QString &toSides, const QString &openSides, QString *why) {
    OpenOutcome o = openImage(in, openSides);
    if (o.status != OpenOutcome::Ok) { *why = "open: " + o.message; return false; }
    ConvertOptions co;
    co.container = container;
    co.toSides = toSides;
    QJsonObject r;
    int exit = 0;
    if (!o.image->convertTo(out, co, &r, why, &exit)) return false;
    return true;
}

// raw -> imd/edsk/dsk -> raw must give the original bytes
static void roundTrips(const QString &work, const QString &raw, const QString &label, bool sequential) {
    const QByteArray want = slurp(raw);
    const bool hasOrder = detectDisk(raw, sequential ? 1 : -1).sides.doubleSided();     // PC floppies and single-sided discs have none
    OpenOutcome src = openImage(raw, sequential ? "sequential" : QString());
    CHECK(src.status == OpenOutcome::Ok, label + ": opens");
    if (src.status != OpenOutcome::Ok) return;
    const QStringList files = listing(src.image.get());
    src.image.reset();
    for (const QString &c : {"imd", "edsk", "dsk"}) {
        const QString mid = QString("%1/%2.%3").arg(work, QFileInfo(raw).fileName(), c);
        const QString back = mid + ".raw";
        QFile::remove(mid); QFile::remove(back);
        QString why;
        CHECK(convert(raw, mid, c, QString(), sequential ? "sequential" : QString(), &why), QString("%1 -> %2: %3").arg(label, c, why));
        OpenOutcome m = openImage(mid);
        CHECK(m.status == OpenOutcome::Ok && listing(m.image.get()) == files, label + " as " + c + " lists the same files");
        m.image.reset();
        CHECK(convert(mid, back, "raw", !hasOrder ? QString() : sequential ? "sequential" : "interleaved", QString(), &why), QString("%1 -> %2 -> raw: %3").arg(label, c, why));
        const QByteArray got = slurp(back);
        CHECK(got.size() >= want.size() && got.left(want.size()) == want && (got.size() == want.size() || got.mid(want.size()).count(char(0)) == got.size() - want.size()),
              QString("%1 -> %2 -> raw is byte-identical (%3 bytes)").arg(label, c).arg(got.size()));
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { printf("usage: convtest <work dir> [<real 640K hybrid .adl> <real 800K DOS Plus .img>]\n"); return 2; }
    const QString work = QDir(argv[1]).absolutePath();
    QDir().mkpath(work);
    QString err;

    printf("rawLayoutFor against containertest's table; every preset round-trips\n");
    spit(work + "/dosboot.bin", QByteArray(2048, 'B'));
    struct P { const char *id; NewDisc::Type t; const char *kind; };
    const P presets[] = {{"adfs-s", NewDisc::AdfsS, "adfs"}, {"adfs-m", NewDisc::AdfsM, "adfs"}, {"adfs-l", NewDisc::AdfsL, "adfs"},
                         {"dfs-ss40", NewDisc::DfsSs40, "ssd"}, {"dfs-ss80", NewDisc::DfsSs80, "ssd"}, {"dfs-ds40", NewDisc::DfsDs40, "dsd"},
                         {"dfs-ds80", NewDisc::DfsDs80, "dsd"}, {"wat-ss40", NewDisc::WatSs40, "ssd"}, {"wat-ss80", NewDisc::WatSs80, "ssd"},
                         {"wat-ds40", NewDisc::WatDs40, "dsd"}, {"wat-ds80", NewDisc::WatDs80, "dsd"}, {"dos-800", NewDisc::Acorn800, "dos800"},
                         {"dos-640", NewDisc::Acorn640, "hybrid"}, {"pc-360", NewDisc::Pc360, "pc"}, {"pc-720", NewDisc::Pc720, "pc"},
                         {"pc-1200", NewDisc::Pc1200, "pc"}, {"pc-1440", NewDisc::Pc1440, "pc"}};
    for (const P &p : presets) {
        const NewDisc::TypeInfo &ti = NewDisc::info(p.t);
        for (int seq = 0; seq <= (ti.doubleSided ? 1 : 0); ++seq) {
            const QString f = QString("%1/%2%3.img").arg(work, p.id, seq ? "-seq" : "");
            CHECK(NewDisc::create(f, p.t, "CONV", seq, &err, p.t == NewDisc::Acorn640 ? work + "/dosboot.bin" : QString()), QString("new %1: %2").arg(f, err));
            const DiskInfo info = detectDisk(f, seq ? 1 : 0);
            dw::device::RawLayout l;
            QString why;
            CHECK(rawLayoutFor(info, &l, &why), QString("%1: rawLayoutFor: %2").arg(p.id, why));
            const Geometry g = geometryOf(p.kind, qint64(info.fullSize));
            CHECK(l.seclen == g.seclen && l.sectrk == g.sectrk && l.cyls == g.cyls && l.heads == g.heads && l.firstId == g.firstId,
                  QString("%1: the layout is containertest's: %2 x %3 x %4 x %5 from %6").arg(p.id).arg(l.seclen).arg(l.sectrk).arg(l.cyls).arg(l.heads).arg(l.firstId));
            CHECK(quint64(l.bytes()) == info.fullSize, QString("%1: the layout is the disc's whole size").arg(p.id));
            CHECK(l.mode == (QByteArray(p.kind).endsWith("sd") && QByteArray(p.kind) != "dsd" ? 2 : QByteArray(p.kind) == "ssd" || QByteArray(p.kind) == "dsd" ? 2 : 5), QString("%1: IMD mode").arg(p.id));
            CHECK(l.sideBySide == (seq && ti.doubleSided && QByteArray(p.kind) != "pc"), QString("%1: sideBySide follows the file's side order").arg(p.id));
            roundTrips(work, f, p.id, seq);
        }
    }
    {
        DiskInfo hd;
        hd.kind = DiskInfo::Adfs;
        hd.fullSize = 100u * 1024 * 1024;
        dw::device::RawLayout l;
        QString why;
        CHECK(!rawLayoutFor(hd, &l, &why) && !why.isEmpty(), "a hard disc has no layout: " + why);
        DiskInfo odd;
        odd.kind = DiskInfo::DosPc;
        odd.fullSize = 500000;
        CHECK(!rawLayoutFor(odd, &l, &why), "an odd PC size has no layout: " + why);
        DiskInfo cpm;
        cpm.kind = DiskInfo::AcornCpm;
        CHECK(!rawLayoutFor(cpm, &l, &why), "a CP/M disc has none: " + why);
    }

    if (argc >= 4) {
        printf("the real discs\n");
        QFile::remove(work + "/real640.adl"); QFile::remove(work + "/real800.img");
        QFile::copy(argv[2], work + "/real640.adl");
        QFile::copy(argv[3], work + "/real800.img");
        roundTrips(work, work + "/real640.adl", "real 640K hybrid", false);
        roundTrips(work, work + "/real800.img", "real 800K DOS Plus", false);
        // TeleDisk: read-only to us, convertible to raw
        const QByteArray raw = slurp(work + "/real800.img");
        spit(work + "/real800.td0", teleDisk(raw, geometryOf("dos800", raw.size())));
        QString why;
        CHECK(convert(work + "/real800.td0", work + "/td0.raw", "raw", QString(), QString(), &why), "TeleDisk -> raw: " + why);
        CHECK(slurp(work + "/td0.raw").left(raw.size()) == raw, "TeleDisk -> raw is the original bytes");
        CHECK(!convert(work + "/real800.img", work + "/never.td0", "td0", QString(), QString(), &why), "TeleDisk can't be written: " + why);
        // a side order change on a double-sided raw image, both ways, and not on a container
        const QString seq = work + "/dsd-to-seq.img";
        QFile::remove(seq);
        CHECK(convert(work + "/dfs-ds80.img", seq, "raw", "sequential", QString(), &why), "dfs-ds80 -> sequential: " + why);
        CHECK(slurp(seq) != slurp(work + "/dfs-ds80.img") && slurp(seq).size() == slurp(work + "/dfs-ds80.img").size(), "the bytes are reordered");
        QString why2;
        CHECK(convert(seq, work + "/dsd-back.img", "raw", "interleaved", "sequential", &why2) && slurp(work + "/dsd-back.img") == slurp(work + "/dfs-ds80.img"), "and back: " + why2);
        CHECK(!convert(work + "/dfs-ds80.img.imd", work + "/x.imd", "imd", "sequential", QString(), &why), "a side order for a container is refused: " + why);
    }

    printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
