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
// cfs.h - the BBC Micro's cassette filing system: the files on a tape image, read from the data the core's tape module
// finds in it (dw::tape::pieces). QtCore only. Every rule here is what MOS 1.20 does when it reads a tape
// (docs/disc-formats.md, "Tapes", with the ROM addresses):
//
//   - A block starts with the sync byte &2A, the first byte after carrier tone (anything else and the MOS waits for
//     carrier again). Then the name (1-10 characters and &00; an 11th byte is read in the &00's place), load (4),
//     exec (4), block number (2), data length (2), flag (1), 4 spare bytes, the header's CRC (2, high byte first);
//     then, if the length isn't 0, the data and its CRC. Numbers are little-endian. The CRC is CRC-16 with polynomial
//     &1021, starting at 0, over the name, its &00 and the 17 bytes after it (and over the data): a block read with
//     its CRC leaves 0.
//   - The flag: bit 7 the last block of the file, bit 0 protected (*RUN only: *LOAD and CHAIN say "Locked"). Bit 6
//     ("empty block" in later documents) is neither set nor read by MOS 1.20.
//   - A file is a run of blocks with the same name numbered from 0, ending at the last-block flag. Block n is loaded at
//     load + 256 x n, so every block but the last holds 256 bytes; only the length's low byte counts its data (0
//     with a non-zero high byte is 256).
//   - The MOS reads 8N1 data at 1200 baud (or 300, *TAPE3). Data framed any other way (a protected game's own
//     loader) can't be cassette filing system blocks.
//
// A tape is read into Items in tape order: files, and the data no block took ("unrecognised data", kept as it is).
//
// Writing (series cfs-2) is as MOS 1.20's *SAVE does it (&F7EC-&F874, &FBE2): the dummy byte &AA, then 5.1 s of
// carrier, the blocks with 0.9 s of carrier between them, 5.1 s after the last, and the motor stops. Edit changes a
// tape's chunks for a file added, replaced, changed, removed or moved, touching no other file's chunks.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <diskwrangler/tape/records.h>

