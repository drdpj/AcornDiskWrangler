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
#include "adfs.h"

#include <QSet>
#include <algorithm>
#include <cstring>

namespace Adfs {

// ---------------------------------------------------------------- helpers

static quint32 le24(const char *p) {
    const quint8 *u = reinterpret_cast<const quint8 *>(p);
    return quint32(u[0]) | (quint32(u[1]) << 8) | (quint32(u[2]) << 16);
}
static quint32 le32(const char *p) {
    const quint8 *u = reinterpret_cast<const quint8 *>(p);
    return le24(p) | (quint32(u[3]) << 24);
}
static void put24(char *p, quint32 v) {
    p[0] = char(v & 0xFF); p[1] = char((v >> 8) & 0xFF); p[2] = char((v >> 16) & 0xFF);
}
static void put32(char *p, quint32 v) { put24(p, v); p[3] = char((v >> 24) & 0xFF); }

bool isValidNameChar(QChar qc) {
    ushort c = qc.unicode();
    if (c <= 0x20 || c >= 0x7f) return false;
    static const QByteArray bad = "\"#$%&*.:@\\^|";
    return !bad.contains(char(c));
}

bool isValidName(const QString &n, QString *why) {
    if (n.isEmpty()) { if (why) *why = "name is empty"; return false; }
    if (n.size() > MaxNameLen) { if (why) *why = "name is longer than 10 characters"; return false; }
    for (QChar c : n)
        if (!isValidNameChar(c)) {
            if (why) *why = QString("character '%1' is not allowed in ADFS names").arg(c);
            return false;
        }
    return true;
}

QString sanitizeName(const QString &host) {
    QString out;
    for (QChar c : host) {
        if (out.size() >= MaxNameLen) break;
        out += isValidNameChar(c) ? c : QChar('_');
    }
    return out.isEmpty() ? QString("_") : out;
}

quint8 bcdInc(quint8 v) {
    unsigned lo = (v & 0x0F) + 1, hi = v >> 4;
    if (lo > 9) { lo = 0; ++hi; }
    if (hi > 9) hi = 0;
    return quint8((hi << 4) | lo);
}

// ADC-style checksum over bytes 254..0, carry propagated, final carry dropped.
quint8 mapChecksum(const char *s) {
    unsigned sum = 0, carry = 0;
    for (int i = 254; i >= 0; --i) {
        unsigned t = sum + quint8(s[i]) + carry;
        carry = t > 255 ? 1 : 0;
        sum = t & 255;
    }
    return quint8(sum);
}

// ------------------------------------------------------------------ Entry

QString Entry::name() const {
    QString s;
    for (int i = 0; i < 10; ++i) {
        quint8 c = quint8(nameRaw[i]) & 0x7f;
        if (c == 0x0d || c == 0) break;
        s += QChar(c);
    }
    return s;
}

void Entry::setName(const QString &n) {
    QByteArray old = nameRaw, nr(10, 0x0d);
    for (int i = 0; i < n.size() && i < 10; ++i) nr[i] = char(n[i].toLatin1());
    for (int i = 0; i < 5; ++i)
        if (quint8(old[i]) & 0x80) nr[i] = char(quint8(nr[i]) | 0x80);
    nameRaw = nr;
}

void Entry::setFlag(int i, bool on) {
    quint8 b = quint8(nameRaw[i]);
    nameRaw[i] = char(on ? (b | 0x80) : (b & 0x7f));
}

QString Entry::attrString() const {
    QString s;
    if (flag(D)) s += 'D';
    if (flag(L)) s += 'L';
    if (flag(W)) s += 'W';
    if (flag(R)) s += 'R';
    if (flag(E)) s += 'E';
    return s;
}

QDateTime Entry::stamp() const {
    quint64 cs = (quint64(load & 0xFF) << 32) | exec;
    return QDateTime(QDate(1900, 1, 1), QTime(0, 0), Qt::UTC).addSecs(qint64(cs / 100));
}

// -------------------------------------------------------------------- Dir

QString Dir::title() const {
    QString s;
    for (int i = 14; i < 33; ++i) {
        quint8 c = quint8(tail[i]) & 0x7f;
        if (c == 0x0d || c == 0) break;
        s += QChar(c);
    }
    return s;
}

quint32 Dir::parent() const { return le24(tail.constData() + 11); }

int Dir::indexOf(const QString &n) const {
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].name().compare(n, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

void Dir::sortEntries() {
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.name().toUpper() < b.name().toUpper();
    });
}

QByteArray Dir::serialize() const {
    QByteArray b(DirBytes, 0);
    char *p = b.data();
    p[0] = char(seq);
    memcpy(p + 1, "Hugo", 4);
    for (int i = 0; i < entries.size() && i < MaxDirEntries; ++i) {
        char *e = p + 5 + i * 26;
        const Entry &en = entries[i];
        memcpy(e, en.nameRaw.constData(), 10);
        put32(e + 10, en.load);
        put32(e + 14, en.exec);
        put32(e + 18, en.length);
        put24(e + 22, en.sin);
        e[25] = char(en.seq);
    }
    QByteArray t = tail;
    t.resize(53);
    t[0] = 0;
    t[47] = char(seq);
    memcpy(t.data() + 48, "Hugo", 4);
    t[52] = 0;
    memcpy(p + 5 + MaxDirEntries * 26, t.constData(), 53);
    return b;
}

