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
// adfs.h - Acorn ADFS "old map" (S/M/L, "Hugo" directory) hard disc image access.
//
// Layout (256-byte sectors, as used by BBC/Master ADFS hard drives, BeebSCSI scsi0.dat etc.):
//   sector 0     free space map part 1: 82 x 24-bit free-run start addresses,
//                0xF6-0xFA disc name (even chars), 0xFC-0xFE total sectors, 0xFF checksum
//   sector 1     free space map part 2: 82 x 24-bit free-run lengths,
//                0xF6-0xFA disc name (odd chars), 0xFB-0xFC disc id, 0xFD boot option,
//                0xFE end-of-list pointer (3 * number of runs), 0xFF checksum
//   sectors 2-6  root directory ("$"), 1280 bytes: Hugo format, 47 x 26-byte entries
//
// All modifications are STAGED in memory; nothing touches the image file until
// commit(), which re-validates the whole free space map + directory tree first.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace Adfs {

const int SectorSize = 256;
const int DirSectors = 5;
const int DirBytes = 1280;
const int MaxDirEntries = 47;
const int MaxFreeRuns = 82;
const quint32 RootSector = 2;
const quint32 FirstDataSector = 7;   // 0-1 map, 2-6 root directory
const int MaxNameLen = 10;

// Attribute flags used by the Image API
enum Flag { FlagR = 1, FlagW = 2, FlagL = 4, FlagE = 8 };

inline quint32 sectorsFor(quint32 bytes) { return (bytes + SectorSize - 1) / SectorSize; }

bool isValidNameChar(QChar c);
bool isValidName(const QString &name, QString *why = nullptr);
QString sanitizeName(const QString &hostName);
quint8 bcdInc(quint8 v);
quint8 mapChecksum(const char *sector256);

// One 26-byte directory entry
struct Entry {
    // Indices into nameRaw whose bit 7 carries an attribute
    enum { R = 0, W = 1, L = 2, D = 3, E = 4 };

    QByteArray nameRaw = QByteArray(10, 0x0d);   // 10 bytes, bit 7 of bytes 0-4 = R W L D E
    quint32 load = 0, exec = 0, length = 0, sin = 0;
    quint8 seq = 0;

    QString name() const;
    void setName(const QString &n);               // keeps attribute bits
    bool flag(int i) const { return (quint8(nameRaw[i]) & 0x80) != 0; }
    void setFlag(int i, bool on);
    bool isDir() const { return flag(D); }
    QString attrString() const;                   // e.g. "DLWR"
    bool hasStamp() const { return (load & 0xFFF00000u) == 0xFFF00000u; }
    quint32 fileType() const { return (load >> 8) & 0xFFF; }
    QDateTime stamp() const;
};

// A 1280-byte directory
struct Dir {
    quint32 sector = 0;
    quint8 seq = 0;                               // master sequence number (BCD)
    QVector<Entry> entries;
    QByteArray tail = QByteArray(53, 0);          // bytes 0x4CB..0x4FF (name, parent, title, ...)

    QString title() const;
    quint32 parent() const;
    int indexOf(const QString &name) const;       // case-insensitive
    void sortEntries();
    QByteArray serialize() const;
    static bool parse(quint32 sector, const QByteArray &raw, Dir *out, QString *err);
};

// Free space map
struct FsMap {
    struct Run { quint32 start; quint32 len; };
    QVector<Run> runs;
    quint32 total = 0;
    QByteArray base;                              // the 512 raw bytes (preserves name/id/boot)

    // Parses and checks. Returns true when there are no errors.
    static bool parse(const QByteArray &s01, FsMap *out, QStringList *errors, QStringList *warnings);
    QByteArray serialize() const;
    quint32 freeSectors() const;
    quint32 largestRun() const;
    bool allocate(quint32 n, quint32 *start);     // best fit
    bool release(quint32 start, quint32 n, QString *err);
};

struct Report {
    QStringList errors, warnings, info;
    bool ok() const { return errors.isEmpty(); }
};

// A blank old-map ADFS image of totalSectors sectors (640 = S, 1280 = M, 2560 = L, or any size).
// With allFree == false the free space map lists nothing free ("disc full"), as a hybrid disc needs.
QByteArray makeBlankImage(quint32 totalSectors, const QString &title, bool allFree = true);

class Image {
public:
    struct Extent { quint32 start; QByteArray data; };
    struct Snapshot { FsMap map; QList<Extent> pending; };

