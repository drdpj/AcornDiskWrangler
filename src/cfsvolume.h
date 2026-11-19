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
// cfsvolume.h - a cassette tape (a UEF file, dw::tape::TapeFile) as a DiskWranglerCore dw::fs::Volume: its files in
// tape order (Acorn::Cfs). QtCore only. Since series cfs-2 it changes the tape as the BBC's MOS would have written it:
// a file added goes at the end, saved as *SAVE saves (Cfs::saveChunks); a file replaced, renamed or given new
// attributes is written again where it is, keeping its lead and trailer; a file removed or moved takes its own chunks
// (Cfs::Edit) and leaves every other file's chunks as they were.
//
// Two files on a tape may share a name (a BBC and an Electron version, a loader's parts). Each entry is named
// uniquely - the first GAME is "GAME", the second "GAME (2)" - and labelled with its real name, which the window, ls
// and a copy show (Entry::label). A path is one name: "GAME", "GAME (2)", or "#3", the third file on the tape
// (parsePath); a bare name that is only another file's real name finds the first file of that name. '/' is a name
// character ("M/C"). The data the cassette filing system can't read is an entry of its own, "unrecognised data
// (chunks n-m)", read-only, its bytes as they are; so is a file with a problem (a bad CRC, a missing block). Both move
// with the others (move()) but are never changed or removed, and a copy to another tape takes their chunks as they
// are (rawChunks).
//
// Attributes: load and exec (retro-files "acorn"), protected (flag bit 0: *RUN only; it travels as access "E",
// execute only, and a copy onto a disc drops it; an item whose access is E without R arrives protected), and the
// number of blocks; position (the file's number on the tape, what "#n" takes) for the command line's JSON.
#pragma once

#include <QVector>

#include <diskwrangler/fs/volume.h>
#include <diskwrangler/tape/tapefile.h>

#include "cfs.h"

namespace Acorn {

class CfsNameRules : public dw::fs::NameRules {
public:
    // 1-10 characters of Latin-1, none of them &00 (the MOS ends a name there) or a control character.
    bool valid(const QString &name, dw::fs::NodeKind kind, QString *why) const override;
    QString fromHost(const QString &hostName, dw::fs::NodeKind kind) const override;
};

class CfsVolume : public dw::fs::Volume {
public:
    explicit CfsVolume(dw::tape::TapeFile *tape);          // the tape must outlive the volume
    dw::tape::TapeFile *tape() const { return m_tape; }
    const Cfs::Tape &tapeRead() const { return m_read; }

    QString fsId() const override { return "cfs"; }
    dw::fs::Caps caps() const override;
    QVector<dw::fs::AttrSpec> fileAttrs() const override;
    const dw::fs::NameRules &names() const override { return m_rules; }
    dw::fs::VolumeInfo info() const override;

    bool parsePath(const QString &text, dw::fs::Path *out, QString *err) const override;
    QString displayPath(const dw::fs::Path &p) const override;

    bool list(const dw::fs::Path &dir, QVector<dw::fs::Entry> *out, QString *err) const override;
    bool stat(const dw::fs::Path &node, dw::fs::Entry *out, QString *err) const override;
    bool read(const dw::fs::Path &file, QByteArray *out, QString *err) const override;
    bool write(const dw::fs::Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs,
               const QDateTime &when, bool replace, QString *err) override;
    bool remove(const dw::fs::Path &node, QString *err) override;
    // A new name for a file (two files may share a name on a tape, as the MOS allows).
    bool rename(const dw::fs::Path &from, const dw::fs::Path &to, QString *err) override;
    bool setAttrs(const dw::fs::Path &node, const dw::fs::AttrValues &a, QString *err) override;
    // Files (or unrecognised data) moved, in their tape order, to just before the entry `before` (empty: to the end of
    // the tape). Like the other changes, only in a Disc::transact.
    bool move(const QVector<dw::fs::Path> &nodes, const dw::fs::Path &before, QString *err);
    dw::fs::Report check() const override;
    bool reload(QString *err) override;

    // retro-files "acorn": load, exec, and access "E" for a protected file (execute only, as the format says of
    // CFS), "WR" for any other
    QMap<QString, QJsonObject> toRetro(const dw::fs::Entry &e) const override;
    dw::fs::AttrValues fromRetro(const QMap<QString, QJsonObject> &families, dw::fs::NodeKind kind) const override;
    // A DFS file in $ comes without its letter ("$.GAME" -> "GAME"); any other letter stays ("A.GAME", as *SAVE
    // "A.GAME" names it on a tape); the rest by the name rules (at most 10 characters). A tape may hold any number of
    // files of one name, so a name already there is only a clash as sameName() says: by default only when the item is
    // identical to a file there (identicalTo), which it then names; otherwise the item is placed under the unique name
    // it will have once added ("GAME (2)"), and write() gives it its real name.
    dw::fs::Placement placeIncoming(const dw::TransferItem &item, const dw::fs::Path &targetDir) const override;

    // What a file arriving under a name already on the tape is.
    enum class SameName {
        AddUnlessIdentical,              // added at the end, unless it is identical to a file there: a clash with that
                                         // file (acorndw without --replace)
        Add,                             // always added at the end (the window, which asks about identical files itself)
        Replace                          // a clash with the first file of that name (acorndw --replace)
    };
    void setSameName(SameName s) { m_sameName = s; }
    SameName sameName() const { return m_sameName; }
    // The file on the tape this item would duplicate (the same name, load and exec where the item gives them, and the
    // same bytes; the protected flag isn't compared), as an item index, or -1. Unrecognised data and damaged files
    // (which travel as chunks) are never a duplicate.
    int identicalTo(const dw::TransferItem &item) const;
    // The unique name this item's file has when it is added at the end of the tape.
    QString nameWhenAdded(const QString &realName) const;

    // The item an entry of the root names (by its unique name), or -1.
    int itemIndex(const QString &name) const;
    static QString unrecognisedLabel(const Cfs::Item &it);
    // Unrecognised data and a damaged file travel with their chunks (retro-files namespace "uef", key "chunks": UEF
    // chunk records in base64, with the base frequency and baud rate they were played at in front). Dropped on a tape,
    // they are added at the end as they are; anywhere else they arrive as a file of their bytes (a damaged file with
    // its load and exec).
    static QByteArray rawChunks(const QMap<QString, QJsonObject> &families);

private:
    dw::fs::Entry entry(int i) const;
    bool find(const dw::fs::Path &p, int *index, QString *err) const;
    bool changeable(int i, QString *err) const;          // a file with no problem
    // The item's blocks again with a new name, addresses or protected flag, each block keeping its other flag bits
    // and its spare bytes.
    QVector<QByteArray> reblock(int i, const QByteArray &name, quint32 load, quint32 exec, bool prot) const;
    bool apply(const QVector<dw::tape::Chunk> &chunks, QString *err);
    dw::tape::TapeFile *m_tape;
    Cfs::Tape m_read;
    QStringList m_names;                                    // each item's unique name
    CfsNameRules m_rules;
    SameName m_sameName = SameName::AddUnlessIdentical;
    // The last placement of a new file under a name already on the tape: the unique name placeIncoming gave it, and
    // the real name write() gives the file (the Importer places each item, then writes it).
    mutable QString m_placedAs, m_placedReal;
    QString incomingName(const dw::TransferItem &item, QString *warning) const;
};

} // namespace Acorn