bool Dir::parse(quint32 sector, const QByteArray &raw, Dir *out, QString *err) {
    if (raw.size() != DirBytes) { *err = "short read"; return false; }
    if (raw.mid(1, 4) != "Hugo" || raw.mid(0x4FB, 4) != "Hugo") {
        *err = "not an ADFS 'Hugo' directory (missing markers)";
        return false;
    }
    if (quint8(raw[0]) != quint8(raw[0x4FA])) {
        *err = "directory start/end sequence numbers differ (broken directory)";
        return false;
    }
    Dir d;
    d.sector = sector;
    d.seq = quint8(raw[0]);
    for (int i = 0; i < MaxDirEntries; ++i) {
        const char *p = raw.constData() + 5 + i * 26;
        if (p[0] == 0) break;
        Entry e;
        e.nameRaw = QByteArray(p, 10);
        e.load = le32(p + 10);
        e.exec = le32(p + 14);
        e.length = le32(p + 18);
        e.sin = le24(p + 22);
        e.seq = quint8(p[25]);
        d.entries.append(e);
    }
    d.tail = raw.mid(5 + MaxDirEntries * 26, 53);
    *out = d;
    return true;
}

// ------------------------------------------------------------------ FsMap

bool FsMap::parse(const QByteArray &s01, FsMap *out, QStringList *errors, QStringList *warnings) {
    FsMap m;
    QStringList errs, warns;
    if (s01.size() != 512) {
        errs << "could not read the free space map (sectors 0-1)";
    } else {
        m.base = s01;
        const char *s0 = s01.constData(), *s1 = s0 + 256;
        if (mapChecksum(s0) != quint8(s0[255]))
            errs << QString("free space map sector 0 checksum is wrong (stored %1, calculated %2)")
                        .arg(quint8(s0[255]), 2, 16, QChar('0')).arg(mapChecksum(s0), 2, 16, QChar('0'));
        if (mapChecksum(s1) != quint8(s1[255]))
            errs << QString("free space map sector 1 checksum is wrong (stored %1, calculated %2)")
                        .arg(quint8(s1[255]), 2, 16, QChar('0')).arg(mapChecksum(s1), 2, 16, QChar('0'));
        m.total = le24(s0 + 0xFC);
        if (m.total < 8) errs << "disc size in free space map is implausible";
        int end = quint8(s1[0xFE]);
        if (end % 3 != 0 || end > MaxFreeRuns * 3) {
            errs << QString("free space list end pointer is invalid (%1)").arg(end);
            end = 0;
        }
        quint64 prevEnd = 0;
        for (int i = 0; i < end / 3; ++i) {
            FsMap::Run r{le24(s0 + i * 3), le24(s1 + i * 3)};
            m.runs.append(r);
            QString id = QString("free run %1 (sector %2, %3 long)").arg(i).arg(r.start).arg(r.len);
            if (r.len == 0) errs << id + " has zero length";
            if (r.start < FirstDataSector) errs << id + " overlaps the map/root directory";
            if (quint64(r.start) + r.len > m.total) errs << id + " extends beyond the end of the disc";
            if (i > 0) {
                if (r.start < prevEnd) errs << id + " overlaps or is out of order with the previous run";
                else if (r.start == prevEnd) warns << id + " touches the previous run (should have been merged)";
            }
            prevEnd = quint64(r.start) + r.len;
        }
    }
    *out = m;
    if (errors) *errors += errs;
    if (warnings) *warnings += warns;
    return errs.isEmpty();
}

QByteArray FsMap::serialize() const {
    QByteArray b = base.size() == 512 ? base : QByteArray(512, 0);
    char *s0 = b.data(), *s1 = b.data() + 256;
    for (int i = 0; i < MaxFreeRuns * 3; ++i) { s0[i] = 0; s1[i] = 0; }
    for (int i = 0; i < runs.size(); ++i) {
        put24(s0 + i * 3, runs[i].start);
        put24(s1 + i * 3, runs[i].len);
    }
    put24(s0 + 0xFC, total);
    s1[0xFE] = char(runs.size() * 3);
    s0[0xFF] = char(mapChecksum(s0));
    s1[0xFF] = char(mapChecksum(s1));
    return b;
}

quint32 FsMap::freeSectors() const {
    quint64 n = 0;
    for (const Run &r : runs) n += r.len;
    return quint32(n);
}

quint32 FsMap::largestRun() const {
    quint32 n = 0;
    for (const Run &r : runs) n = qMax(n, r.len);
    return n;
}

bool FsMap::allocate(quint32 n, quint32 *start) {
    int best = -1;
    for (int i = 0; i < runs.size(); ++i)
        if (runs[i].len >= n && (best < 0 || runs[i].len < runs[best].len)) best = i;
    if (best < 0) return false;
    *start = runs[best].start;
    runs[best].start += n;
    runs[best].len -= n;
    if (runs[best].len == 0) runs.remove(best);
    return true;
}

