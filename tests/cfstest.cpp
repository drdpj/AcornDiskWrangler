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
// cfstest: the cassette filing system on tapes (src/cfs.*, src/cfsvolume.*, the tape side of diskformat and
// Adfs::OpenedDisc).
//
//   cfstest [UEF_DIR]                 hand-made tapes, every rule of docs/disc-formats.md "Tapes"; then every real tape in
//                                     UEF_DIR opened, listed and read
//   cfstest --dump FILE               the tape as tests/cfs_check.py prints it (the independent reader compares)
//   cfstest --write-samples DIR       the hand-made tapes and the tapes this program wrote, for cfs_check.py
//   cfstest --moved FILE              a tape whose files were moved (files D B C A; A opened the tape with no carrier
//                                     before it), for design/test-scripts/jsbeeb/cfs-jsbeeb.mjs
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

#include <diskwrangler/tape/records.h>
#include <diskwrangler/tape/tapefile.h>
#include <diskwrangler/xfer/transferitem.h>

#include "adfsvolume.h"
#include "cfs.h"
#include "cfsvolume.h"
#include "diskformat.h"
#include "newdisc.h"

using namespace Acorn;
using dw::tape::Chunk;
using dw::tape::TapeFile;
namespace uef = dw::tape::uef;

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, qPrintable(QString(what))); ++fails; } \
                            else printf("  ok: %s\n", qPrintable(QString(what))); } while (0)

// ----- making tapes: blocks as MOS 1.20 writes them, the CRC computed here (not with Cfs::crc)

static quint16 crc16(const QByteArray &b) {
    quint16 c = 0;
    for (char ch : b) {
        c ^= quint16(uchar(ch)) << 8;
        for (int i = 0; i < 8; ++i) c = (c & 0x8000) ? quint16((c << 1) ^ 0x1021) : quint16(c << 1);
    }
    return c;
}

static QByteArray le(quint32 v, int n) {
    QByteArray b;
    for (int i = 0; i < n; ++i) b.append(char((v >> (8 * i)) & 0xFF));
    return b;
}

struct BlockSpec {
    QByteArray name;
    quint32 load = 0x1900, exec = 0x8023;
    int number = 0;
    quint8 flag = 0;
    QByteArray data;
    bool badHeader = false, badData = false;
    bool noTerminator = false;           // 11 name bytes, no &00 (the MOS reads the 11th in its place)
};

static QByteArray block(const BlockSpec &s) {
    QByteArray h = s.name;
    if (!s.noTerminator) h.append('\0');
    h += le(s.load, 4) + le(s.exec, 4) + le(quint32(s.number), 2) + le(quint32(s.data.size()), 2);
    h.append(char(s.flag));
    h += QByteArray(4, '\0');
    quint16 c = crc16(h);
    if (s.badHeader) c ^= 1;
    QByteArray b = QByteArray(1, '\x2A') + h + char(c >> 8) + char(c & 0xFF);
    if (!s.data.isEmpty()) {
        quint16 d = crc16(s.data);
        if (s.badData) d ^= 0x100;
        b += s.data + char(d >> 8) + char(d & 0xFF);
    }
    return b;
}

static QByteArray bytes(int n, int seed) {
    QByteArray b;
    quint32 x = quint32(seed) * 2654435761u + 1;
    for (int i = 0; i < n; ++i) { x = x * 1103515245u + 12345u; b.append(char(x >> 16)); }
    return b;
}

// A file as the MOS saves it: 256-byte blocks, the last one flagged, each after its carrier.
static void saveFile(QVector<Chunk> *t, const QByteArray &name, const QByteArray &data, quint32 load, quint32 exec, quint8 flags = 0) {
    t->append(uef::carrier(5 * 1200));                  // the leader before a file's first block
    t->append(uef::data(QByteArray(1, '\xAA')));        // the dummy byte
    const int n = data.isEmpty() ? 1 : (data.size() + 255) / 256;
    for (int k = 0; k < n; ++k) {
        BlockSpec s;
        s.name = name;
        s.load = load;
        s.exec = exec;
        s.number = k;
        s.data = data.mid(k * 256, 256);
        s.flag = quint8(flags | (k == n - 1 ? 0x80 : 0));
        t->append(uef::carrier(k ? 600 : 1200));
        t->append(uef::data(block(s)));
    }
    t->append(uef::carrier(5 * 1200));
    t->append(uef::gap(1.0));
}

static QByteArray uefFile(const QVector<Chunk> &chunks, bool gz = true) {
    TapeFile t;
    t.createBlank(dw::tape::Kind::Uef, gz);
    QString e;
    t.setChunks(chunks, &e);
    return t.serialize();
}

static Chunk raw(int id, const QByteArray &body) { return Chunk(id, body); }