    // forceFullSize (if non-zero) fixes the disc size instead of trusting the ADFS map, and tolerates a map that
    // claims more sectors than the file holds (Acorn DOS Plus hybrid discs do exactly that).
    bool open(const QString &path, QString *err, quint64 forceFullSize = 0);
    // A non-ADFS container (DFS, PC FAT floppy, Acorn DOS disc): no ADFS map is read or checked.
    // The image is treated as fullSize bytes long even if the file is shorter.
    bool openRaw(const QString &path, quint64 fullSize, QString *err);
    bool isAdfs() const { return m_isAdfs; }
    quint64 fullSize() const { return m_fullSize; }
    quint64 fileSize() const { return quint64(m_file.size()); }
    // True if the file on disk is shorter than the disc it describes; saving pads it out with zeros.
    bool needsPadding() const { return m_file.isOpen() && quint64(m_file.size()) < m_fullSize; }
    // Hybrid discs: make the ADFS side read-only while the DOS side stays editable.
    void setAdfsLocked(bool on) { m_adfsLocked = on; }
    bool adfsLocked() const { return m_adfsLocked; }
    void close();
    bool isOpen() const { return m_file.isOpen(); }
    bool editable() const { return m_editable; }
    bool dirty() const { return !m_pending.isEmpty(); }
    QString path() const { return m_path; }
    const Report &openReport() const { return m_openReport; }

    quint32 totalSectors() const { return m_map.total; }
    quint32 freeSectors() const { return m_map.freeSectors(); }
    quint32 largestFreeRun() const { return m_map.largestRun(); }

    // Full check of the (staged) free space map and directory tree.
    Report validate() const;
    // Validates, then writes. Writes data first, directories next, free space map last.
    bool commit(QString *err, bool makeBackup, Report *postReport);
    void revert();

    // Byte-range access inside a file that occupies [sin, sin+ceil(fileLen/256)) (used for the DOS partition).
    // Reads see staged changes; writes are staged like everything else.
    bool readFileBytes(quint32 sin, quint32 fileLen, quint64 off, quint32 len, QByteArray *out, QString *err) const;
    bool writeFileBytes(quint32 sin, quint32 fileLen, quint64 off, const QByteArray &data, QString *err);
    bool pendingTouches(quint32 startSector, quint32 count) const;
    // Extra checks run by commit() before anything is written; non-empty result vetoes the save.
    void setExtraValidator(std::function<QStringList()> f) { m_extra = f; }
    Snapshot snapshot() const { return Snapshot{m_map, m_pending}; }
    void restore(const Snapshot &s) { m_map = s.map; m_pending = s.pending; }

    bool readDir(quint32 sector, Dir *d, QString *err) const;
    bool readFile(const Entry &e, QByteArray *out, QString *err) const;

    bool addFile(quint32 parent, const QString &name, const QByteArray &data, quint32 load,
                 quint32 exec, quint8 flags, bool replaceExisting, QString *err);
    bool makeDir(quint32 parent, const QString &name, QString *err, quint32 *newSector = nullptr);
    bool deleteEntry(quint32 parent, const QString &name, QString *err);
    // Sets the disc name: the root directory title (max 10 printable characters; the title field is 19 bytes but only 10 is known to be safe). If the free space
    // map already carries a disc name, that is kept in step (first 10 characters).
    bool setDiscTitle(const QString &title, QString *err);
    // Replaces name/attributes/load/exec of an entry (length, sector and seq are kept).
    bool replaceEntry(quint32 parent, const QString &oldName, const Entry &updated, QString *err);

private:
    QByteArray readRaw(quint32 start, quint32 count) const;
    QByteArray readSectors(quint32 start, quint32 count) const;
    void writeSectors(quint32 start, QByteArray data);
    void stageFsMap();
    bool writeDir(const Dir &d);
    bool freeRun(quint32 start, quint32 n, QString *err);
    bool freeTree(quint32 dirSector, int depth, QString *err);
    bool checkEditable(QString *err) const;
    bool checkAdfs(QString *err) const;
    bool addFileInner(quint32 parent, const QString &name, const QByteArray &data, quint32 load,
                      quint32 exec, quint8 flags, bool replace, QString *err);
    bool deleteInner(quint32 parent, const QString &name, QString *err);

    mutable QFile m_file;
    bool m_isAdfs = true;
    bool m_adfsLocked = false;
    bool m_mapSizeMayExceed = false;   // set for hybrid discs: the ADFS map does not describe the whole disc
    quint64 m_fullSize = 0;
    QString m_path;
    bool m_editable = false;
    FsMap m_map;
    QByteArray m_orig;            // sectors 0-1 as they were on disk when opened / last saved
    QList<Extent> m_pending;
    Report m_openReport;
    std::function<QStringList()> m_extra;
};

} // namespace Adfs