bool FsMap::release(quint32 start, quint32 n, QString *err) {
    if (n == 0) return true;
    if (start < FirstDataSector || quint64(start) + n > total) {
        *err = QString("internal error: freeing sectors %1+%2 outside the data area").arg(start).arg(n);
        return false;
    }
    int i = 0;
    while (i < runs.size() && runs[i].start < start) ++i;
    if ((i > 0 && quint64(runs[i - 1].start) + runs[i - 1].len > start) ||
        (i < runs.size() && quint64(start) + n > runs[i].start)) {
        *err = QString("internal error: sectors %1+%2 are already free").arg(start).arg(n);
        return false;
    }
    bool mergePrev = i > 0 && quint64(runs[i - 1].start) + runs[i - 1].len == start;
    bool mergeNext = i < runs.size() && quint64(start) + n == runs[i].start;
    if (mergePrev && mergeNext) {
        runs[i - 1].len += n + runs[i].len;
        runs.remove(i);
    } else if (mergePrev) {
        runs[i - 1].len += n;
    } else if (mergeNext) {
        runs[i].start = start;
        runs[i].len += n;
    } else {
        if (runs.size() >= MaxFreeRuns) {
            *err = "the free space map is full (82 fragments); the disc needs compacting before more space can be freed";
            return false;
        }
        runs.insert(i, Run{start, n});
    }
    return true;
}

quint64 SideLayout::toFile(quint64 off, quint64 *room) const {
    if (!doubleSided()) { if (room) *room = ~quint64(0) - off; return off; }
    quint64 t = off / trackBytes, in = off % trackBytes;
    if (room) *room = trackBytes - in;
    if (t >= 2ull * cylinders) return off;
    quint64 cyl = sideFirst ? t % cylinders : t / 2, side = sideFirst ? t / cylinders : t % 2;
    quint64 f = sequential ? side * cylinders + cyl : cyl * 2 + side;
    return f * trackBytes + in;
}

QByteArray layoutFile(const QByteArray &disc, const SideLayout &lay) {
    if (!lay.remaps()) return disc;
    QByteArray out = disc;
    const quint64 T = lay.trackBytes;
    for (quint64 t = 0; t < 2ull * lay.cylinders && (t + 1) * T <= quint64(disc.size()); ++t) {
        quint64 f = lay.toFile(t * T);
        if (f + T <= quint64(out.size())) memcpy(out.data() + f, disc.constData() + t * T, size_t(T));
    }
    return out;
}

QByteArray reorderSides(const QByteArray &file, const SideLayout &lay, bool toSequential) {
    if (!lay.doubleSided()) return file;
    SideLayout il = lay, seq = lay;                    // file track t of an interleaved file <-> its place in a sequential one
    il.sideFirst = seq.sideFirst = false;
    il.sequential = false;
    seq.sequential = true;
    QByteArray out = file;
    const quint64 T = lay.trackBytes;
    for (quint64 t = 0; t < 2ull * lay.cylinders && (t + 1) * T <= quint64(file.size()); ++t) {
        quint64 s = seq.toFile(t * T), i = il.toFile(t * T);
        if (s + T > quint64(file.size())) continue;
        if (toSequential) memcpy(out.data() + s, file.constData() + i, size_t(T));
        else memcpy(out.data() + i, file.constData() + s, size_t(T));
    }
    return out;
}

QByteArray makeBlankImage(quint32 totalSectors, const QString &title, bool allFree) {
    QByteArray img(int(totalSectors) * SectorSize, 0);
    FsMap m;
    m.total = totalSectors;
    m.base = QByteArray(512, 0);
    if (allFree && totalSectors > FirstDataSector) m.runs.append(FsMap::Run{FirstDataSector, totalSectors - FirstDataSector});
    QByteArray map = m.serialize();
    Dir d;
    d.sector = RootSector;
    d.tail = QByteArray(53, 0);
    d.tail[1] = '$';
    for (int i = 2; i <= 10; ++i) d.tail[i] = '\r';
    put24(d.tail.data() + 11, RootSector);
    QByteArray t(19, '\r');
    for (int i = 0; i < title.size() && i < 10; ++i) t[i] = char(title[i].toLatin1());
    memcpy(d.tail.data() + 14, t.constData(), 19);
    QByteArray dir = d.serialize();
    memcpy(img.data(), map.constData(), 512);
    memcpy(img.data() + RootSector * SectorSize, dir.constData(), size_t(dir.size()));
    return img;
}

// ------------------------------------------------------------------ Image

