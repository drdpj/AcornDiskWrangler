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
#include "dfs.h"

#include <algorithm>
#include <cstring>

using namespace Adfs;

namespace Dfs {

bool validNameChar(QChar qc) {
    ushort c = qc.unicode();
    if (c <= 0x20 || c >= 0x7F) return false;
    return !QByteArray(".:\"#*").contains(char(c));
}
bool validDir(QChar c) { return validNameChar(c); }

bool validName(const QString &n, QString *why) {
    if (n.isEmpty()) { if (why) *why = "name is empty"; return false; }
    if (n.size() > MaxName) { if (why) *why = "DFS names are at most 7 characters"; return false; }
    for (QChar c : n)
        if (!validNameChar(c)) { if (why) *why = QString("character '%1' is not allowed in DFS names").arg(c); return false; }
    return true;
}

QString sanitizeName(const QString &host) {
    QString o;
    for (QChar c : host) { if (o.size() >= MaxName) break; o += validNameChar(c) ? c : QChar('_'); }
    return o.isEmpty() ? QString("_") : o;
}

bool plausibleCatalogue(const QByteArray &s0, const QByteArray &s1, quint32 *sectors) {
    if (s0.size() < 256 || s1.size() < 256) return false;
    quint8 cnt = quint8(s1[5]);
    if (cnt % 8 != 0 || cnt > MaxFiles * 8) return false;
    quint32 sec = (quint32(quint8(s1[6]) & 3) << 8) | quint8(s1[7]);
    if (sec < 2 || sec > 800) return false;
    for (int i = 1; i <= cnt / 8; ++i) {
        for (int k = 0; k < 7; ++k) { quint8 c = quint8(s0[i * 8 + k]) & 0x7F; if (c < 0x20 || c > 0x7E) return false; }
        quint8 d = quint8(s0[i * 8 + 7]) & 0x7F;
        if (d < 0x20 || d > 0x7E) return false;
        quint8 hi = quint8(s1[i * 8 + 6]);
        quint32 start = (quint32(hi & 3) << 8) | quint8(s1[i * 8 + 7]);
        quint32 len = quint8(s1[i * 8 + 4]) | (quint32(quint8(s1[i * 8 + 5])) << 8) | (quint32((hi >> 4) & 3) << 16);
        if (start < 2 || start + (len + 255) / 256 > sec) return false;
    }
    if (sectors) *sectors = sec;
    return true;
}

// ------------------------------------------------------------ sector access

quint32 Disc::phys(quint32 n) const {
    if (!m_lay.doubleSided) return n;
    if (m_lay.interleaved) return ((n / SectorsPerTrack) * 2 + quint32(m_side)) * SectorsPerTrack + n % SectorsPerTrack;
    return quint32(m_side) * m_lay.sectorsPerSide + n;
}

bool Disc::rd(quint32 n, quint32 count, QByteArray *out, QString *err) const {
    out->clear();
    while (count) {
        quint32 chunk = count;
        if (m_lay.doubleSided && m_lay.interleaved) chunk = qMin(count, quint32(SectorsPerTrack) - n % SectorsPerTrack);
        QByteArray t;
        if (!m_img->readFileBytes(0, m_len, quint64(phys(n)) * SectorSize, chunk * SectorSize, &t, err)) return false;
        out->append(t);
        n += chunk; count -= chunk;
    }
    return true;
}

bool Disc::wr(quint32 n, const QByteArray &data, QString *err) {
    quint32 count = quint32(data.size() / SectorSize);
    int pos = 0;
    while (count) {
        quint32 chunk = count;
        if (m_lay.doubleSided && m_lay.interleaved) chunk = qMin(count, quint32(SectorsPerTrack) - n % SectorsPerTrack);
        if (!m_img->writeFileBytes(0, m_len, quint64(phys(n)) * SectorSize, data.mid(pos, int(chunk * SectorSize)), err)) return false;
        n += chunk; count -= chunk; pos += int(chunk * SectorSize);
    }
    return true;
}

// ----------------------------------------------------------------- catalogue

bool Disc::attach(Image *img, int side, const Layout &lay, QString *err) {
    m_img = img; m_side = side; m_lay = lay; m_len = quint32(img->fullSize());
    if (!load(err)) { m_img = nullptr; return false; }
    return true;
}

bool Disc::load(QString *err) {
    QByteArray s0, s1;
    if (!rd(0, 1, &s0, err) || !rd(1, 1, &s1, err)) return false;
    quint32 sec = 0;
    if (!plausibleCatalogue(s0, s1, &sec)) { *err = "not a valid DFS catalogue"; return false; }
    m_sectors = sec;
    m_cycle = quint8(s1[4]);
    m_boot = (quint8(s1[6]) >> 4) & 3;
    QByteArray t;
    for (int i = 0; i < 8; ++i) t.append(s0[i]);
    for (int i = 0; i < 4; ++i) t.append(s1[i]);
    int end = t.indexOf('\0');
    if (end >= 0) t.truncate(end);
    m_title = QString::fromLatin1(t).trimmed();
    m_files.clear();
    int cnt = quint8(s1[5]) / 8;
    for (int i = 1; i <= cnt; ++i) {
        File f;
        QByteArray nm;
        for (int k = 0; k < 7; ++k) nm.append(char(quint8(s0[i * 8 + k]) & 0x7F));
        f.name = QString::fromLatin1(nm).trimmed();
        f.dir = QChar(quint8(s0[i * 8 + 7]) & 0x7F);
        f.locked = (quint8(s0[i * 8 + 7]) & 0x80) != 0;
        quint8 hi = quint8(s1[i * 8 + 6]);
        f.load = quint8(s1[i * 8]) | (quint32(quint8(s1[i * 8 + 1])) << 8) | (quint32((hi >> 2) & 3) << 16);
        f.exec = quint8(s1[i * 8 + 2]) | (quint32(quint8(s1[i * 8 + 3])) << 8) | (quint32((hi >> 6) & 3) << 16);
        f.length = quint8(s1[i * 8 + 4]) | (quint32(quint8(s1[i * 8 + 5])) << 8) | (quint32((hi >> 4) & 3) << 16);
        f.start = (quint32(hi & 3) << 8) | quint8(s1[i * 8 + 7]);
        m_files.append(f);
    }
    return true;
}

bool Disc::writeCatalogue(QString *err) {
    QByteArray s0(SectorSize, 0), s1(SectorSize, 0);
    QByteArray t = m_title.toLatin1().left(MaxTitle);
    for (int i = 0; i < 12; ++i) {
        char c = i < t.size() ? t[i] : 0;
        if (i < 8) s0[i] = c; else s1[i - 8] = c;
    }
    m_cycle = bcdInc(m_cycle);
    s1[4] = char(m_cycle);
    s1[5] = char(m_files.size() * 8);
    s1[6] = char(((m_boot & 3) << 4) | ((m_sectors >> 8) & 3));
    s1[7] = char(m_sectors & 0xFF);
    for (int i = 0; i < m_files.size(); ++i) {
        const File &f = m_files[i];
        int o = (i + 1) * 8;
        QByteArray nm = f.name.toLatin1().left(MaxName);
        for (int k = 0; k < 7; ++k) s0[o + k] = k < nm.size() ? nm[k] : ' ';
        s0[o + 7] = char(f.dir.unicode() | (f.locked ? 0x80 : 0));
        s1[o] = char(f.load & 0xFF); s1[o + 1] = char((f.load >> 8) & 0xFF);
        s1[o + 2] = char(f.exec & 0xFF); s1[o + 3] = char((f.exec >> 8) & 0xFF);
        s1[o + 4] = char(f.length & 0xFF); s1[o + 5] = char((f.length >> 8) & 0xFF);
        s1[o + 6] = char(((f.start >> 8) & 3) | (((f.load >> 16) & 3) << 2) | (((f.length >> 16) & 3) << 4) |
                         (((f.exec >> 16) & 3) << 6));
        s1[o + 7] = char(f.start & 0xFF);
    }
    return wr(0, s0 + s1, err);
}

int Disc::indexOf(QChar dir, const QString &name) const {
    for (int i = 0; i < m_files.size(); ++i)
        if (m_files[i].dir.toUpper() == dir.toUpper() && m_files[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

static quint32 capacity(const Disc &d, const Layout &lay) {
    quint32 c = d.sectors();
    if (lay.sectorsPerSide) c = qMin(c, lay.sectorsPerSide);
    return c;
}

quint32 Disc::freeSectors() const {
    quint32 used = 2, cap = capacity(*this, m_lay);
    for (const File &f : m_files) used += f.sectors();
    return cap > used ? cap - used : 0;
}

bool Disc::findGap(quint32 n, quint32 *start) const {
    QVector<QPair<quint32, quint32>> occ;
    for (const File &f : m_files) if (f.sectors()) occ.append({f.start, f.start + f.sectors()});
    std::sort(occ.begin(), occ.end());
    quint32 cursor = 2, cap = capacity(*this, m_lay);
    for (const auto &o : occ) {
        if (o.first >= cursor && o.first - cursor >= n) { *start = cursor; return true; }
        cursor = qMax(cursor, o.second);
    }
    if (cap >= cursor && cap - cursor >= n) { *start = cursor; return true; }
    return false;
}

quint32 Disc::largestFree() const {
    QVector<QPair<quint32, quint32>> occ;
    for (const File &f : m_files) if (f.sectors()) occ.append({f.start, f.start + f.sectors()});
    std::sort(occ.begin(), occ.end());
    quint32 cursor = 2, best = 0, cap = capacity(*this, m_lay);
    for (const auto &o : occ) {
        if (o.first > cursor) best = qMax(best, o.first - cursor);
        cursor = qMax(cursor, o.second);
    }
    if (cap > cursor) best = qMax(best, cap - cursor);
    return best;
}

bool Disc::readFile(const File &f, QByteArray *out, QString *err) const {
    out->clear();
    if (f.length == 0) return true;
    if (f.start + f.sectors() > capacity(*this, m_lay)) {
        *err = "file lies beyond the end of the disc";
        return false;
    }
    if (!rd(f.start, f.sectors(), out, err)) return false;
    out->truncate(int(f.length));
    return true;
}

// ---------------------------------------------------------------- mutations

#define DFS_TXN(call)                                                          \
    do {                                                                       \
        if (!m_img || !m_img->editable()) { *err = "image is read-only"; return false; } \
        Image::Snapshot snap = m_img->snapshot();                              \
        Disc before = *this;                                                   \
        bool ok = (call);                                                      \
        if (!ok) { m_img->restore(snap); *this = before; return false; }       \
        return true;                                                           \
    } while (0)

bool Disc::addFile(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec, bool locked,
                   bool replace, QString *err) {
    DFS_TXN(addInner(dir, name, data, load, exec, locked, replace, err));
}

bool Disc::addInner(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec, bool locked,
                    bool replace, QString *err) {
    QString why;
    if (!validName(name, &why)) { *err = QString("'%1': %2").arg(name, why); return false; }
    if (!validDir(dir)) { *err = QString("'%1' is not a valid DFS directory character").arg(dir); return false; }
    if (data.size() > 0x3FFFF) { *err = "file too large for DFS"; return false; }
    int idx = indexOf(dir, name);
    if (idx >= 0) {
        if (!replace) { *err = QString("'%1.%2' already exists").arg(dir, name); return false; }
        m_files.removeAt(idx);
    }
    if (m_files.size() >= MaxFiles) { *err = "the catalogue is full (DFS allows 31 files per disc side)"; return false; }
    quint32 n = quint32((data.size() + SectorSize - 1) / SectorSize), start = 0;
    if (n == 0) {
        start = 2;
        for (const File &f : m_files) start = qMax(start, f.start);       // harmless address for an empty file
        if (start > capacity(*this, m_lay)) start = 2;
    } else if (!findGap(n, &start)) {
        *err = QString("not enough contiguous free space on the DFS disc for '%1' (needs %2 sectors; largest free block %3, total free %4)")
                   .arg(name).arg(n).arg(largestFree()).arg(freeSectors());
        return false;
    }
    if (n) {
        QByteArray padded = data;
        padded.append(QByteArray(int(n * SectorSize) - data.size(), 0));
        if (!wr(start, padded, err)) return false;
    }
    File f;
    f.name = name; f.dir = dir; f.locked = locked;
    f.load = load & 0x3FFFF; f.exec = exec & 0x3FFFF;
    f.length = quint32(data.size()); f.start = start;
    m_files.append(f);
    std::stable_sort(m_files.begin(), m_files.end(), [](const File &a, const File &b) { return a.start > b.start; });
    return writeCatalogue(err);
}

bool Disc::remove(QChar dir, const QString &name, QString *err) {
    DFS_TXN([&]() -> bool {
        int idx = indexOf(dir, name);
        if (idx < 0) { *err = QString("'%1.%2' not found").arg(dir, name); return false; }
        m_files.removeAt(idx);
        return writeCatalogue(err);
    }());
}

bool Disc::modify(QChar oldDir, const QString &oldName, QChar newDir, const QString &newName, bool locked,
                  quint32 load, quint32 exec, QString *err) {
    DFS_TXN([&]() -> bool {
        int idx = indexOf(oldDir, oldName);
        if (idx < 0) { *err = QString("'%1.%2' not found").arg(oldDir, oldName); return false; }
        QString why;
        if (!validName(newName, &why)) { *err = QString("'%1': %2").arg(newName, why); return false; }
        if (!validDir(newDir)) { *err = QString("'%1' is not a valid DFS directory character").arg(newDir); return false; }
        int clash = indexOf(newDir, newName);
        if (clash >= 0 && clash != idx) { *err = QString("'%1.%2' already exists").arg(newDir, newName); return false; }
        File &f = m_files[idx];
        f.name = newName; f.dir = newDir; f.locked = locked;
        f.load = load & 0x3FFFF; f.exec = exec & 0x3FFFF;
        return writeCatalogue(err);
    }());
}

bool Disc::setTitle(const QString &title, QString *err) {
    DFS_TXN([&]() -> bool {
        if (title.size() > MaxTitle) { *err = "the DFS disc title can be at most 12 characters"; return false; }
        for (QChar c : title)
            if (c.unicode() < 0x20 || c.unicode() > 0x7E) { *err = "the title may only contain printable ASCII characters"; return false; }
        m_title = title;
        return writeCatalogue(err);
    }());
}

bool Disc::setBootOption(int opt, QString *err) {
    DFS_TXN([&]() -> bool {
        if (opt < 0 || opt > 3) { *err = "the boot option must be 0 (off), 1 (*LOAD), 2 (*RUN) or 3 (*EXEC)"; return false; }
        m_boot = opt;
        return writeCatalogue(err);
    }());
}

// -------------------------------------------------------------------- check

Report Disc::check() {
    Report r;
    QString err;
    if (!attached()) { r.errors << "no DFS disc"; return r; }
    if (!load(&err)) { r.errors << err; return r; }
    if (m_lay.sectorsPerSide && m_sectors > m_lay.sectorsPerSide)
        r.errors << QString("the catalogue claims %1 sectors but the image only holds %2 per side").arg(m_sectors).arg(m_lay.sectorsPerSide);
    struct Ext { quint32 s, e; QString n; };
    QVector<Ext> ex;
    for (int i = 0; i < m_files.size(); ++i) {
        const File &f = m_files[i];
        QString id = f.fullName();
        QString why;
        if (!validName(f.name, &why)) r.warnings << QString("file %1 has an unusual name (%2)").arg(id, why);
        if (f.start < 2) r.errors << id + ": starts inside the catalogue";
        if (f.start + f.sectors() > m_sectors) r.errors << id + ": extends beyond the end of the disc";
        if (f.sectors()) ex.append({f.start, f.start + f.sectors(), id});
        for (int j = 0; j < i; ++j)
            if (m_files[j].dir.toUpper() == f.dir.toUpper() && m_files[j].name.compare(f.name, Qt::CaseInsensitive) == 0)
                r.errors << id + ": duplicate file name";
        if (i > 0 && f.start > m_files[i - 1].start) r.warnings << id + ": catalogue is not in descending sector order";
    }
    std::sort(ex.begin(), ex.end(), [](const Ext &a, const Ext &b) { return a.s < b.s; });
    for (int i = 1; i < ex.size(); ++i)
        if (ex[i].s < ex[i - 1].e) r.errors << QString("%1 overlaps %2").arg(ex[i].n, ex[i - 1].n);
    r.info << QString("DFS drive %1%2: %3 files, %4 of %5 sectors free")
                  .arg(m_side).arg(m_title.isEmpty() ? QString() : " '" + m_title + "'").arg(m_files.size()).arg(freeSectors()).arg(m_sectors);
    return r;
}

} // namespace Dfs