// The hand-made tapes, by name: each one rule or a few.
static QMap<QString, QByteArray> samples() {
    QMap<QString, QByteArray> m;
    {   // files as the MOS writes them: several blocks, one block, an empty file, the same name twice, protected
        QVector<Chunk> t;
        t.append(uef::text(0x0000, "cfstest"));
        saveFile(&t, "GAME", bytes(600, 1), 0x1900, 0x8023);
        saveFile(&t, "LOADER", bytes(200, 2), 0xFFFF0E00, 0xFFFF801F, 0x01);
        saveFile(&t, "EMPTY", QByteArray(), 0, 0);
        saveFile(&t, "GAME", bytes(256, 3), 0x3000, 0x3000);
        saveFile(&t, "Game", bytes(10, 4), 0x3000, 0x3000);
        saveFile(&t, "GAME (2)", bytes(20, 5), 0x3000, 0x3000);
        saveFile(&t, "TEN CHARS!", bytes(30, 6), 0x0E00, 0x0E00);
        m["standard"] = uefFile(t);
        m["standard-plain"] = uefFile(t, false);
    }
    {   // damage: CRCs, missing and repeated blocks, a file that never ends, a block cut short by the end of the tape
        QVector<Chunk> t;
        auto put = [&](const BlockSpec &s) { t.append(uef::carrier(600)); t.append(uef::data(block(s))); };
        BlockSpec s;
        s.name = "HDR"; s.data = bytes(256, 7); put(s);
        s.number = 1; s.flag = 0x80; s.data = bytes(5, 8); s.badHeader = true; put(s);
        s = BlockSpec(); s.name = "DATA"; s.data = bytes(256, 9); s.badData = true; put(s);
        s.number = 1; s.flag = 0x80; s.data = bytes(9, 10); s.badData = false; put(s);
        s = BlockSpec(); s.name = "GAP"; s.data = bytes(256, 11); put(s);          // block 1 missing
        s.number = 2; s.flag = 0x80; s.data = bytes(3, 12); put(s);
        s = BlockSpec(); s.name = "LATE"; s.number = 1; s.flag = 0x80; s.data = bytes(40, 13); put(s);   // starts at 1
        s = BlockSpec(); s.name = "SHORT"; s.data = bytes(100, 14); put(s);       // not last, not 256 bytes
        s.number = 1; s.flag = 0x80; s.data = bytes(1, 15); put(s);
        s = BlockSpec(); s.name = "NOEND"; s.data = bytes(256, 16); put(s);       // its last block never comes
        s = BlockSpec(); s.name = "CUT"; s.flag = 0x80; s.data = bytes(80, 17);
        t.append(uef::carrier(600));
        t.append(uef::data(block(s).left(60)));                                  // the tape ends inside the data
        m["damaged"] = uefFile(t);
    }
    {   // framing: 300 baud, &0104 8N1 and 8E1, &0102, a block split across chunks with carrier between, a gap
        // inside a block, the Atom's &0105
        QVector<Chunk> t;
        BlockSpec s;
        s.name = "SLOW"; s.flag = 0x80; s.data = bytes(50, 20);
        t.append(raw(0x0117, le(300, 2)));
        t.append(uef::carrier(1200));
        t.append(uef::data(block(s)));
        t.append(raw(0x0117, le(1200, 2)));
        s.name = "FMT8N1"; s.data = bytes(60, 21);
        t.append(uef::carrier(600));
        t.append(uef::data(block(s), 8, 'N', 1));
        s.name = "FMT8E1"; s.data = bytes(70, 22);
        t.append(uef::carrier(600));
        t.append(uef::data(block(s), 8, 'E', 1));                               // not what the MOS reads
        s.name = "WAVE"; s.data = bytes(30, 27);
        t.append(uef::carrier(600));
        t.append(raw(0x0104, QByteArray("\x08" "N" "\xFF", 3) + block(s)));     // 8N1 with the extra short wave
        {
            s.name = "BITS"; s.data = bytes(20, 23);
            const QByteArray b = block(s);
            QByteArray bits(1 + (b.size() * 10 + 4 + 7) / 8, '\0');
            int at = 0;
            auto bit = [&](int v) { if (v) bits[1 + at / 8] = char(uchar(bits[1 + at / 8]) | (1 << (at % 8))); ++at; };
            for (int i = 0; i < 4; ++i) bit(1);
            for (char c : b) { bit(0); for (int k = 0; k < 8; ++k) bit((uchar(c) >> k) & 1); bit(1); }
            bits[0] = char((bits.size() - 1) * 8 - at);
            t.append(uef::carrier(600));
            t.append(raw(0x0102, bits));
        }
        {
            s.name = "SPLIT"; s.data = bytes(90, 24);
            const QByteArray b = block(s);
            t.append(uef::carrier(600));
            t.append(uef::data(b.left(30)));
            t.append(uef::carrier(30));                                          // the line idling inside the block
            t.append(raw(0x0113, uef::putFloat(1210)));
            t.append(uef::data(b.mid(30)));
            t.append(raw(0x0113, uef::putFloat(1200)));
        }
        {
            s.name = "HOLE"; s.data = bytes(90, 25);
            const QByteArray b = block(s);
            t.append(uef::carrier(600));
            t.append(uef::data(b.left(40)));
            t.append(uef::gap(0.5));                                             // silence inside it: cut short
            t.append(uef::data(b.mid(40)));
        }
        t.append(uef::carrier(600));
        t.append(raw(0x0105, "ATOMDATA"));
        s.name = "AFTER"; s.data = bytes(10, 26);
        t.append(uef::carrier(600));
        t.append(uef::data(QByteArray("\x01\x02", 2)));                         // not &2A after carrier: ignored ...
        t.append(uef::data(block(s)));                                           // ... and so is this, until carrier
        s.name = "LAST";
        t.append(uef::carrier(600));
        t.append(uef::data(block(s)));
        m["framing"] = uefFile(t);
    }
    {   // names: no &00 after 10 characters (the MOS reads an 11th byte in its place), control characters, Latin-1
        QVector<Chunk> t;
        BlockSpec s;
        s.flag = 0x80;
        s.name = "ELEVENCHARX"; s.noTerminator = true; s.data = bytes(10, 30);
        t.append(uef::carrier(600)); t.append(uef::data(block(s)));
        s.noTerminator = false;
        s.name = QByteArray("A\x01" "B", 3); s.data = bytes(10, 31);
        t.append(uef::carrier(600)); t.append(uef::data(block(s)));
        s.name = QByteArray("\xA3" "5 back", 7); s.data = bytes(10, 32);
        t.append(uef::carrier(600)); t.append(uef::data(block(s)));
        s.name = "ZERO"; s.data = QByteArray(); s.flag = 0x80;
        t.append(uef::carrier(600)); t.append(uef::data(block(s)));
        m["names"] = uefFile(t);
    }
    {   // nothing the cassette filing system can read
        QVector<Chunk> t;
        t.append(uef::text(0x0000, "cfstest"));
        t.append(uef::carrier(1200));
        t.append(uef::data(bytes(300, 40), 8, 'E', 1));
        t.append(uef::gap(1));
        t.append(uef::carrier(1200));
        t.append(uef::data(bytes(50, 41)));
        m["nothing"] = uefFile(t);
    }
    return m;
}

// ----- the dump tests/cfs_check.py compares

static QString nameText(const QByteArray &n) {
    QString s;
    for (char ch : n) {
        const uchar c = uchar(ch);
        s += c >= 0x21 && c < 0x7F && c != 0x5C ? QString(QChar(c)) : QString("\\x%1").arg(c, 2, 16, QChar('0'));
    }
    return s;
}

static QString h16(const QByteArray &b) { return QString(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16)); }

static QStringList dump(const TapeFile &t) {
    QStringList out;
    const Cfs::Tape r = Cfs::read(dw::tape::pieces(t));
    int n = 0;
    for (const Cfs::Item &it : r.items) {
        if (it.type == Cfs::Item::FileItem) {
            const Cfs::File &f = it.file;
            QString line = QString::asprintf("file %d %s load %08X exec %08X length %d blocks %d flag %02X chunks %d-%d data %s",
                                             ++n, qPrintable(nameText(f.name)), f.load, f.exec, int(f.data.size()), f.blocks,
                                             unsigned(f.flag), f.first, f.last, qPrintable(h16(f.data)));
            for (const QString &p : f.problems) line += " problem " + p;
            out << line;
        } else {
            out << QString("unrecognised chunks %1-%2 bytes %3").arg(it.first).arg(it.last).arg(it.bytes);
        }
    }
    return out;
}

static bool openTape(const QByteArray &file, TapeFile *t, const QString &name = "t.uef") {
    QString e;
    return t->openBytes(file, name, "uef", &e);
}