bool Image::open(const QString &path, QString *err, quint64 forceFullSize, const SideLayout &sides) {
    close();
    m_sides = sides;
    m_file.setFileName(path);
    m_editable = true;
    if (!m_file.open(QIODevice::ReadWrite)) {
        m_editable = false;
        if (!m_file.open(QIODevice::ReadOnly)) {
            *err = m_file.errorString();
            return false;
        }
    }
    m_path = path;
    m_isAdfs = true;
    m_adfsLocked = false;
    m_mapSizeMayExceed = false;
    m_pending.clear();
    m_orig = readRaw(0, 2);
    if (m_orig.size() != 512) {
        *err = "file is too small to be an ADFS disc image";
        close();
        return false;
    }
    QStringList e, w;
    FsMap::parse(m_orig, &m_map, &e, &w);
    m_fullSize = quint64(m_file.size());
    m_mapSizeMayExceed = forceFullSize != 0;
    if (forceFullSize) {
        m_fullSize = qMax(m_fullSize, forceFullSize);
    } else if (e.isEmpty()) {
        // Only a *standard floppy* size is trusted enough to pad a short file up to. Anything else (a hard disc, or a
        // disc whose map claims an odd size) is left exactly as it is: guessing here would grow a real image.
        quint64 want = quint64(m_map.total) * SectorSize;
        if (want == 163840 || want == 327680 || want == 655360) m_fullSize = qMax(m_fullSize, want);
    }
    m_openReport = validate();
    if (!m_openReport.ok()) m_editable = false;
    return true;
}

bool Image::openRaw(const QString &path, quint64 fullSize, QString *err, const SideLayout &sides) {
    close();
    m_sides = sides;
    m_file.setFileName(path);
    m_editable = true;
    if (!m_file.open(QIODevice::ReadWrite)) {
        m_editable = false;
        if (!m_file.open(QIODevice::ReadOnly)) {
            *err = m_file.errorString();
            return false;
        }
    }
    m_path = path;
    m_isAdfs = false;
    m_adfsLocked = false;
    m_pending.clear();
    m_fullSize = qMax(fullSize, quint64(m_file.size()));
    m_map = FsMap();
    m_map.total = quint32((m_fullSize + SectorSize - 1) / SectorSize);
    m_orig = readRaw(0, 2);
    if (m_orig.size() < 512) m_orig.append(QByteArray(512 - m_orig.size(), 0));
    m_openReport = Report();
    return true;
}

void Image::close() {
    if (m_file.isOpen()) m_file.close();
    m_pending.clear();
    m_sides = SideLayout();
    m_convert = false;
    m_map = FsMap();
    m_isAdfs = true;
    m_adfsLocked = false;
    m_fullSize = 0;
    m_editable = false;
    m_openReport = Report();
    m_path.clear();
}

QByteArray Image::readRaw(quint32 start, quint32 count) const {
    if (!m_file.isOpen()) return QByteArray();
    if (!m_sides.remaps()) {
        if (!m_file.seek(qint64(start) * SectorSize)) return QByteArray();
        return m_file.read(qint64(count) * SectorSize);
    }
    // the file's track order differs from the disc's numbering: read track by track; a piece past the end of a
    // short file reads as zeros
    QByteArray out;
    quint64 off = quint64(start) * SectorSize, left = quint64(count) * SectorSize;
    while (left) {
        quint64 room;
        quint64 f = m_sides.toFile(off, &room);
        qint64 n = qint64(qMin(left, room));
        QByteArray piece;
        if (m_file.seek(qint64(f))) piece = m_file.read(n);
        if (piece.size() < n) piece.append(QByteArray(int(n - piece.size()), 0));
        out.append(piece);
        off += quint64(n); left -= quint64(n);
    }
    return out;
}

bool Image::writeRaw(quint64 off, const QByteArray &data) {
    qint64 done = 0;
    while (done < data.size()) {
        quint64 room;
        quint64 f = m_sides.toFile(off + quint64(done), &room);
        qint64 n = qMin<qint64>(data.size() - done, qint64(qMin<quint64>(room, quint64(data.size()))));
        if (!m_file.seek(qint64(f)) || m_file.write(data.constData() + done, n) != n) return false;
        done += n;
    }
    return true;
}

bool Image::setConvertOnSave(bool toSequential, QString *err) {
    if (!checkEditable(err)) return false;
    if (!m_sides.doubleSided()) { *err = "only a double-sided floppy image has a side order"; return false; }
    m_convert = toSequential != m_sides.sequential;
    return true;
}

QByteArray Image::readSectors(quint32 start, quint32 count) const {
    QByteArray out = readRaw(start, count);
    if (out.size() < int(count * SectorSize)) out.append(QByteArray(int(count * SectorSize) - out.size(), 0));
    for (const Extent &e : m_pending) {
        quint64 es = e.start, ec = e.data.size() / SectorSize;
        quint64 lo = qMax<quint64>(start, es), hi = qMin<quint64>(quint64(start) + count, es + ec);
        if (lo < hi)
            memcpy(out.data() + (lo - start) * SectorSize, e.data.constData() + (lo - es) * SectorSize,
                   (hi - lo) * SectorSize);
    }
    return out;
}

void Image::writeSectors(quint32 start, QByteArray data) {
    if (data.size() % SectorSize) data.append(QByteArray(SectorSize - data.size() % SectorSize, 0));
    for (int i = 0; i < m_pending.size(); ++i)
        if (m_pending[i].start == start && m_pending[i].data.size() == data.size()) {
            m_pending.removeAt(i);
            break;
        }
    m_pending.append(Extent{start, data});
}

