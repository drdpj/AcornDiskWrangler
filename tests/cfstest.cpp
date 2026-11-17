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
//   cfstest --write-samples DIR       the hand-made tapes, for cfs_check.py
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

#include <diskwrangler/tape/records.h>
#include <diskwrangler/tape/tapefile.h>

#include "adfsvolume.h"
#include "cfs.h"
#include "cfsvolume.h"
#include "diskformat.h"

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
        CHECK(!v.write(dw::fs::Path(), "NEW", "x", {}, QDateTime(), false, &err) && !v.remove(dw::fs::Path({"GAME"}), &err),
              "read-only for now: " + err);
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
        CHECK(v.toRetro(es[0]).isEmpty(), "no load or exec to carry");
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
                  od.isOpen() && od.tapeMount() == 0 && od.disc().mounts().size() == 1 && od.disc().mounts()[0].readOnly,
              "the document: one read-only tape mount " + err);
        CHECK(od.disc().container() == nullptr && od.disc().staged() != nullptr, "its Disc stages on the tape, not a container");
        CHECK(!od.readOnlyWhy(0).isEmpty() && !od.dirty() && od.save(&err), "nothing to save: " + od.readOnlyWhy(0));
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
    if (a.size() == 2 && a[0] == "--write-samples") {
        QDir().mkpath(a[1]);
        const QMap<QString, QByteArray> s = samples();
        for (auto it = s.begin(); it != s.end(); ++it) {
            QFile f(a[1] + "/" + it.key() + ".uef");
            if (!f.open(QIODevice::WriteOnly) || f.write(it.value()) != it.value().size()) return 1;
        }
        return 0;
    }
    QTemporaryDir tmp;
    handMade(tmp.path() + "/tapes");
    if (!a.isEmpty()) realTapes(a[0]);
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