static void handMade(const QString &dir) {
    const QMap<QString, QByteArray> s = samples();
    QString err;

    printf("the CRC (MOS 1.20 &F7B0)\n");
    CHECK(Cfs::crc("123456789") == 0x31C3, "CRC-16/XMODEM check value &31C3");
    {
        const QByteArray b = block(BlockSpec{"X", 0, 0, 0, 0x80, "abc"});
        CHECK(Cfs::crc(b.mid(1, 2 + 17 + 2)) == 0, "a header read with its CRC leaves 0");
    }

    printf("files as the MOS writes them\n");
    {
        TapeFile t;
        CHECK(openTape(s["standard"], &t) && t.gzip(), "the tape opens");
        const Cfs::Tape r = Cfs::read(dw::tape::pieces(t));
        CHECK(r.files() == 7 && r.items.size() == 7, QString("seven files, no unrecognised data (%1 items)").arg(r.items.size()));
        const Cfs::File &g = r.items[0].file;
        CHECK(g.name == "GAME" && g.blocks == 3 && g.data == bytes(600, 1) && g.load == 0x1900 && g.exec == 0x8023 &&
                  g.problems.isEmpty(), "GAME: three blocks, 600 bytes, its addresses");
        CHECK(r.items[1].file.isProtected() && r.items[1].file.load == 0xFFFF0E00, "LOADER: protected, an I/O processor address");
        CHECK(r.items[2].file.data.isEmpty() && r.items[2].file.blocks == 1 && r.items[2].file.problems.isEmpty(),
              "EMPTY: one block with no data");
        CHECK(r.items[3].file.data == bytes(256, 3) && r.items[3].file.blocks == 1, "the second GAME: one full block");

        CfsVolume v(&t);
        QVector<dw::fs::Entry> es;
        CHECK(v.list(dw::fs::Path(), &es, &err) && es.size() == 7, "the volume lists them");
        QStringList names, shown;
        for (const dw::fs::Entry &e : es) { names << e.name; shown << e.shown(); }
        CHECK(names == QStringList({"GAME", "LOADER", "EMPTY", "GAME (2)", "Game (3)", "GAME (2) (2)", "TEN CHARS!"}),
              "unique names, in tape order: " + names.join(", "));
        CHECK(shown == QStringList({"GAME", "LOADER", "EMPTY", "GAME", "Game", "GAME (2)", "TEN CHARS!"}),
              "shown by their own names: " + shown.join(", "));
        CHECK(v.caps().ordered && !v.caps().dirs, "ordered, no directories");
        dw::fs::Path p;
        CHECK(v.parsePath("#4", &p, &err) && p.parts == QStringList({"GAME (2)"}), "#4 is the fourth file");
        CHECK(!v.parsePath("#9", &p, &err) && err == "the tape has 7 files", "#9 doesn't exist: " + err);
        CHECK(v.parsePath("M/C", &p, &err) && p.parts == QStringList({"M/C"}), "'/' is a name character");
        QByteArray d;
        CHECK(v.read(dw::fs::Path({"GAME"}), &d, &err) && d == bytes(600, 1), "GAME reads the first");
        CHECK(v.read(dw::fs::Path({"GAME (2)"}), &d, &err) && d == bytes(256, 3), "GAME (2) reads the second");
        CHECK(v.read(dw::fs::Path({"game"}), &d, &err) && d == bytes(600, 1), "a name in another case: the first match");
        dw::fs::Entry e;
        CHECK(v.stat(dw::fs::Path({"LOADER"}), &e, &err) && e.attrs.value("protected").toBool() &&
                  e.attrs.value("position").toInt() == 2 && e.attrs.value("blocks").toInt() == 1, "LOADER's attributes");
        const QMap<QString, QJsonObject> rf = v.toRetro(e);
        CHECK(rf["acorn"]["load"] == "FFFF0E00" && rf["acorn"]["exec"] == "FFFF801F" && rf["acorn"]["access"] == "E",
              "retro-files: load, exec, and E (execute only) for a protected file");
        CHECK(v.toRetro(es[0])["acorn"]["access"] == "WR", "  and WR for any other");
        CHECK(v.check().ok() && v.check().warnings.isEmpty(), "a clean check");

        TapeFile plain;
        CHECK(openTape(s["standard-plain"], &plain) && !plain.gzip() && dump(plain) == dump(t), "the same tape uncompressed reads the same");
    }

    printf("damage\n");
    {
        TapeFile t;
        openTape(s["damaged"], &t);
        const QStringList d = dump(t);
        for (const QString &l : d) printf("    %s\n", qPrintable(l));
        const Cfs::Tape r = Cfs::read(dw::tape::pieces(t));
        auto problems = [&](int i) { return r.items.value(i).file.problems.join("; "); };
        CHECK(problems(0) == "block 1 header CRC", "a bad header CRC: " + problems(0));
        CHECK(problems(1) == "block 0 data CRC", "a bad data CRC: " + problems(1));
        CHECK(r.items[2].file.name == "GAP" && problems(2) == "no last block" && r.items[3].file.name == "GAP" &&
                  problems(3) == "starts at block 2", "a missing block splits the file: " + problems(2) + " / " + problems(3));
        CHECK(problems(4) == "starts at block 1", "a file that starts at block 1: " + problems(4));
        CHECK(problems(5) == "block 0 is 100 bytes", "a block before the last that isn't 256 bytes: " + problems(5));
        CHECK(problems(6) == "no last block", "a file whose last block never comes: " + problems(6));
        CHECK(problems(7) == "block 0 cut short; no last block" || problems(7) == "block 0 cut short",
              "a block cut short by the end of the tape: " + problems(7));
        CfsVolume v(&t);
        CHECK(v.check().ok() && v.check().warnings.size() == 8, QString("problems are warnings, not errors (%1)").arg(v.check().warnings.size()));
    }

    printf("framing\n");
    {
        TapeFile t;
        openTape(s["framing"], &t);
        const QStringList d = dump(t);
        for (const QString &l : d) printf("    %s\n", qPrintable(l));
        const Cfs::Tape r = Cfs::read(dw::tape::pieces(t));
        QStringList names;
        for (const Cfs::Item &i : r.items) names << (i.type == Cfs::Item::FileItem ? QString(i.file.name) + (i.file.problems.isEmpty() ? "" : "!") : QString("?"));
        CHECK(names == QStringList({"SLOW", "FMT8N1", "?", "BITS", "SPLIT", "HOLE!", "?", "LAST"}),
              "300 baud, 8N1 by &0104, &0102 and a split block read; 8E1, the extra wave, a gap inside a block, the "
              "Atom's data and a block not straight after carrier don't: " + names.join(" "));
        CHECK(r.items[4].file.data == bytes(90, 24) && r.items[4].first < r.items[4].last, "the split block, from two chunks");
    }

    printf("names\n");
    {
        TapeFile t;
        openTape(s["names"], &t);
        const Cfs::Tape r = Cfs::read(dw::tape::pieces(t));
        CHECK(r.files() == 4, "four files");
        CHECK(r.items[0].file.name == "ELEVENCHAR" && r.items[0].file.problems.isEmpty() && r.items[0].file.data == bytes(10, 30),
              "11 name bytes: the first 10 are the name, the 11th stands for the &00: " + QString(r.items[0].file.name));
        CHECK(r.items[1].file.name == QByteArray("A\x01" "B", 3), "a control character is kept");
        CHECK(Cfs::nameText(r.items[2].file.name) == QString::fromLatin1("\xA3" "5 back"), "Latin-1 names: one character per byte");
        CHECK(r.items[3].file.data.isEmpty() && r.items[3].file.blocks == 1, "a block of length 0 has no data or data CRC");
        CfsNameRules n;
        QString why;
        CHECK(n.valid("TEN CHARS!", dw::fs::NodeKind::File, &why) && !n.valid("ELEVENCHARS", dw::fs::NodeKind::File, &why) &&
                  !n.valid("", dw::fs::NodeKind::File, &why), "names: 1-10 characters");
    }

    printf("unrecognised data\n");
    {
        TapeFile t;
        openTape(s["nothing"], &t);
        const Cfs::Tape r = Cfs::read(dw::tape::pieces(t));
        CHECK(r.files() == 0 && r.items.size() == 1 && r.items[0].type == Cfs::Item::Unrecognised && r.items[0].bytes == 350,
              QString("one row of unrecognised data, 350 bytes (%1)").arg(r.items.value(0).bytes));
        CfsVolume v(&t);
        QVector<dw::fs::Entry> es;
        v.list(dw::fs::Path(), &es, &err);
        CHECK(es.size() == 1 && es[0].name == "unrecognised data (chunks 2-5)" && !es[0].problem.isEmpty(),
              "listed as " + es.value(0).name + ", with a problem (read-only)");
        QByteArray d;
        CHECK(v.read(dw::fs::Path({es[0].name}), &d, &err) && d == bytes(300, 40) + bytes(50, 41), "its bytes, as they are");
        const QMap<QString, QJsonObject> rf = v.toRetro(es[0]);
        CHECK(rf.keys() == QStringList({"uef"}) && !CfsVolume::rawChunks(rf).isEmpty(),
              "no load or exec to carry, only its chunks (namespace uef)");
    }

    printf("detection and the document\n");
    {
        QDir().mkpath(dir);
        for (auto it = s.begin(); it != s.end(); ++it) {
            QFile f(dir + "/" + it.key() + ".uef");
            f.open(QIODevice::WriteOnly);
            f.write(it.value());
        }
        dw::fs::Verdict v = detectCandidates(dir + "/standard.uef");
        CHECK(v.action == dw::fs::Verdict::Open && diskInfo(*v.bestCandidate()).kind == DiskInfo::Tape &&
                  v.bestCandidate()->tier == dw::fs::Tier::Certain, "a UEF of files: Tape, certain: " + v.bestCandidate()->description);
        v = detectCandidates(dir + "/nothing.uef");
        CHECK(v.action == dw::fs::Verdict::Open && v.bestCandidate()->tier == dw::fs::Tier::Likely, "a UEF of nothing readable: likely");
        // a TZX: named and refused
        {
            TapeFile z;
            z.createBlank(dw::tape::Kind::Tzx, false);
            z.setChunks({dw::tape::tzx::standard(QByteArray("\x00hello", 6))}, &err);
            QFile f(dir + "/spec.tzx");
            f.open(QIODevice::WriteOnly);
            f.write(z.serialize());
        }
        v = detectCandidates(dir + "/spec.tzx");
        CHECK(v.bestCandidate() && diskInfo(*v.bestCandidate()).kind == DiskInfo::ForeignTape && !v.bestCandidate()->supported,
              "a TZX: " + (v.bestCandidate() ? v.bestCandidate()->description : QString("nothing")));
        Adfs::OpenedDisc od;
        CHECK(od.open(dir + "/standard.uef", diskInfo(*detectCandidates(dir + "/standard.uef").bestCandidate()), &err) && od.isTape() &&
                  od.isOpen() && od.tapeMount() == 0 && od.disc().mounts().size() == 1 && !od.disc().mounts()[0].readOnly,
              "the document: one tape mount, writable " + err);
        CHECK(od.disc().container() == nullptr && od.disc().staged() != nullptr, "its Disc stages on the tape, not a container");
        CHECK(od.readOnlyWhy(0).isEmpty() && !od.dirty() && od.save(&err), "nothing to save");
        CHECK(od.saveAs(dir + "/copy.uef", dw::device::Kind::None, &err) && QFileInfo(od.path()).fileName() == "copy.uef",
              "Save As: a copy, which the document goes on with " + err);
        TapeFile a, b;
        a.open(dir + "/standard.uef", &err);
        b.open(dir + "/copy.uef", &err);
        CHECK(a.chunks() == b.chunks() && b.gzip(), "the copy holds the same tape, compressed as it was");
        od.close();
        CHECK(!od.isOpen() && od.disc().staged() == nullptr, "closed");
        CHECK(od.open(dir + "/standard.uef", diskInfo(*detectCandidates(dir + "/standard.uef").bestCandidate()), &err), "open again");
        CHECK(!od.writeCopy(dir + "/x.ssd", dw::device::Kind::Raw, QString(), false, &err), "never converted into a disc: " + err);
        CHECK(!od.open(dir + "/spec.tzx", diskInfo(*detectCandidates(dir + "/spec.tzx").bestCandidate()), &err) && !od.isOpen(),
              "a TZX isn't opened: " + err);
    }
}