void Image::stageFsMap() { writeSectors(0, m_map.serialize()); }

bool Image::readDir(quint32 sector, Dir *d, QString *err) const {
    if (quint64(sector) + DirSectors > m_map.total) { *err = "directory lies beyond the end of the disc"; return false; }
    return Dir::parse(sector, readSectors(sector, DirSectors), d, err);
}

bool Image::writeDir(const Dir &d) {
    writeSectors(d.sector, d.serialize());
    return true;
}

bool Image::readFile(const Entry &e, QByteArray *out, QString *err) const {
    quint32 n = sectorsFor(e.length);
    if (n == 0) { out->clear(); return true; }
    if (quint64(e.sin) + n > m_map.total) { *err = "file lies beyond the end of the disc"; return false; }
    *out = readSectors(e.sin, n).left(int(e.length));
    return true;
}

bool Image::checkAdfs(QString *err) const {
    if (!m_isAdfs) { *err = "this image does not contain an ADFS file system"; return false; }
    if (m_adfsLocked) { *err = "the ADFS part of this hybrid disc is read-only (edit the DOS side instead)"; return false; }
    return checkEditable(err);
}

bool Image::readFileBytes(quint32 sin, quint32 fileLen, quint64 off, quint32 len, QByteArray *out, QString *err) const {
    if (off + len > fileLen) { *err = "read beyond the end of the file"; return false; }
    if (len == 0) { out->clear(); return true; }
    quint64 first = off / SectorSize, last = (off + len - 1) / SectorSize;
    if (quint64(sin) + last >= m_map.total) { *err = "read beyond the end of the disc"; return false; }
    QByteArray s = readSectors(sin + quint32(first), quint32(last - first + 1));
    *out = s.mid(int(off - first * SectorSize), int(len));
    return true;
}

bool Image::writeFileBytes(quint32 sin, quint32 fileLen, quint64 off, const QByteArray &data, QString *err) {
    if (!checkEditable(err)) return false;
    if (data.isEmpty()) return true;
    if (off + data.size() > fileLen) { *err = "write beyond the end of the file"; return false; }
    quint64 first = off / SectorSize, last = (off + data.size() - 1) / SectorSize;
    if (quint64(sin) + last >= m_map.total) { *err = "write beyond the end of the disc"; return false; }
    QByteArray s = readSectors(sin + quint32(first), quint32(last - first + 1));
    memcpy(s.data() + (off - first * SectorSize), data.constData(), size_t(data.size()));
    writeSectors(sin + quint32(first), s);
    return true;
}

bool Image::pendingTouches(quint32 start, quint32 count) const {
    for (const Extent &e : m_pending) {
        quint64 es = e.start, ee = es + e.data.size() / SectorSize;
        if (es < quint64(start) + count && quint64(start) < ee) return true;
    }
    return false;
}

bool Image::checkEditable(QString *err) const {
    if (!m_file.isOpen()) { *err = "no image open"; return false; }
    if (!m_editable) { *err = "image is read-only (file not writable, or its free space map failed validation)"; return false; }
    return true;
}

bool Image::freeRun(quint32 start, quint32 n, QString *err) {
    if (!m_map.release(start, n, err)) return false;
    // drop staged data that lay completely inside the freed area
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        const Extent &e = m_pending[i];
        if (e.start >= start && quint64(e.start) + e.data.size() / SectorSize <= quint64(start) + n && e.start >= 2)
            m_pending.removeAt(i);
    }
    return true;
}

bool Image::freeTree(quint32 dirSector, int depth, QString *err) {
    if (depth > 64) { *err = "directory nesting too deep"; return false; }
    Dir d;
    if (!readDir(dirSector, &d, err)) return false;
    for (const Entry &e : d.entries) {
        if (e.isDir()) { if (!freeTree(e.sin, depth + 1, err)) return false; }
        else if (!freeRun(e.sin, sectorsFor(e.length), err)) return false;
    }
    return freeRun(dirSector, DirSectors, err);
}

// -------------------------------------------------------------- modifications

bool Image::addFile(quint32 parent, const QString &name, const QByteArray &data, quint32 load,
                    quint32 exec, quint8 flags, bool replace, QString *err) {
    if (!checkAdfs(err)) return false;
    Snapshot snap = snapshot();
    if (!addFileInner(parent, name, data, load, exec, flags, replace, err)) { restore(snap); return false; }
    stageFsMap();
    return true;
}

