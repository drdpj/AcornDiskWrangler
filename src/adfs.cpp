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

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimeZone>
#include <algorithm>
#include <cstring>
#include <limits>

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

// Free space map check byte, exactly as Acorn's ADFS ROM computes it (ADFS 1.30 &9065): A = &FF, carry clear, then
// ADC bytes &FE down to &00, the carry running on; the final carry is dropped. (b-em's adfs_checksum and the RISC OS
// Open "FileCore Disc Map Old" page agree.) Starting at &FF matters only when the bytes are all zero - e.g. sector 1
// of a disc with no free space - where it gives &FF, not 0; an earlier version started at 0 and got that wrong.
quint8 mapChecksum(const char *s) {
    unsigned sum = 0xFF, carry = 0;
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
    return QDateTime(QDate(1900, 1, 1), QTime(0, 0), QTimeZone::utc()).addSecs(qint64(cs / 100));
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
        bool s1zero = true;
        for (int i = 0; i < 255; ++i) if (s1[i]) s1zero = false;
        if (s1zero && quint8(s1[255]) == 0) {
            // Versions before the checksum was checked against Acorn's ROM wrote 00 here for an all-zero sector 1 (no
            // free space, no disc id, boot option 0: a new DOS Plus 640K disc made from a DOSBOOT file); Acorn's ADFS
            // expects FF. Only that exact case is accepted - Image::open corrects it, so it is saved right.
            m.oldZeroCheck = true;
            warns << "free space map sector 1 has check byte 00, written by an earlier version of Acorn Disk Wrangler "
                     "(Acorn's ADFS expects FF); it has been corrected - save to write the fix";
        } else if (mapChecksum(s1) != quint8(s1[255])) {
            errs << QString("free space map sector 1 checksum is wrong (stored %1, calculated %2)")
                        .arg(quint8(s1[255]), 2, 16, QChar('0')).arg(mapChecksum(s1), 2, 16, QChar('0'));
        }
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

bool Image::openContainer(const QString &path, const SideLayout &sides, QString *err) {
    close();
    if (!m_c.open(path, err)) return false;
    m_c.setRawPadding(0, 0);                    // a short raw file reads as zeros beyond its end
    m_path = path;
    m_sides = sides;
    if (m_c.kind() != dw::device::Kind::Raw) m_sides.sequential = false;   // every track says where it is: stored interleaved
    QFileInfo fi(path);
    m_editable = fi.isWritable() && !m_c.readOnlyContainer();
    if (m_c.kind() != dw::device::Kind::Raw && m_c.storedSize() == 0) {
        QString why;
        m_c.storedSize(&why);
        *err = QString("this %1 can't be read as a whole disc: %2").arg(dw::device::kindName(m_c.kind()), why);
        close();
        return false;
    }
    m_lin = dw::device::Linear(&m_c, m_sides, std::numeric_limits<qint64>::max() / 4);
    return true;
}

// The Linear, sized to the disc (reads and writes past it fail; the callers check the map's bounds first).
void Image::makeLinear() {
    m_c.setRawPadding(qint64(m_fullSize), 0);
    m_c.setWritable(m_editable);
    const qint64 size = isRawFile() ? qint64(qMax<quint64>(m_fullSize, quint64(m_map.total) * SectorSize))
                                    : qMin<qint64>(m_c.storedSize(), qint64(qMax<quint64>(m_fullSize, quint64(m_map.total) * SectorSize)));
    m_lin = dw::device::Linear(&m_c, m_sides, size);
}

quint64 Image::fileSize() const { return isRawFile() ? quint64(m_c.rawFileSize()) : quint64(m_c.storedSize()); }

bool Image::open(const QString &path, QString *err, quint64 forceFullSize, const SideLayout &sides) {
    if (!openContainer(path, sides, err)) return false;
    m_isAdfs = true;
    m_adfsLocked = false;
    m_mapSizeMayExceed = false;
    if (fileSize() < 512) {
        *err = "file is too small to be an ADFS disc image";
        close();
        return false;
    }
    m_orig = readSectors(0, 2);
    QStringList e, w;
    FsMap::parse(m_orig, &m_map, &e, &w);
    m_fullSize = fileSize();
    m_mapSizeMayExceed = forceFullSize != 0;
    if (forceFullSize) {
        m_fullSize = qMax(m_fullSize, forceFullSize);
    } else if (e.isEmpty() && isRawFile()) {
        // Only a *standard floppy* size is trusted enough to pad a short file up to. Anything else (a hard disc, or a
        // disc whose map claims an odd size) is left exactly as it is: guessing here would grow a real image.
        quint64 want = quint64(m_map.total) * SectorSize;
        if (want == 163840 || want == 327680 || want == 655360) m_fullSize = qMax(m_fullSize, want);
    }
    makeLinear();
    m_openReport = validate();
    if (!m_openReport.ok()) m_editable = false;
    m_c.setWritable(m_editable);
    if (m_openReport.ok() && m_map.oldZeroCheck && m_editable) stageFsMap();   // the earlier version's check byte: fix it (staged)
    return true;
}

bool Image::openRaw(const QString &path, quint64 fullSize, QString *err, const SideLayout &sides) {
    if (!openContainer(path, sides, err)) return false;
    m_isAdfs = false;
    m_adfsLocked = false;
    m_fullSize = isRawFile() ? qMax(fullSize, fileSize()) : fileSize();
    m_map = FsMap();
    m_map.total = quint32((m_fullSize + SectorSize - 1) / SectorSize);
    makeLinear();
    m_orig = readSectors(0, 2);
    m_openReport = Report();
    return true;
}

void Image::close() {
    m_lin = dw::device::Linear();
    m_c.close();
    m_sides = SideLayout();
    m_convert = false;
    m_map = FsMap();
    m_isAdfs = true;
    m_adfsLocked = false;
    m_fullSize = 0;
    m_editable = false;
    m_openReport = Report();
    m_path.clear();
    m_stageError.clear();
}

// The file as it is on disc now, in the disc's numbering (for "has something else changed it?").
QByteArray Image::readOnDisk(quint32 start, quint32 count) const {
    dw::device::Container now;
    QString e;
    QByteArray out;
    if (now.open(m_path, &e)) {
        now.setRawPadding(0, 0);
        dw::device::Linear l(&now, m_sides, std::numeric_limits<qint64>::max() / 4);
        l.read(qint64(start) * SectorSize, qint64(count) * SectorSize, &out, &e);
    }
    return out;
}

bool Image::setConvertOnSave(bool toSequential, QString *err) {
    if (!checkEditable(err)) return false;
    if (!m_sides.doubleSided()) { *err = "only a double-sided floppy image has a side order"; return false; }
    if (!isRawFile()) { *err = "only a raw image file has a side order to change: this " + dw::device::kindName(m_c.kind()) + " records where every track is"; return false; }
    m_convert = toSequential != m_sides.sequential;
    return true;
}

bool Image::moveToCopy(const QString &dest, QString *err) {
    if (QFile::exists(dest) && !QFile::remove(dest)) { *err = "could not replace " + dest; return false; }
    if (!QFile::copy(m_path, dest)) { *err = "could not copy the image to " + dest; return false; }
    QFile(dest).setPermissions(QFile(dest).permissions() | QFile::WriteOwner);
    // the copy holds the same bytes, so what is staged applies to it unchanged
    dw::device::Container moved;
    if (!moved.open(dest, err)) { *err = "could not open " + dest + ": " + *err; return false; }
    dw::device::Container::Snapshot staged = m_c.snapshot();
    m_c = moved;
    m_c.restore(staged);
    m_path = dest;
    makeLinear();
    return true;
}

QByteArray Image::readSectors(quint32 start, quint32 count) const {
    QByteArray out;
    QString e;
    if (!m_lin.read(qint64(start) * SectorSize, qint64(count) * SectorSize, &out, &e)) out.clear();
    if (out.size() < int(count * SectorSize)) out.append(QByteArray(int(count * SectorSize) - out.size(), 0));
    return out;
}

bool Image::writeSectors(quint32 start, QByteArray data) {
    if (data.size() % SectorSize) data.append(QByteArray(SectorSize - data.size() % SectorSize, 0));
    QString e;
    if (m_lin.write(qint64(start) * SectorSize, data, &e)) return true;
    if (m_stageError.isEmpty()) m_stageError = QString("sector %1: %2").arg(start).arg(e);
    return false;
}

bool Image::staged(const Snapshot &snap, QString *err) {
    if (m_stageError.isEmpty()) return true;
    *err = m_stageError;
    m_stageError.clear();
    restore(snap);
    return false;
}

bool Image::stageFsMap() { return writeSectors(0, m_map.serialize()); }

bool Image::readDir(quint32 sector, Dir *d, QString *err) const {
    if (quint64(sector) + DirSectors > m_map.total) { *err = "directory lies beyond the end of the disc"; return false; }
    return Dir::parse(sector, readSectors(sector, DirSectors), d, err);
}

bool Image::writeDir(const Dir &d) { return writeSectors(d.sector, d.serialize()); }

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
    Snapshot snap = snapshot();
    writeSectors(sin + quint32(first), s);
    return staged(snap, err);
}

