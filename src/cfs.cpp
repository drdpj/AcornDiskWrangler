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
#include "cfs.h"

#include <QHash>
#include <QSet>

using dw::tape::Piece;

namespace Acorn {
namespace Cfs {

quint16 crc(const QByteArray &bytes, quint16 start) {
    quint16 c = start;
    for (char ch : bytes) {
        c ^= quint16(uchar(ch) << 8);
        for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? quint16((c << 1) ^ 0x1021) : quint16(c << 1);
    }
    return c;
}

QString nameText(const QByteArray &name) { return QString::fromLatin1(name); }
QByteArray nameBytes(const QString &name) { return name.toLatin1(); }

int Tape::files() const {
    int n = 0;
    for (const Item &i : items) if (i.type == Item::FileItem) ++n;
    return n;
}

int Tape::goodBlocks() const {
    int n = 0;
    for (const Block &b : blocks) if (b.headerOk) ++n;
    return n;
}

namespace {

// The tape as the serial ULA hands it to the MOS: bytes (with the chunk each came from, and whether it is framed 8N1,
// which is all the MOS reads), carrier tone, and silence.
struct Event {
    enum Type { Byte, Tone, Gap };
    Type type;
    uchar value;
    int chunk;
    bool readable;
};

bool readable(const dw::tape::Coding &c) {
    return c.kind == dw::tape::Coding::Serial && c.dataBits == 8 && c.parity == 'N' && c.stopBits == 1 && !c.extraWave &&
           !c.atom;
}

QVector<Event> events(const QVector<Piece> &pieces) {
    QVector<Event> ev;
    for (const Piece &p : pieces) {
        switch (p.type) {
        case Piece::Data: {
            const bool ok = readable(p.coding);
            for (int i = 0; i < p.bytes.size(); ++i) ev.append({Event::Byte, uchar(p.bytes[i]), p.chunkOf(i), ok});
            break;
        }
        case Piece::Tone: ev.append({Event::Tone, 0, p.first, false}); break;
        case Piece::Gap: ev.append({Event::Gap, 0, p.first, false}); break;
        default: break;                                  // text, the base frequency, ...: nothing on the line
        }
    }
    return ev;
}

// The bytes of a block from ev[from] on: carrier between them is the line idling and is passed over; a gap, an
// unreadable byte or the end of the tape stops it. Reads up to n more bytes into got (their indices).
bool take(const QVector<Event> &ev, int *at, int n, QVector<int> *got) {
    while (n > 0) {
        while (*at < ev.size() && ev[*at].type == Event::Tone) ++*at;
        if (*at >= ev.size() || ev[*at].type != Event::Byte || !ev[*at].readable) return false;
        got->append(*at);
        ++*at;
        --n;
    }
    return true;
}

quint32 le(const QByteArray &b, int at, int n) {
    quint32 v = 0;
    for (int i = 0; i < n; ++i) v |= quint32(uchar(b[at + i])) << (8 * i);
    return v;
}

// Every block, and the events no block took.
QVector<Block> readBlocks(const QVector<Event> &ev, QVector<int> *spare) {
    QVector<Block> out;
    bool armed = true;                                   // the start of the tape counts as after carrier
    int i = 0;
    while (i < ev.size()) {
        const Event &e = ev[i];
        if (e.type == Event::Tone) { armed = true; ++i; continue; }
        if (e.type == Event::Gap) { armed = false; ++i; continue; }
        if (!(armed && e.readable && e.value == 0x2A)) {
            spare->append(i);
            armed = false;                               // anything but &2A after carrier: the MOS waits for carrier again
            ++i;
            continue;
        }
        const int start = i;
        int at = i + 1;
        QVector<int> got;
        // the name: up to its &00, at most 11 bytes (MOS 1.20 &F742-&F756)
        QByteArray name;
        bool ok = true;
        while (name.size() < 11) {
            if (!take(ev, &at, 1, &got)) { ok = false; break; }
            const uchar v = ev[got.last()].value;
            name.append(char(v));
            if (v == 0) break;
        }
        if (ok && !take(ev, &at, 19, &got)) ok = false;
        if (!ok) {                                       // cut short: the &2A and what followed are unrecognised
            spare->append(start);
            *spare += got;
            armed = false;
            i = at;
            continue;
        }
        QByteArray hdr;
        for (int k : got) hdr.append(char(ev[k].value));
        Block b;
        b.first = ev[start].chunk;
        b.name = name.left(10);
        const int z = b.name.indexOf('\0');
        if (z >= 0) b.name.truncate(z);
        const QByteArray f = hdr.mid(name.size());
        b.load = le(f, 0, 4);
        b.exec = le(f, 4, 4);
        b.number = quint16(le(f, 8, 2));
        b.length = quint16(le(f, 10, 2));
        b.flag = uchar(f[12]);
        b.spare = f.mid(13, 4);
        b.headerOk = crc(hdr) == 0;
        const int n = b.length == 0 ? 0 : ((b.length & 0xFF) ? (b.length & 0xFF) : 256);
        if (n) {
            const int k0 = got.size();
            QByteArray d;
            if (!take(ev, &at, n + 2, &got)) {
                b.cut = true;
                b.dataOk = false;
                for (int k = k0; k < got.size() && k < k0 + n; ++k) d.append(char(ev[got[k]].value));
                b.data = d;
            } else {
                for (int k = k0; k < got.size(); ++k) d.append(char(ev[got[k]].value));
                b.data = d.left(n);
                b.dataOk = crc(d) == 0;
            }
        }
        b.last = ev[got.last()].chunk;
        out.append(b);
        armed = false;
        i = at;
    }
    return out;
}

} // namespace

Tape read(const QVector<Piece> &pieces) {
    Tape t;
    const QVector<Event> ev = events(pieces);
    QVector<int> spare;
    t.blocks = readBlocks(ev, &spare);

    // blocks into files: a run with the same name, numbered on from 0, ending at the last-block flag
    QVector<Item> files;
    int cur = -1;
    int next = 0;
    bool done = true;
    auto close = [&]() {
        if (cur >= 0 && !done) files[cur].file.problems << "no last block";
    };
    for (int bi = 0; bi < t.blocks.size(); ++bi) {
        const Block &b = t.blocks.at(bi);
        QStringList problems;
        if (!b.headerOk) problems << QString("block %1 header CRC").arg(b.number);
        if (b.cut) problems << QString("block %1 cut short").arg(b.number);
        else if (!b.dataOk) problems << QString("block %1 data CRC").arg(b.number);
        if (!(cur >= 0 && !done && b.name == files[cur].file.name && b.number == next)) {
            close();
            Item it;
            it.type = Item::FileItem;
            it.first = b.first;
            it.file.name = b.name;
            it.file.load = b.load;
            it.file.exec = b.exec;
            it.file.flag = b.flag;
            it.file.first = b.first;
            it.file.firstBlock = bi;
            if (b.number != 0) it.file.problems << QString("starts at block %1").arg(b.number);
            files.append(it);
            cur = files.size() - 1;
            done = false;
        }
        File &f = files[cur].file;
        f.problems += problems;
        if (!b.isLast() && b.data.size() != 256 && !b.cut)
            f.problems << QString("block %1 is %2 bytes").arg(b.number).arg(b.data.size());
        f.data += b.data;
        ++f.blocks;
        f.last = b.last;
        files[cur].last = b.last;
        next = b.number + 1;
        if (b.isLast()) done = true;
    }
    close();

    // what no block took: bytes left in a block's own chunks (the MOS sends more after a block's CRC) and a chunk
    // holding one byte and nothing else (the dummy byte, noise before a file) are normal; anything more is data the
    // cassette filing system can't read. What lies between two files is one row.
    QSet<int> blockChunks;
    for (const Block &b : t.blocks)
        for (int c = b.first; c <= b.last; ++c) blockChunks.insert(c);
    QHash<int, int> perChunk;                            // bytes in each chunk
    for (const Event &e : ev) if (e.type == Event::Byte) ++perChunk[e.chunk];
    struct Run { int first, last, bytes; };
    QVector<Run> runs;
    for (int k : spare) {
        const int c = ev[k].chunk;
        if (blockChunks.contains(c) || perChunk.value(c) == 1) continue;
        bool fileBetween = false;
        if (!runs.isEmpty())
            for (const Item &f : files)
                if (runs.last().last < f.first && f.first <= c) { fileBetween = true; break; }
        if (!runs.isEmpty() && !fileBetween) {
            runs.last().last = c;
            ++runs.last().bytes;
        } else {
            runs.append({c, c, 1});
        }
    }
    QVector<Item> all = files;
    for (const Run &r : runs) {
        Item it;
        it.type = Item::Unrecognised;
        it.first = r.first;
        it.last = r.last;
        it.bytes = r.bytes;
        all.append(it);
    }
    std::stable_sort(all.begin(), all.end(), [](const Item &a, const Item &b) { return a.first < b.first; });
    t.items = all;
    return t;
}

// ----------------------------------------------------------------- writing

QByteArray encodeBlock(const QByteArray &name, quint32 load, quint32 exec, int number, const QByteArray &data, quint8 flag,
                       const QByteArray &spare) {
    auto le = [](QByteArray *b, quint32 v, int n) { for (int i = 0; i < n; ++i) b->append(char((v >> (8 * i)) & 0xFF)); };
    QByteArray h = name.left(10);
    h.append('\0');
    le(&h, load, 4);
    le(&h, exec, 4);
    le(&h, quint32(number), 2);
    le(&h, quint32(data.size()), 2);
    h.append(char(flag));
    h.append((spare + QByteArray(4, '\0')).left(4));
    const quint16 hc = crc(h);
    QByteArray b = QByteArray(1, '\x2A') + h + char(hc >> 8) + char(hc & 0xFF);
    if (!data.isEmpty()) {
        const quint16 dc = crc(data);
        b += data + char(dc >> 8) + char(dc & 0xFF);
    }
    return b;
}

QVector<QByteArray> encodeFile(const QByteArray &name, quint32 load, quint32 exec, const QByteArray &data, bool protect) {
    QVector<QByteArray> out;
    const int n = data.isEmpty() ? 1 : int((data.size() + 255) / 256);
    for (int k = 0; k < n; ++k)
        out << encodeBlock(name, load, exec, k, data.mid(k * 256, 256), quint8((protect ? 0x01 : 0) | (k == n - 1 ? 0x80 : 0)));
    return out;
}

QVector<dw::tape::Chunk> saveChunks(const QVector<QByteArray> &blocks, bool afterOthers) {
    namespace uef = dw::tape::uef;
    QVector<dw::tape::Chunk> c;
    if (afterOthers) c << uef::gap(Mos::StopSeconds);
    c << uef::carrierWithDummy(Mos::DummyLeadCycles, Mos::LeaderCycles);
    for (int k = 0; k < blocks.size(); ++k) {
        if (k) c << uef::carrier(Mos::BlockGapCycles);
        c << uef::data(blocks[k]);
    }
    c << uef::carrier(Mos::TrailerCycles);
    return c;
}

namespace {

// What the chunks before a point leave set: the base frequency (&0113) and the baud rate (&0117).
struct Mode {
    double hz = 1200;
    int baud = 1200;
    bool operator==(const Mode &o) const { return hz == o.hz && baud == o.baud; }
};

Mode modeBefore(const QVector<dw::tape::Chunk> &c, int at) {
    Mode m;
    for (int i = 0; i < at && i < c.size(); ++i) {
        if (c[i].id == 0x0113 && c[i].body.size() >= 4) m.hz = dw::tape::uef::getFloat(c[i].body, 0);
        if (c[i].id == 0x0117 && c[i].body.size() >= 2) m.baud = uchar(c[i].body[0]) | (uchar(c[i].body[1]) << 8);
    }
    return m;
}

int itemLast(const Item &i) { return i.type == Item::FileItem ? i.file.last : i.last; }
int itemFirst(const Item &i) { return i.type == Item::FileItem ? i.file.first : i.first; }

} // namespace

Edit::Edit(const QVector<dw::tape::Chunk> &chunks, const Tape &tape)
    : m_chunks(chunks), m_tape(tape), m_pieces(dw::tape::pieces(dw::tape::Kind::Uef, chunks)) {
    // the information chunks the tape opens with stay at its start
    m_lead = chunks.size();
    for (const Piece &p : m_pieces)
        if (p.type != Piece::Info) { m_lead = p.first; break; }
    int from = m_lead;
    for (const Item &i : m_tape.items) {
        const int first = itemFirst(i), last = itemLast(i);
        if (first < from || last < first) m_ok = false;   // items that overlap: never changed
        m_spans.append({qMin(from, first), last});
        from = last + 1;
    }
}

bool Edit::usable(QString *why) const {
    if (!m_ok && why) *why = "the tape's files and data overlap, so it can't be changed safely";
    return m_ok;
}

// The segments one after another, each played at the base frequency and baud rate it was (new chunks: the defaults,
// 1200 Hz and 1200 baud, at which the MOS saves).
QVector<dw::tape::Chunk> Edit::join(const QVector<Seg> &segs) const {
    QVector<dw::tape::Chunk> out;
    Mode cur;
    for (const Seg &s : segs) {
        if (!s.fresh && s.last < s.first) continue;
        const Mode need = s.fresh ? Mode() : modeBefore(m_chunks, s.first);
        if (need.hz != cur.hz) out << dw::tape::uef::baseFrequency(need.hz);
        if (need.baud != cur.baud) {
            QByteArray b;
            b.append(char(need.baud & 0xFF)).append(char((need.baud >> 8) & 0xFF));
            out << dw::tape::Chunk(0x0117, b);
        }
        if (s.fresh) {
            out += s.chunks;
            cur = Mode();
        } else {
            for (int i = s.first; i <= s.last; ++i) out << m_chunks[i];
            cur = modeBefore(m_chunks, s.last + 1);
        }
    }
    return out;
}

bool Edit::append(const QVector<QByteArray> &blocks, QVector<dw::tape::Chunk> *out, QString *why) const {
    if (!usable(why)) return false;
    bool before = false;                                 // anything on the tape but information
    for (const Piece &p : m_pieces) if (p.type != Piece::Info) before = true;
    *out = join({old(0, m_chunks.size() - 1), made(saveChunks(blocks, before))});
    return true;
}

bool Edit::replace(int item, const QVector<QByteArray> &blocks, QVector<dw::tape::Chunk> *out, QString *why) const {
    if (!usable(why)) return false;
    if (item < 0 || item >= m_tape.items.size() || m_tape.items[item].type != Item::FileItem) { if (why) *why = "no such file"; return false; }
    const File &f = m_tape.items[item].file;
    QVector<Seg> segs;
    bool oneForOne = blocks.size() == f.blocks;
    for (int k = 1; oneForOne && k < f.blocks; ++k)
        if (m_tape.blocks[f.firstBlock + k].first <= m_tape.blocks[f.firstBlock + k - 1].last) oneForOne = false;
    if (oneForOne) {
        int at = 0;
        for (int k = 0; k < f.blocks; ++k) {
            const Block &b = m_tape.blocks[f.firstBlock + k];
            segs << old(at, b.first - 1) << made({dw::tape::uef::data(blocks[k])});
            at = b.last + 1;
        }
        segs << old(at, m_chunks.size() - 1);
    } else {
        QVector<dw::tape::Chunk> c;
        for (int k = 0; k < blocks.size(); ++k) {
            if (k) c << dw::tape::uef::carrier(Mos::BlockGapCycles);
            c << dw::tape::uef::data(blocks[k]);
        }
        segs << old(0, f.first - 1) << made(c) << old(f.last + 1, m_chunks.size() - 1);
    }
    *out = join(segs);
    return true;
}

bool Edit::remove(int item, QVector<dw::tape::Chunk> *out, QString *why) const {
    if (!usable(why)) return false;
    if (item < 0 || item >= m_spans.size()) { if (why) *why = "no such file"; return false; }
    *out = join({old(0, m_spans[item].first - 1), old(m_spans[item].last + 1, m_chunks.size() - 1)});
    return true;
}

bool Edit::move(const QVector<int> &items, int before, QVector<dw::tape::Chunk> *out, QString *why) const {
    if (!usable(why)) return false;
    const int n = m_spans.size();
    QSet<int> moving;
    for (int i : items) {
        if (i < 0 || i >= n) { if (why) *why = "no such file"; return false; }
        moving.insert(i);
    }
    if (before < 0 || before > n) { if (why) *why = "no such place on the tape"; return false; }
    while (before < n && moving.contains(before)) ++before;
    QVector<int> order;
    for (int i = 0; i <= n; ++i) {
        if (i == before)
            for (int k = 0; k < n; ++k) if (moving.contains(k)) order << k;
        if (i < n && !moving.contains(i)) order << i;
    }
    QVector<Seg> segs;
    segs << old(0, m_lead - 1);
    for (int i : order) {
        const Span &s = m_spans[i];
        const Item &it = m_tape.items[i];
        bool lead = false;                               // carrier before its first block, as the MOS needs
        for (const Piece &p : m_pieces)
            if (p.type == Piece::Tone && p.first >= s.first && p.last < itemFirst(it)) lead = true;
        if (it.type == Item::FileItem && !lead) segs << made({dw::tape::uef::carrier(Mos::LeaderCycles)});
        segs << old(s.first, s.last);
    }
    segs << old(n ? m_spans[n - 1].last + 1 : m_lead, m_chunks.size() - 1);
    *out = join(segs);
    return true;
}

} // namespace Cfs
} // namespace Acorn