bool Image::addFileInner(quint32 parent, const QString &name, const QByteArray &data, quint32 load,
                         quint32 exec, quint8 flags, bool replace, QString *err) {
    QString why;
    if (!isValidName(name, &why)) { *err = QString("'%1': %2").arg(name, why); return false; }
    if (quint64(data.size()) > 0xFFFFFFFFull) { *err = "file is too large"; return false; }
    Dir d;
    if (!readDir(parent, &d, err)) return false;
    int idx = d.indexOf(name);
    if (idx >= 0) {
        if (!replace) { *err = QString("'%1' already exists").arg(name); return false; }
        if (d.entries[idx].isDir()) { *err = QString("'%1' is a directory").arg(name); return false; }
        if (!freeRun(d.entries[idx].sin, sectorsFor(d.entries[idx].length), err)) return false;
        d.entries.removeAt(idx);
    }
    if (d.entries.size() >= MaxDirEntries) { *err = "directory is full (ADFS allows 47 entries per directory)"; return false; }

    quint32 n = sectorsFor(quint32(data.size())), start = 0;
    if (n) {
        if (!m_map.allocate(n, &start)) {
            *err = QString("not enough contiguous free space for '%1' (needs %2 sectors; largest free block is %3, total free %4)")
                       .arg(name).arg(n).arg(m_map.largestRun()).arg(m_map.freeSectors());
            return false;
        }
        writeSectors(start, data);
    } else {
        start = m_map.runs.isEmpty() ? FirstDataSector : m_map.runs.first().start;   // empty file: no sectors used
    }

    Entry e;
    e.setName(name);
    e.setFlag(Entry::R, flags & FlagR);
    e.setFlag(Entry::W, flags & FlagW);
    e.setFlag(Entry::L, flags & FlagL);
    e.setFlag(Entry::E, flags & FlagE);
    e.load = load; e.exec = exec; e.length = quint32(data.size()); e.sin = start;
    d.seq = bcdInc(d.seq);
    e.seq = d.seq;
    d.entries.append(e);
    d.sortEntries();
    return writeDir(d);
}

bool Image::makeDir(quint32 parent, const QString &name, QString *err, quint32 *newSector) {
    if (!checkAdfs(err)) return false;
    Snapshot snap = snapshot();
    bool ok = [&]() -> bool {
        QString why;
        if (!isValidName(name, &why)) { *err = QString("'%1': %2").arg(name, why); return false; }
        Dir d;
        if (!readDir(parent, &d, err)) return false;
        if (d.indexOf(name) >= 0) { *err = QString("'%1' already exists").arg(name); return false; }
        if (d.entries.size() >= MaxDirEntries) { *err = "directory is full (ADFS allows 47 entries per directory)"; return false; }
        quint32 start = 0;
        if (!m_map.allocate(DirSectors, &start)) { *err = "not enough contiguous free space for a new directory"; return false; }

        Entry e;
        e.setName(name);
        e.setFlag(Entry::R, true); e.setFlag(Entry::W, true); e.setFlag(Entry::D, true);
        e.length = DirBytes; e.sin = start;
        d.seq = bcdInc(d.seq);
        e.seq = d.seq;

        Dir nd;
        nd.sector = start;
        nd.seq = 0;
        nd.tail = QByteArray(53, 0);
        memcpy(nd.tail.data() + 1, e.nameRaw.constData(), 10);
        put24(nd.tail.data() + 11, parent);
        QByteArray title = name.toLatin1() + '\r';
        memcpy(nd.tail.data() + 14, title.constData(), qMin(19, int(title.size())));
        writeDir(nd);

        d.entries.append(e);
        d.sortEntries();
        writeDir(d);
        if (newSector) *newSector = start;
        return true;
    }();
    if (!ok) { restore(snap); return false; }
    stageFsMap();
    return true;
}

bool Image::deleteEntry(quint32 parent, const QString &name, QString *err) {
    if (!checkAdfs(err)) return false;
    Snapshot snap = snapshot();
    if (!deleteInner(parent, name, err)) { restore(snap); return false; }
    stageFsMap();
    return true;
}

bool Image::deleteInner(quint32 parent, const QString &name, QString *err) {
    Dir d;
    if (!readDir(parent, &d, err)) return false;
    int idx = d.indexOf(name);
    if (idx < 0) { *err = QString("'%1' not found").arg(name); return false; }
    const Entry e = d.entries[idx];
    if (e.isDir()) { if (!freeTree(e.sin, 0, err)) return false; }
    else if (!freeRun(e.sin, sectorsFor(e.length), err)) return false;
    d.entries.removeAt(idx);
    d.seq = bcdInc(d.seq);
    return writeDir(d);
}

bool Image::setDiscTitle(const QString &title, QString *err) {
    if (!checkAdfs(err)) return false;
    if (title.size() > MaxNameLen) { *err = "the disc name can be at most 10 characters"; return false; }
    for (QChar ch : title)
        if (ch.unicode() < 0x20 || ch.unicode() > 0x7E) { *err = "the disc name may only contain printable ASCII characters"; return false; }
    Snapshot snap = snapshot();
    Dir d;
    if (!readDir(RootSector, &d, err)) return false;
    QByteArray t(19, 0x0d);                       // ADFS pads titles with CR
    for (int i = 0; i < title.size(); ++i) t[i] = char(title[i].toLatin1());
    memcpy(d.tail.data() + 14, t.constData(), 19);
    d.seq = bcdInc(d.seq);
    writeDir(d);
    if (m_map.base.size() == 512) {               // keep a disc name already held in the map in step
        char *s0 = m_map.base.data(), *s1 = s0 + 256;
        bool had = false;
        for (int i = 0; i < 5; ++i) if (s0[0xF6 + i] || s1[0xF6 + i]) had = true;
        if (had) {
            quint8 last = quint8(s1[0xF6 + 4]);
            char pad = (last == 0 || last == 0x0d || last == 0x20) ? char(last) : ' ';
            QString nm = title.left(10);
            for (int i = 0; i < 10; ++i) (i % 2 ? s1 : s0)[0xF6 + i / 2] = i < nm.size() ? char(nm[i].toLatin1()) : pad;
        }
    }
    stageFsMap();
    return true;
}