// ----- writing (series cfs-2)

static QString ids(const QVector<Chunk> &c) {
    QStringList l;
    for (const Chunk &k : c) l << QString::number(k.id, 16);
    return l.join(" ");
}

static quint32 le16(const QByteArray &b, int at) { return uchar(b[at]) | (uchar(b[at + 1]) << 8); }

static QStringList names(const CfsVolume &v) {
    QVector<dw::fs::Entry> es;
    QString e;
    v.list(dw::fs::Path(), &es, &e);
    QStringList n;
    for (const dw::fs::Entry &x : es) n << x.shown();
    return n;
}

// Chunks with no carrier before the first block: a tape that starts straight with a file (A), then B, C and D.
static QVector<Chunk> unled() {
    QVector<Chunk> t;
    BlockSpec s;
    s.name = "A"; s.flag = 0x80; s.data = bytes(100, 60); s.load = 0x3000; s.exec = 0x3000;
    t.append(uef::data(block(s)));
    s.name = "B"; s.data = bytes(256, 61);
    t.append(uef::carrier(1200)); t.append(uef::data(block(s)));
    s.name = "C"; s.flag = 0; s.data = bytes(256, 62);
    t.append(uef::carrier(1200)); t.append(uef::data(block(s)));
    s.number = 1; s.flag = 0x80; s.data = bytes(44, 63);
    t.append(uef::carrier(600)); t.append(uef::data(block(s)));
    s = BlockSpec(); s.name = "D"; s.flag = 0x80; s.data = QByteArray(); s.load = 0x3000; s.exec = 0x3000;
    t.append(uef::carrier(1200)); t.append(uef::data(block(s)));
    t.append(uef::carrier(1200));
    return t;
}

// The unled tape with D moved to the front and A to the end: D B C A.
static QByteArray movedTape(QString *why) {
    TapeFile t;
    t.createBlank(dw::tape::Kind::Uef, false);
    QString e;
    t.setChunks(unled(), &e);
    CfsVolume v(&t);
    if (!v.move({dw::fs::Path({"D"})}, dw::fs::Path({"A"}), &e) || !v.move({dw::fs::Path({"A"})}, dw::fs::Path(), &e)) {
        *why = e;
        return QByteArray();
    }
    return t.serialize();
}