bool Image::pendingTouches(quint32 start, quint32 count) const {
    return m_lin.touched(qint64(start) * SectorSize, qint64(count) * SectorSize);
}

bool Image::checkEditable(QString *err) const {
    if (!m_c.isOpen()) { *err = "no image open"; return false; }
    if (!m_editable) {
        *err = m_c.readOnlyContainer() ? "image is read-only (TeleDisk images can only be read)"
                                       : "image is read-only (file not writable, or its free space map failed validation)";
        return false;
    }
    return true;
}

bool Image::freeRun(quint32 start, quint32 n, QString *err) {
    if (!m_map.release(start, n, err)) return false;
    // drop staged data that lay completely inside the freed area
    if (start >= 2) m_lin.discard(qint64(start) * SectorSize, qint64(n) * SectorSize);
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
    if (!addFileInner(parent, name, data, load, exec, flags, replace, err)) { m_stageError.clear(); restore(snap); return false; }
    stageFsMap();
    return staged(snap, err);
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
    if (!ok) { m_stageError.clear(); restore(snap); return false; }
    stageFsMap();
    return staged(snap, err);
}

bool Image::deleteEntry(quint32 parent, const QString &name, QString *err) {
    if (!checkAdfs(err)) return false;
    Snapshot snap = snapshot();
    if (!deleteInner(parent, name, err)) { m_stageError.clear(); restore(snap); return false; }
    stageFsMap();
    return staged(snap, err);
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

int Image::bootOption() const {
    return m_map.base.size() == 512 ? quint8(m_map.base[256 + 0xFD]) : 0;
}

bool Image::setBootOption(int opt, QString *err) {
    if (!checkAdfs(err)) return false;
    if (opt < 0 || opt > 3) { *err = "the boot option must be 0 (off), 1 (*LOAD), 2 (*RUN) or 3 (*EXEC)"; return false; }
    if (m_map.base.size() != 512) { *err = "the free space map has not been read"; return false; }
    Snapshot snap = snapshot();
    m_map.base[256 + 0xFD] = char(opt);
    stageFsMap();                                   // serialize() keeps byte &FD and recomputes both checksums
    return staged(snap, err);
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
    return staged(snap, err);
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
    if (!ok) { m_stageError.clear(); restore(snap); return false; }
    return staged(snap, err);
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

    quint64 size = fileSize(), want = quint64(map.total) * SectorSize;
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
    if (!m_c.hasPending() && !needsPadding() && !m_convert) return true;

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
    if (readOnDisk(0, 2) != m_orig) {
        *err = "The free space map on disk has changed since the image was opened (is another program "
               "using it?). NOTHING was written. Re-open the image and redo your changes.";
        return false;
    }

    // Changing the side order: the whole disc (everything staged included) is laid out again in the other order and
    // staged over the file. Track 0 side 0 is first in both orders.
    Snapshot before = snapshot();
    SideLayout after = m_sides;
    if (m_convert) {
        after.sequential = !m_sides.sequential;
        QByteArray disc = readSectors(0, quint32((m_fullSize + SectorSize - 1) / SectorSize));
        QByteArray file = layoutFile(disc, after);
        file.truncate(int(m_fullSize));
        QString e;
        if (!m_c.writeStored(0, file, &e)) { restore(before); *err = "Could not change the side order: " + e + "\nNOTHING was written."; return false; }
    }

    // The copy that will replace the file is opened again as an Image, as it will be read next time, and checked.
    Report check;
    dw::device::Container::SaveOptions opt;
    opt.backup = makeBackup;
    opt.verify = [&](dw::device::Container &written, QString *e) {
        Image saved;
        const bool ok = m_isAdfs ? saved.open(written.path(), e, m_mapSizeMayExceed ? m_fullSize : 0, after)
                                 : saved.openRaw(written.path(), m_fullSize, e, after);
        if (!ok) { *e = "The saved copy could not be read back (" + *e + "). NOTHING was written."; return false; }
        check = saved.validate();
        QStringList problems = check.errors;
        if (m_reopenCheck) problems += m_reopenCheck(saved);
        if (!problems.isEmpty()) {
            *e = "The saved copy failed its check, so it has not replaced the image. NOTHING was written.\n\n" + problems.join("\n");
            return false;
        }
        return true;
    };
    QString e;
    if (!m_c.commit(opt, &e)) {
        restore(before);
        *err = e.contains("NOTHING was written") ? e : e + "\nNOTHING was written.";
        return false;
    }
    m_sides = after;
    m_convert = false;
    makeLinear();
    m_orig = readSectors(0, 2);
    if (post) *post = check;
    return true;
}

void Image::revert() {
    m_c.discardPending();
    m_convert = false;
    if (!m_isAdfs) return;
    QStringList e, w;
    FsMap::parse(m_orig, &m_map, &e, &w);
}

} // namespace Adfs