bool Image::replaceEntry(quint32 parent, const QString &oldName, const Entry &upd, QString *err) {
    if (!checkAdfs(err)) return false;
    Snapshot snap = snapshot();
    bool ok = [&]() -> bool {
        Dir d;
        if (!readDir(parent, &d, err)) return false;
        int idx = d.indexOf(oldName);
        if (idx < 0) { *err = QString("'%1' not found").arg(oldName); return false; }
        QString newName = upd.name(), why;
        if (!isValidName(newName, &why)) { *err = QString("'%1': %2").arg(newName, why); return false; }
        bool renamed = newName != d.entries[idx].name();
        if (newName.compare(oldName, Qt::CaseInsensitive) != 0 && d.indexOf(newName) >= 0) {
            *err = QString("'%1' already exists").arg(newName);
            return false;
        }
        Entry e = upd;
        e.length = d.entries[idx].length;
        e.sin = d.entries[idx].sin;
        e.seq = d.entries[idx].seq;
        e.setFlag(Entry::D, d.entries[idx].isDir());
        d.entries[idx] = e;
        d.seq = bcdInc(d.seq);
        d.sortEntries();
        writeDir(d);
        if (e.isDir() && renamed) {      // keep the directory's own copy of its name in step
            Dir c;
            if (readDir(e.sin, &c, err)) {
                memcpy(c.tail.data() + 1, e.nameRaw.constData(), 10);
                c.seq = bcdInc(c.seq);
                writeDir(c);
            }
        }
        return true;
    }();
    if (!ok) { restore(snap); return false; }
    return true;
}

// ----------------------------------------------------------------- validate

Report Image::validate() const {
    Report r;
    if (!m_isAdfs) return r;          // DFS / DOS volumes are checked by their own validators
    FsMap map;
    QStringList e, w;
    FsMap::parse(readSectors(0, 2), &map, &e, &w);
    r.errors += e;
    r.warnings += w;

    quint64 size = quint64(m_file.size()), want = quint64(map.total) * SectorSize;
    if (want > size) {
        if (m_mapSizeMayExceed)
            r.warnings << QString("the ADFS map claims %1 sectors but the disc holds %2 (normal for an Acorn DOS Plus "
                                  "hybrid disc: the DOS area is not described by the ADFS map)").arg(map.total).arg(size / SectorSize);
        else if (m_fullSize >= want)
            r.warnings << QString("image file is %1 bytes shorter than the disc size in the free space map; "
                                  "it will be padded with zeros when saved").arg(want - size);
        else
            r.errors << QString("free space map says the disc has %1 sectors but the image file only holds %2")
                            .arg(map.total).arg(size / SectorSize);
    } else if (want < size) {
        r.warnings << QString("image file is %1 bytes larger than the disc size in the free space map").arg(size - want);
    }

    struct Obj { quint32 start; quint32 len; QString what; bool isFree; };
    QVector<Obj> objs;
    objs.append({0, 2, "free space map", false});
    for (int i = 0; i < map.runs.size(); ++i)
        objs.append({map.runs[i].start, map.runs[i].len, QString("free run %1").arg(i), true});

    struct Item { quint32 sector; QString path; int depth; };
    QVector<Item> stack;
    stack.append({RootSector, "$", 0});
    QSet<quint32> seen;
    int dirs = 0, files = 0;
    while (!stack.isEmpty()) {
        Item it = stack.takeLast();
        if (seen.contains(it.sector)) {
            r.errors << QString("directory %1 is referenced more than once (loop or cross-link)").arg(it.path);
            continue;
        }
        seen.insert(it.sector);
        if (it.depth > 64) { r.errors << QString("directory nesting too deep at %1").arg(it.path); continue; }
        if (it.sector < RootSector || quint64(it.sector) + DirSectors > map.total) {
            r.errors << QString("directory %1 is at an invalid sector (%2)").arg(it.path).arg(it.sector);
            continue;
        }
        objs.append({it.sector, DirSectors, "directory " + it.path, false});
        ++dirs;
        Dir d;
        QString perr;
        if (!Dir::parse(it.sector, readSectors(it.sector, DirSectors), &d, &perr)) {
            r.errors << QString("directory %1: %2").arg(it.path, perr);
            continue;
        }
        for (const Entry &en : d.entries) {
            QString nm = en.name(), p = it.path + "." + nm;
            if (nm.isEmpty()) { r.errors << QString("empty file name in directory %1").arg(it.path); continue; }
            if (en.isDir()) {
                stack.append({en.sin, p, it.depth + 1});
            } else {
                ++files;
                quint32 n = sectorsFor(en.length);
                if (n == 0) continue;
                if (en.sin < FirstDataSector || quint64(en.sin) + n > map.total)
                    r.errors << QString("file %1 (sector %2, %3 sectors) lies outside the data area").arg(p).arg(en.sin).arg(n);
                else
                    objs.append({en.sin, n, "file " + p, false});
            }
        }
    }

    std::stable_sort(objs.begin(), objs.end(), [](const Obj &a, const Obj &b) { return a.start < b.start; });
    quint64 maxEnd = 0, used = 0, freeSecs = 0;
    QString maxWhat;
    int overlaps = 0;
    for (const Obj &o : objs) {
        if (o.start < maxEnd) {
            if (++overlaps <= 20)
                r.errors << QString("%1 (sector %2) overlaps %3").arg(o.what).arg(o.start).arg(maxWhat);
        }
        if (quint64(o.start) + o.len > maxEnd) { maxEnd = quint64(o.start) + o.len; maxWhat = o.what; }
        (o.isFree ? freeSecs : used) += o.len;
    }
    if (overlaps > 20) r.errors << QString("... and %1 more overlaps").arg(overlaps - 20);
    if (!m_mapSizeMayExceed && overlaps == 0 && quint64(map.total) >= used + freeSecs && map.total - (used + freeSecs) > 0)
        r.warnings << QString("%1 sectors are neither free nor used by any file (lost space)")
                          .arg(map.total - (used + freeSecs));
    r.info << QString("%1 directories, %2 files, %3 of %4 sectors free in %5 fragment(s)")
                  .arg(dirs).arg(files).arg(map.freeSectors()).arg(map.total).arg(map.runs.size());
    return r;
}