static void writing(const QString &dir, QMap<QString, QByteArray> *written) {
    QString err;
    QDir().mkpath(dir);

    printf("writing: blocks and files as *SAVE makes them\n");
    {
        BlockSpec s;
        s.name = "GAME"; s.load = 0x1900; s.exec = 0x8023; s.flag = 0x80; s.data = bytes(100, 50);
        CHECK(Cfs::encodeBlock("GAME", 0x1900, 0x8023, 0, bytes(100, 50), 0x80) == block(s),
              "a block: &2A, name, &00, addresses, number, length, flag, 4 zero bytes, CRCs (this test's own CRC)");
        s.data = QByteArray();
        CHECK(Cfs::encodeBlock("GAME", 0x1900, 0x8023, 0, QByteArray(), 0x80) == block(s), "a block with no data has no data CRC");
        auto shape = [](const QVector<QByteArray> &b) {
            QStringList l;
            for (const QByteArray &x : b) {
                const int n = x.indexOf('\0', 1);
                l << QString("%1:%2:%3").arg(le16(x, n + 9)).arg(le16(x, n + 11)).arg(uchar(x[n + 13]), 2, 16, QChar('0'));
            }
            return l.join(" ");
        };
        CHECK(shape(Cfs::encodeFile("E", 0, 0, QByteArray(), false)) == "0:0:80", "an empty file: one block, no data, last");
        CHECK(shape(Cfs::encodeFile("F", 0, 0, bytes(256, 1), false)) == "0:256:80", "256 bytes: one full block, last");
        CHECK(shape(Cfs::encodeFile("F", 0, 0, bytes(513, 1), false)) == "0:256:00 1:256:00 2:1:80", "513 bytes: 256, 256, 1");
        CHECK(shape(Cfs::encodeFile("F", 0, 0, bytes(300, 1), true)) == "0:256:01 1:44:81", "protected: bit 0 in every block");
        const QVector<Chunk> c = Cfs::saveChunks(Cfs::encodeFile("F", 0, 0, bytes(300, 1), false), false);
        CHECK(ids(c) == "111 100 110 100 110" && le16(c[0].body, 0) == 2400 && le16(c[0].body, 2) == 12240 &&
                  le16(c[2].body, 0) == 2160 && le16(c[4].body, 0) == 12720,
              "a save: 1 s, the dummy byte, 5.1 s; block; 0.9 s; block; 5.3 s: " + ids(c));
        const QVector<Chunk> d = Cfs::saveChunks(Cfs::encodeFile("F", 0, 0, bytes(3, 1), false), true);
        CHECK(ids(d) == "116 111 100 110" && uef::getFloat(d[0].body, 0) == 1.0, "after other files: a second of silence first");
    }

    printf("writing: a new tape and the volume's changes\n");
    const QString path = dir + "/w.uef";
    CHECK(NewDisc::create(path, NewDisc::Uef, QString(), false, &err), "File > New image: a tape " + err);
    {
        TapeFile t;
        CHECK(t.open(path, &err) && t.gzip() && t.chunks().size() == 1 && t.chunks()[0].id == 0, "gzip, its origin chunk only");
        const dw::fs::Verdict v = detectCandidates(path);
        CHECK(v.bestCandidate() && v.bestCandidate()->tier == dw::fs::Tier::Certain && diskInfo(*v.bestCandidate()).kind == DiskInfo::Tape,
              "an empty tape is a tape, certain (not unrecognised data)");
    }
    Adfs::OpenedDisc od;
    CHECK(od.open(path, diskInfo(*detectCandidates(path).bestCandidate()), &err) && od.readOnlyWhy(od.tapeMount()).isEmpty(),
          "it opens writable " + err);
    CfsVolume *v = od.cfs();
    auto run = [&](const std::function<bool(QString *)> &f) { return od.disc().transact(f, &err); };
    auto put = [&](const QString &n, const QByteArray &d, quint32 load, bool prot = false) {
        return run([&](QString *e) { return v->write(dw::fs::Path(), n, d, {{"load", load}, {"exec", load}, {"protected", prot}}, QDateTime(), false, e); });
    };
    CHECK(put("ONE", bytes(600, 70), 0x1900) && put("TWO", bytes(10, 71), 0x2000, true) && put("THREE", QByteArray(), 0) &&
              put("ONE", bytes(5, 72), 0x5000),
          "four files added, a second ONE among them " + err);
    CHECK(names(*v) == QStringList({"ONE", "TWO", "THREE", "ONE"}), "in the order added: " + names(*v).join(" "));
    QByteArray d;
    CHECK(v->read(dw::fs::Path({"ONE"}), &d, &err) && d == bytes(600, 70) && v->read(dw::fs::Path({"ONE (2)"}), &d, &err) &&
              d == bytes(5, 72), "each reads back");
    dw::fs::Entry e;
    CHECK(v->stat(dw::fs::Path({"TWO"}), &e, &err) && e.attrs.value("protected").toBool() && e.attrs.value("load").toUInt() == 0x2000,
          "TWO protected, load &2000");
    CHECK(od.dirty() && v->check().warnings.isEmpty(), "staged, and a clean check");
    const QVector<Chunk> before = od.tape().chunks();
    {   // the first file's chunks are untouched by what came after it
        const QVector<dw::tape::Piece> ps = dw::tape::pieces(od.tape());
        CHECK(ids(before.mid(1, 7)) == "111 100 110 100 110 100 110" && ids(before.mid(8, 4)) == "116 111 100 110",
              "laid out as the MOS saves: " + ids(before));
    }
    // a failed change leaves nothing
    CHECK(!run([&](QString *e) { return v->write(dw::fs::Path(), "ELEVENCHARS", "x", {}, QDateTime(), false, e); }) &&
              od.tape().chunks() == before, "a name that is too long: refused, nothing changed: " + err);
    // rename: only that file's block chunks change
    CHECK(run([&](QString *e) { return v->rename(dw::fs::Path({"TWO"}), dw::fs::Path({"V1.0"}), e); }), "TWO renamed V1.0 " + err);
    {
        const QVector<Chunk> &now = od.tape().chunks();
        int diff = 0;
        for (int i = 0; i < now.size() && i < before.size(); ++i) diff += now[i] != before[i];
        CHECK(now.size() == before.size() && diff == 1 && names(*v).value(1) == "V1.0", QString("one chunk changed (%1)").arg(diff));
        CHECK(v->stat(dw::fs::Path({"V1.0"}), &e, &err) && e.attrs.value("protected").toBool(), "  and it is still protected");
    }
    // attributes, keeping a block's other flag bits and spare bytes
    CHECK(run([&](QString *e) { return v->setAttrs(dw::fs::Path({"ONE"}), {{"load", 0xFFFF0E00u}, {"protected", true}}, e); }) &&
              v->stat(dw::fs::Path({"ONE"}), &e, &err) && e.attrs.value("load").toUInt() == 0xFFFF0E00 &&
              e.attrs.value("exec").toUInt() == 0x1900 && e.attrs.value("protected").toBool() && e.attrs.value("blocks").toInt() == 3,
          "ONE: load &FFFF0E00, protected, exec kept, three blocks " + err);
    // a replacement with another number of blocks: the file's chunks from its first block to its last
    CHECK(run([&](QString *e) { return v->write(dw::fs::Path(), "ONE", bytes(1000, 73), {}, QDateTime(), true, e); }) &&
              v->read(dw::fs::Path({"ONE"}), &d, &err) && d == bytes(1000, 73) && v->stat(dw::fs::Path({"ONE"}), &e, &err) &&
              e.attrs.value("blocks").toInt() == 4 && e.attrs.value("load").toUInt() == 0xFFFF0E00 && e.attrs.value("protected").toBool(),
          "ONE replaced by 1000 bytes: four blocks, its attributes kept " + err);
    CHECK(names(*v) == QStringList({"ONE", "V1.0", "THREE", "ONE"}), "  in its place: " + names(*v).join(" "));
    // remove, move
    CHECK(run([&](QString *e) { return v->remove(dw::fs::Path({"THREE"}), e); }) && names(*v) == QStringList({"ONE", "V1.0", "ONE"}),
          "THREE removed: " + names(*v).join(" "));
    CHECK(run([&](QString *e) { return v->move({dw::fs::Path({"ONE (2)"})}, dw::fs::Path({"ONE"}), e); }) &&
              v->read(dw::fs::Path({"ONE"}), &d, &err) && d == bytes(5, 72), "the second ONE moved to the front: it is ONE now");
    CHECK(run([&](QString *e) { return v->move({dw::fs::Path({"ONE"}), dw::fs::Path({"V1.0"})}, dw::fs::Path(), e); }) &&
              names(*v) == QStringList({"ONE", "ONE", "V1.0"}) && v->read(dw::fs::Path({"ONE"}), &d, &err) && d == bytes(1000, 73) &&
              v->read(dw::fs::Path({"V1.0"}), &d, &err) && d == bytes(10, 71),
          "two moved to the end, in their order: " + names(*v).join(" "));
    // save: read back, and opened again it reads the same
    const QStringList dumped = dump(od.tape());
    CHECK(od.save(&err, true) && !od.dirty() && QFileInfo::exists(path + ".bak"), "saved, with a .bak " + err);
    {
        TapeFile t;
        CHECK(t.open(path, &err) && t.gzip() && dump(t) == dumped, "opened again: the same files, still gzip");
        (*written)["written"] = t.serialize();
    }
    CHECK(od.saveTapeAs(dir + "/plain.uef", false, &err) && od.path() == dir + "/plain.uef" && !od.tape().gzip(), "Save As, uncompressed " + err);
    {
        QFile f(dir + "/plain.uef");
        f.open(QIODevice::ReadOnly);
        CHECK(f.read(10) == QByteArray("UEF File!\0", 10), "  a plain UEF file");
    }
    od.close();

    printf("writing: what is kept as it is\n");
    {
        TapeFile t;
        openTape(samples()["damaged"], &t);
        CfsVolume w(&t);
        CHECK(!w.rename(dw::fs::Path({"HDR"}), dw::fs::Path({"X"}), &err) && !w.remove(dw::fs::Path({"DATA"}), &err),
              "a damaged file can't be renamed or removed: " + err);
        TapeFile n;
        openTape(samples()["nothing"], &n);
        CfsVolume u(&n);
        QVector<dw::fs::Entry> es;
        u.list(dw::fs::Path(), &es, &err);
        CHECK(!u.remove(dw::fs::Path({es[0].name}), &err), "unrecognised data can't be removed: " + err);
        QFile f(dir + "/nothing.uef");
        f.open(QIODevice::WriteOnly);
        f.write(samples()["nothing"]);
        f.close();
        Adfs::OpenedDisc o;
        CHECK(o.open(dir + "/nothing.uef", diskInfo(*detectCandidates(dir + "/nothing.uef").bestCandidate()), &err) &&
                  o.disc().mounts()[0].readOnly && !o.readOnlyWhy(0).isEmpty(), "a tape of no BBC files is read-only: " + o.readOnlyWhy(0));
    }

    printf("writing: base frequency and baud rate kept for every chunk\n");
    {
        // SLOW at 300 baud, the rest at 1200; appended and removed files mustn't change what anything plays at
        TapeFile t;
        openTape(samples()["framing"], &t);
        const QStringList was = dump(t);
        CfsVolume w(&t);
        CHECK(w.remove(dw::fs::Path({"FMT8N1"}), &err), "FMT8N1 removed " + err);
        QStringList now = dump(t), want = was;
        want.removeAt(1);
        for (QString &l : want) l = l.section(" chunks ", 0, 0) + " " + l.section(" data ", 1);
        for (QString &l : now) l = l.section(" chunks ", 0, 0) + " " + l.section(" data ", 1);
        for (int i = 0; i < want.size(); ++i) want[i] = want[i].section(' ', 2);
        for (int i = 0; i < now.size(); ++i) now[i] = now[i].section(' ', 2);
        CHECK(now == want, "the others read as before");
        CHECK(w.move({dw::fs::Path({"SLOW"})}, dw::fs::Path(), &err), "SLOW (300 baud) moved to the end " + err);
        CHECK(w.write(dw::fs::Path(), "NEW", bytes(20, 80), {}, QDateTime(), false, &err), "a file added after it");
        bool slow300 = false, new1200 = false;
        const Cfs::Tape r = Cfs::read(dw::tape::pieces(t));
        for (const dw::tape::Piece &p : dw::tape::pieces(t)) {
            if (p.type != dw::tape::Piece::Data) continue;
            for (const Cfs::Item &i : r.items) {
                if (i.type != Cfs::Item::FileItem || p.first > i.file.last || p.last < i.file.first) continue;
                if (i.file.name == "SLOW") slow300 = p.coding.baud == 300;
                if (i.file.name == "NEW") new1200 = p.coding.baud == 1200 && p.coding.baseHz == 1200;
            }
        }
        CHECK(slow300 && new1200 && names(w).last() == "NEW", "SLOW still at 300 baud, NEW at 1200: " + names(w).join(" "));
    }

    printf("writing: unrecognised data copied to another tape stays data\n");
    {
        // unrecognised data copied as a drop copies it (toRetro on the source; placeIncoming, fromRetro and write on
        // the target): the framing sample's two runs, with a file between them (two runs side by side are one), and
        // data played at 300 baud and 1210 Hz, which must play at those again
        TapeFile src, slow;
        openTape(samples()["framing"], &src);
        {
            QVector<Chunk> t;
            t.append(uef::text(0x0000, "cfstest"));
            t.append(raw(0x0113, uef::putFloat(1210)));
            t.append(raw(0x0117, le(300, 2)));
            t.append(uef::carrier(1200));
            t.append(uef::data(bytes(200, 90), 8, 'E', 1));
            t.append(uef::carrier(600));
            openTape(uefFile(t), &slow);
        }
        CfsVolume from(&src), from300(&slow);
        TapeFile dst;
        dst.createBlank(dw::tape::Kind::Uef, true);
        CfsVolume to(&dst);
        auto codings = [](const TapeFile &t, const Cfs::Item &i) {
            const int first = i.type == Cfs::Item::FileItem ? i.file.first : i.first;
            const int last = i.type == Cfs::Item::FileItem ? i.file.last : i.last;
            QStringList l;
            for (const dw::tape::Piece &p : dw::tape::pieces(t))
                if (p.type == dw::tape::Piece::Data && p.first <= last && p.last >= first)
                    l << QString("%1/%2").arg(p.coding.baud).arg(p.coding.baseHz);
            l.removeDuplicates();
            return l.join(",");
        };
        QStringList want, got;
        QVector<QByteArray> data;
        auto copy = [&](const CfsVolume &v, const TapeFile &t, int k) {
            QVector<dw::fs::Entry> es;
            v.list(dw::fs::Path(), &es, &err);
            const Cfs::Item &it = v.tapeRead().items.value(k);
            CHECK(k < es.size() && it.type == Cfs::Item::Unrecognised, "unrecognised data to copy");
            if (k >= es.size()) return;
            dw::TransferItem x;
            x.name = es[k].name;
            x.attrs = v.toRetro(es[k]);
            x.sourceFs = v.fsId();
            CHECK(v.read(dw::fs::Path({es[k].name}), &x.data, &err), "read " + err);
            const dw::fs::Placement pl = to.placeIncoming(x, dw::fs::Path());
            dw::fs::Entry ex;
            CHECK(pl.warning.isEmpty() && !to.stat(dw::fs::Path({pl.name}), &ex, &err), "placed under a name nothing has: " + pl.name);
            CHECK(to.write(dw::fs::Path(), pl.name, x.data, to.fromRetro(x.attrs, dw::fs::NodeKind::File), QDateTime(), false, &err),
                  "added " + err);
            CHECK(!to.write(dw::fs::Path(), pl.name, x.data, to.fromRetro(x.attrs, dw::fs::NodeKind::File), QDateTime(), true, &err),
                  "never in place of something: " + err);
            want << QString("%1 bytes %2").arg(it.bytes).arg(codings(t, it));
            data << x.data;
        };
        auto add = [&](const QString &name) {
            CHECK(to.write(dw::fs::Path(), name, bytes(20, 80), {}, QDateTime(), false, &err), name + " added " + err);
            want << name + " 1200/1200";
        };
        copy(from, src, 2);
        add("MID");
        copy(from, src, 6);
        add("MID2");
        copy(from300, slow, 0);
        add("NEW");
        QVector<dw::fs::Entry> es;
        to.list(dw::fs::Path(), &es, &err);
        QVector<QByteArray> now;
        for (int k = 0; k < to.tapeRead().items.size(); ++k) {
            const Cfs::Item &it = to.tapeRead().items[k];
            if (it.type == Cfs::Item::Unrecognised) {
                got << QString("%1 bytes %2").arg(it.bytes).arg(codings(dst, it));
                QByteArray d;
                to.read(dw::fs::Path({es[k].name}), &d, &err);
                now << d;
            } else {
                got << QString("%1 %2").arg(Cfs::nameText(it.file.name)).arg(codings(dst, it));
            }
        }
        CHECK(got == want && want.size() == 6 && want[4].endsWith("300/1210"),
              QString("each run as data at its own rates, the files at 1200: %1 | %2").arg(got.join("; "), want.join("; ")));
        CHECK(now == data, "the same bytes");
        CHECK(to.check().ok(), "a clean check");
        // anywhere else (a disc) it's a file of its bytes: the namespace is one only a tape takes
        dw::TransferItem plain;
        plain.name = "unrecognised data (chunks 7-9)";
        plain.attrs["uef"] = QJsonObject{{"chunks", "not chunks"}};
        CHECK(to.placeIncoming(plain, dw::fs::Path()).name != "unrecognised data" && to.fromRetro(plain.attrs, dw::fs::NodeKind::File).isEmpty(),
              "chunks that don't unpack: an ordinary file");
    }

    printf("writing: a name already on the tape\n");
    {
        // a tape holds any number of files of one name: another file is added under the unique name it will have
        // and keeps its real name; only an identical file (name, addresses given, bytes) is a clash, and with that file
        TapeFile dst;
        dst.createBlank(dw::tape::Kind::Uef, true);
        CfsVolume to(&dst);
        CHECK(to.write(dw::fs::Path(), "GAME", bytes(300, 1), {{"load", 0x1900u}, {"exec", 0x8023u}}, QDateTime(), false, &err), "GAME " + err);
        auto item = [](const QString &name, const QByteArray &data, const QString &load) {
            dw::TransferItem x;
            x.name = name;
            x.data = data;
            if (!load.isEmpty()) x.attrs["acorn"] = QJsonObject{{"load", load}, {"exec", "00008023"}, {"access", "WR"}};
            return x;
        };
        auto add = [&](const dw::TransferItem &x, const QString &as) {
            const dw::fs::Placement pl = to.placeIncoming(x, dw::fs::Path());
            dw::fs::Entry ex;
            CHECK(pl.name == as && !to.stat(dw::fs::Path({pl.name}), &ex, &err), QString("%1 placed as %2: %3").arg(x.name, as, pl.name));
            CHECK(to.write(dw::fs::Path(), pl.name, x.data, to.fromRetro(x.attrs, dw::fs::NodeKind::File), QDateTime(), false, &err),
                  "added " + err);
        };
        auto clash = [&](const dw::TransferItem &x, const QString &with) {
            const dw::fs::Placement pl = to.placeIncoming(x, dw::fs::Path());
            dw::fs::Entry ex;
            CHECK(pl.name == with && to.stat(dw::fs::Path({pl.name}), &ex, &err), QString("%1 a clash with %2: %3").arg(x.name, with, pl.name));
        };
        add(item("GAME", bytes(300, 2), "00001900"), "GAME (2)");          // other bytes
        add(item("GAME", bytes(300, 1), "00003000"), "GAME (3)");          // another load address
        add(item("game", bytes(10, 3), ""), "game (4)");                   // another case: the MOS takes it for GAME
        clash(item("GAME", bytes(300, 1), "00001900"), "GAME");            // identical to the first
        clash(item("GAME", bytes(300, 2), ""), "GAME (2)");                // no addresses given: the bytes decide
        dw::TransferItem dfs = item("$.GAME", bytes(5, 4), "");
        dfs.sourceFs = "dfs";
        add(dfs, "GAME (5)");                                               // a DFS file's "$." off first
        add(item("ABCDEFGHIJ", bytes(5, 5), ""), "ABCDEFGHIJ");
        add(item("ABCDEFGHIJ", bytes(5, 6), ""), "ABCDEFGHIJ (2)");        // longer than a name can be: only the entry's
        to.setSameName(CfsVolume::SameName::Add);                          // the window: identical ones too (it asked)
        add(item("GAME", bytes(300, 1), "00001900"), "GAME (6)");
        to.setSameName(CfsVolume::SameName::Replace);                      // acorndw --replace: the first of the name
        clash(item("GAME", bytes(9, 9), ""), "GAME");
        QStringList real;
        for (const Cfs::Item &it : to.tapeRead().items) real << Cfs::nameText(it.file.name);
        CHECK(real == QStringList({"GAME", "GAME", "GAME", "game", "GAME", "ABCDEFGHIJ", "ABCDEFGHIJ", "GAME"}),
              "each under its own name: " + real.join(","));
        CHECK(to.tapeRead().items.value(2).file.load == 0x3000 && to.tapeRead().items.value(3).file.data == bytes(10, 3),
              "the files added are the items");
        // a write of a name never placed is that name, as before
        to.setSameName(CfsVolume::SameName::AddUnlessIdentical);
        CHECK(to.write(dw::fs::Path(), "X (2)", bytes(3, 7), {}, QDateTime(), false, &err) &&
              Cfs::nameText(to.tapeRead().items.last().file.name) == "X (2)", "X (2) " + err);
        CHECK(to.check().ok(), "a clean check");
    }

    printf("writing: damaged files copied to another tape stay as they were\n");
    {
        // every file of the damaged sample copied, in order, as a drop copies them: the new tape reads the same,
        // problem for problem
        TapeFile src;
        openTape(samples()["damaged"], &src);
        CfsVolume from(&src);
        TapeFile dst;
        dst.createBlank(dw::tape::Kind::Uef, true);
        CfsVolume to(&dst);
        QVector<dw::fs::Entry> es;
        from.list(dw::fs::Path(), &es, &err);
        QStringList names;
        for (const dw::fs::Entry &e : es) {
            dw::TransferItem x;
            x.name = e.shown();
            x.attrs = from.toRetro(e);
            x.sourceFs = from.fsId();
            from.read(dw::fs::Path({e.name}), &x.data, &err);
            CHECK(x.attrs.contains("uef") && x.attrs.contains("acorn"), e.name + ": its chunks, and its load and exec for a disc");
            const dw::fs::Placement pl = to.placeIncoming(x, dw::fs::Path());
            dw::fs::Entry ex;
            names << pl.name;
            CHECK(!to.stat(dw::fs::Path({pl.name}), &ex, &err), "placed under a name nothing has: " + pl.name);
            CHECK(to.write(dw::fs::Path(), pl.name, x.data, to.fromRetro(x.attrs, dw::fs::NodeKind::File), QDateTime(), false, &err),
                  e.name + " added " + err);
        }
        CHECK(names.value(0) == "HDR (damaged)", "named for the report: " + names.join(", "));
        auto strip = [](QStringList l) {
            for (QString &x : l) x = x.section(" chunks ", 0, 0) + " " + x.section(" data ", 1);
            return l;
        };
        CHECK(es.size() == 8 && strip(dump(dst)) == strip(dump(src)),
              "the same files with the same problems:\n" + dump(dst).join("\n") + "\nnot\n" + dump(src).join("\n"));
    }

    printf("writing: a file that opened the tape with no carrier before it\n");
    {
        QString why;
        const QByteArray m = movedTape(&why);
        TapeFile t;
        CHECK(!m.isEmpty() && openTape(m, &t), "moved: " + why);
        CfsVolume w(&t);
        CHECK(names(w) == QStringList({"D", "B", "C", "A"}), "D B C A: " + names(w).join(" "));
        const Cfs::Tape r = w.tapeRead();
        CHECK(t.chunks()[r.items[3].file.first - 1].id == 0x0110 && le16(t.chunks()[r.items[3].file.first - 1].body, 0) == Cfs::Mos::LeaderCycles,
              "A got the MOS's leader in front of it");
        (*written)["moved"] = m;
    }
}

