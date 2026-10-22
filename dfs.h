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
// dfs.h - Acorn DFS (single density, 256-byte sectors, 10 per track) discs inside an Adfs::Image.
// One Disc object handles one side: a double-sided image gives two independent discs
// (side 0 = drive 0, side 1 = drive 2), each with its own catalogue.
#pragma once

#include <QChar>
#include <QString>
#include <QVector>

#include "adfs.h"

namespace Dfs {

const int SectorSize = 256;
const int SectorsPerTrack = 10;
const int MaxFiles = 31;
const int MaxName = 7;
const int MaxTitle = 12;

struct File {
    QString name;            // up to 7 characters
    QChar dir = '$';         // single-letter "directory"
    bool locked = false;
    quint32 load = 0, exec = 0, length = 0, start = 0;
    quint32 sectors() const { return (length + SectorSize - 1) / SectorSize; }
    QString fullName() const { return QString(dir) + "." + name; }
};

// How the sides are laid out in the image file.
struct Layout {
    bool doubleSided = false;
    bool interleaved = true;          // DSD: track 0 side 0, track 0 side 1, track 1 side 0 ...
    quint32 sectorsPerSide = 0;       // physical capacity of one side (tracks * 10)
};

bool validNameChar(QChar c);
bool validName(const QString &name, QString *why);
bool validDir(QChar c);
QString sanitizeName(const QString &hostName);
// Catalogue sanity check used for format detection (sectors = disc size the catalogue claims).
bool plausibleCatalogue(const QByteArray &sector0, const QByteArray &sector1, quint32 *sectors = nullptr);

class Disc {
public:
    bool attach(Adfs::Image *img, int side, const Layout &lay, QString *err);
    void detach() { m_img = nullptr; }
    bool attached() const { return m_img != nullptr; }
    int side() const { return m_side; }

    QString title() const { return m_title; }
    int bootOption() const { return m_boot; }
    quint32 sectors() const { return m_sectors; }       // disc size according to the catalogue
    quint32 freeSectors() const;
    quint32 largestFree() const;
    const QVector<File> &files() const { return m_files; }
    int indexOf(QChar dir, const QString &name) const;

    bool readFile(const File &f, QByteArray *out, QString *err) const;
    bool addFile(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec,
                 bool locked, bool replace, QString *err);
    bool remove(QChar dir, const QString &name, QString *err);
    bool modify(QChar oldDir, const QString &oldName, QChar newDir, const QString &newName, bool locked,
                quint32 load, quint32 exec, QString *err);
    bool setTitle(const QString &title, QString *err);

    Adfs::Report check();     // reads the staged state

private:
    quint32 phys(quint32 n) const;
    bool rd(quint32 n, quint32 count, QByteArray *out, QString *err) const;
    bool wr(quint32 n, const QByteArray &data, QString *err);
    bool load(QString *err);
    bool writeCatalogue(QString *err);
    bool findGap(quint32 n, quint32 *start) const;
    bool addInner(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec, bool locked,
                  bool replace, QString *err);

    Adfs::Image *m_img = nullptr;
    int m_side = 0;
    Layout m_lay;
    quint32 m_len = 0;
    QString m_title;
    int m_boot = 0;
    quint8 m_cycle = 0;
    quint32 m_sectors = 0;
    QVector<File> m_files;
};

} // namespace Dfs