// --------------------------------------------------------------- commit/revert

bool Image::commit(QString *err, bool makeBackup, Report *post) {
    if (!checkEditable(err)) return false;
    if (m_pending.isEmpty() && !needsPadding() && !m_convert) return true;

    Report pre = validate();
    if (!pre.ok()) {
        *err = "The free space map / directory structure failed validation. NOTHING was written.\n\n" +
               pre.errors.join("\n");
        return false;
    }
    if (m_extra) {
        QStringList ex = m_extra();
        if (!ex.isEmpty()) {
            *err = "A consistency check failed. NOTHING was written.\n\n" + ex.join("\n");
            return false;
        }
    }
    if (readRaw(0, 2) != m_orig) {
        *err = "The free space map on disk has changed since the image was opened (is another program "
               "using it?). NOTHING was written. Re-open the image and redo your changes.";
        return false;
    }
    if (makeBackup) {
        QString bak = m_path + ".bak";
        QFile::remove(bak);
        if (!QFile::copy(m_path, bak)) { *err = "could not create backup " + bak + ". NOTHING was written."; return false; }
    }

    // never leave a truncated image behind: pad it to its full size first
    if (quint64(m_file.size()) < m_fullSize && !m_file.resize(qint64(m_fullSize))) {
        *err = "Could not pad the image to its full size: " + m_file.errorString() + "\nNOTHING was written.";
        return false;
    }

    if (m_convert) {
        // Rewrite the whole disc in the other side order: the disc (with everything staged) is laid out again and
        // written over the file, sectors 0-1 last. Track 0 side 0 is first in both orders.
        quint32 n = quint32((m_fullSize + SectorSize - 1) / SectorSize);
        QByteArray disc = readSectors(0, n);
        SideLayout to = m_sides;
        to.sequential = !m_sides.sequential;
        QByteArray file = layoutFile(disc, to);
        file.truncate(int(m_fullSize));
        if (!m_file.seek(512) || m_file.write(file.constData() + 512, file.size() - 512) != file.size() - 512 ||
            !m_file.seek(0) || m_file.write(file.constData(), 512) != 512) {
            *err = QString("Write failed while changing the side order: %1\nThe image may now be inconsistent - "
                           "restore it from a backup.").arg(m_file.errorString());
            return false;
        }
        m_sides = to;
        m_convert = false;
        m_pending.clear();
    }

    // data and directories first, the free space map (sectors 0-1) last
    for (int pass = 0; pass < 2; ++pass)
        for (const Extent &e : m_pending) {
            bool isMap = e.start < 2;
            if ((pass == 0) == isMap) continue;
            if (!writeRaw(quint64(e.start) * SectorSize, e.data)) {
                *err = QString("Write failed at sector %1: %2\nThe image may now be inconsistent - "
                               "check it before using it.").arg(e.start).arg(m_file.errorString());
                return false;
            }
        }
    m_file.flush();
    m_pending.clear();
    m_orig = readRaw(0, 2);
    Report p = validate();
    if (post) *post = p;
    if (!p.ok()) {
        *err = "Saved, but verification afterwards reported problems:\n\n" + p.errors.join("\n");
        return false;
    }
    return true;
}

void Image::revert() {
    m_pending.clear();
    m_convert = false;
    if (!m_isAdfs) return;
    QStringList e, w;
    FsMap::parse(m_orig, &m_map, &e, &w);
}

} // namespace Adfs