namespace Acorn {
namespace Cfs {

// MOS 1.20's CRC (&F7B0): CRC-16, polynomial &1021, from start, most significant bit first.
quint16 crc(const QByteArray &bytes, quint16 start = 0);

struct Block {
    QByteArray name;                 // as on the tape, at most 10 bytes, without its &00
    quint32 load = 0, exec = 0;
    quint16 number = 0, length = 0;  // length as the header gives it
    quint8 flag = 0;
    QByteArray spare;                // the 4 spare bytes
    QByteArray data;
    bool headerOk = true;            // the header's CRC agrees
    bool dataOk = true;              // the data's CRC agrees (true when there is no data)
    bool cut = false;                // the tape ends (or its data stops) inside the block's data
    int first = 0, last = 0;         // the chunks it came from
    bool isLast() const { return flag & 0x80; }
};

struct File {
    QByteArray name;
    quint32 load = 0, exec = 0;      // from its first block
    quint8 flag = 0;                 // its first block's flag
    QByteArray data;                 // every block's data in order
    int blocks = 0;
    int first = 0, last = 0;         // the chunks it came from
    QStringList problems;            // a bad CRC, a missing block, ... (such a file is read-only)
    int firstBlock = 0;              // its first block in Tape::blocks (the others follow it)
    bool isProtected() const { return flag & 0x01; }
};

struct Item {
    enum Type { FileItem, Unrecognised };
    Type type = FileItem;
    File file;                       // FileItem
    int first = 0, last = 0;         // the chunks: a file's, or the unrecognised data's
    int bytes = 0;                   // Unrecognised: how many bytes no block took
};

struct Tape {
    QVector<Item> items;             // in tape order
    QVector<Block> blocks;           // every block found, in tape order
    int files() const;
    int goodBlocks() const;          // blocks whose header CRC agrees
};

// The blocks, files and unrecognised data of a tape's pieces (dw::tape::pieces of a UEF).
Tape read(const QVector<dw::tape::Piece> &pieces);

// The name as text: one character per byte (Latin-1), as Acorn names travel (docs/retro-files-format.md).
QString nameText(const QByteArray &name);
QByteArray nameBytes(const QString &name);

// ----- writing, as MOS 1.20 saves (docs/disc-formats.md, "Tapes"). Carrier is counted in cycles of 2400 Hz.
namespace Mos {
const int LeaderCycles = 12240;      // before a file's first block: 5.1 s (&F7FF: 5 waits of 51 frames)
const int BlockGapCycles = 2160;     // between blocks: 0.9 s (&F85E 0.2 s, then &F804 5 waits of &C7 + 1 frames; *SAVE
                                     // sets &C7 to 6, &FB2D)
const int TrailerCycles = 12720;     // after the last block: 5.3 s (&F85E 0.2 s, &F86D 5.1 s); then the motor stops
const int DummyLeadCycles = 2400;    // before the dummy byte: the motor runs from "RECORD then RETURN" until RETURN
                                     // sends &AA (&FBE2); how long is the user's, 1 s here
const double StopSeconds = 1.0;      // the silence before a file saved after others (the motor stopped in between)
}

// One block: &2A, the name and &00, load, exec, block number, data length, flag, 4 spare bytes (0, as *SAVE leaves
// them, &F7ED, unless given), the header's CRC (high byte first); then the data and its CRC, if there is data.
QByteArray encodeBlock(const QByteArray &name, quint32 load, quint32 exec, int number, const QByteArray &data, quint8 flag,
                       const QByteArray &spare = QByteArray());
// A file's blocks, as *SAVE makes them (&F2C8-&F2F0): 256 bytes each but the last, which has what is left (an empty
// file: one block with none) and flag &80; every block has the protected bit if protect.
QVector<QByteArray> encodeFile(const QByteArray &name, quint32 load, quint32 exec, const QByteArray &data, bool protect);
// The chunks of one *SAVE: the dummy byte (&0111: carrier, &AA, the 5.1 s leader), the blocks (&0100) with carrier
// (&0110) between them, and the trailer; with silence first (&0116) if afterOthers.
QVector<dw::tape::Chunk> saveChunks(const QVector<QByteArray> &blocks, bool afterOthers);

// Changes to a tape's chunks. Each item owns the chunks from just after the item before it to its own last chunk: its
// lead, its dummy byte, and the trailer and silence of the file before it (the first item's start after the
// information chunks the tape opens with, such as its origin). What follows the last item stays at the end. The base
// frequency (&0113) and baud rate (&0117) every chunk was played at are kept, wherever the chunks go: where a run of
// chunks lands among other settings, the settings it needs are put in front of it. Each function gives the tape's new
// chunks, or false with why.
class Edit {
public:
    Edit(const QVector<dw::tape::Chunk> &chunks, const Tape &tape);
    bool append(const QVector<QByteArray> &blocks, QVector<dw::tape::Chunk> *out, QString *why) const;
    // The item's blocks replaced (a file changed): with as many blocks as it had, each new one in place of the old one's
    // chunks and the carrier between them kept; otherwise the file's chunks from its first block to its last replaced
    // with the new blocks and the MOS's carrier between them. Its lead and trailer are kept either way.
    bool replace(int item, const QVector<QByteArray> &blocks, QVector<dw::tape::Chunk> *out, QString *why) const;
    bool remove(int item, QVector<dw::tape::Chunk> *out, QString *why) const;
    // The items (in their tape order) moved to just before item `before` (items.size(): after the last; an item being
    // moved: the first one after it that isn't). A file whose chunks have no carrier before its first block (one that
    // opened the tape) gets the MOS's leader in front of it.
    bool move(const QVector<int> &items, int before, QVector<dw::tape::Chunk> *out, QString *why) const;

    struct Span { int first, last; };
    const QVector<Span> &spans() const { return m_spans; }

private:
    struct Seg {                         // a run of the tape's chunks [first, last], or new chunks
        int first = 0, last = -1;
        bool fresh = false;
        QVector<dw::tape::Chunk> chunks;
    };
    static Seg old(int first, int last) { Seg s; s.first = first; s.last = last; return s; }
    static Seg made(const QVector<dw::tape::Chunk> &c) { Seg s; s.fresh = true; s.chunks = c; return s; }
    QVector<dw::tape::Chunk> join(const QVector<Seg> &segs) const;
    bool usable(QString *why) const;
    QVector<dw::tape::Chunk> m_chunks;
    Tape m_tape;
    QVector<dw::tape::Piece> m_pieces;
    QVector<Span> m_spans;
    int m_lead = 0;                      // the first chunk that isn't information
    bool m_ok = true;
};

} // namespace Cfs
} // namespace Acorn