static void realTapes(const QString &dir) {
    const QStringList names = QDir(dir).entryList(QStringList() << "*.uef" << "*.UEF", QDir::Files, QDir::Name);
    printf("real tapes in %s: %d\n", qPrintable(dir), int(names.size()));
    QString err;
    for (const QString &n : names) {
        const QString path = dir + "/" + n;
        const dw::fs::Verdict v = detectCandidates(path);
        CHECK(v.action == dw::fs::Verdict::Open && diskInfo(*v.bestCandidate()).kind == DiskInfo::Tape &&
                  v.bestCandidate()->tier == dw::fs::Tier::Certain, n + ": " + v.bestCandidate()->description);
        Adfs::OpenedDisc od;
        if (!od.open(path, diskInfo(*v.bestCandidate()), &err)) { CHECK(false, n + ": " + err); continue; }
        CfsVolume *vol = od.cfs();
        QVector<dw::fs::Entry> es;
        vol->list(dw::fs::Path(), &es, &err);
        int bad = 0, bytes = 0;
        for (const dw::fs::Entry &e : es) {
            QByteArray d;
            if (!vol->read(dw::fs::Path({e.name}), &d, &err) || d.size() != e.size) ++bad;
            if (!e.problem.isEmpty() && e.attrs.contains("load")) ++bad;
            bytes += d.size();
        }
        CHECK(bad == 0 && !es.isEmpty(), QString("%1: %2 entries read, %3 bytes, no file with a problem").arg(n).arg(es.size()).arg(bytes));
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments().mid(1);
    if (a.size() == 2 && a[0] == "--dump") {
        TapeFile t;
        QString err;
        if (!t.open(a[1], &err)) { fprintf(stderr, "%s\n", qPrintable(err)); return 1; }
        for (const QString &l : dump(t)) printf("%s\n", qPrintable(l));
        return 0;
    }
    if (a.size() == 2 && a[0] == "--moved") {
        QString why;
        const QByteArray m = movedTape(&why);
        QFile f(a[1]);
        if (m.isEmpty() || !f.open(QIODevice::WriteOnly) || f.write(m) != m.size()) { fprintf(stderr, "%s\n", qPrintable(why)); return 1; }
        return 0;
    }
    if (a.size() == 2 && a[0] == "--write-samples") {
        QDir().mkpath(a[1]);
        QMap<QString, QByteArray> s = samples();
        {
            QTemporaryDir tmp;
            const int was = fails;
            writing(tmp.path(), &s);                     // (its checks print; a failure there fails the samples)
            if (fails != was) return 1;
        }
        for (auto it = s.begin(); it != s.end(); ++it) {
            QFile f(a[1] + "/" + it.key() + ".uef");
            if (!f.open(QIODevice::WriteOnly) || f.write(it.value()) != it.value().size()) return 1;
        }
        return 0;
    }
    QTemporaryDir tmp;
    handMade(tmp.path() + "/tapes");
    QMap<QString, QByteArray> written;
    writing(tmp.path() + "/writing", &written);
    if (!a.isEmpty()) realTapes(a[0]);
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
