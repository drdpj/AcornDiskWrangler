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
//   - The flag: bit 7 the last block of the file, bit 0 protected (*RUN and CHAIN only: *LOAD says "Locked"). Bit 6
//     ("empty block" in later documents) is neither set nor read by MOS 1.20.
//   - A file is a run of blocks with the same name numbered from 0, ending at the last-block flag. Block n is loaded at
//     load + 256 x n, so every block but the last holds 256 bytes; only the length's low byte counts its data (0
//     with a non-zero high byte is 256).
//   - The MOS reads 8N1 data at 1200 baud (or 300, *TAPE3). Data framed any other way (a protected game's own
//     loader) can't be cassette filing system blocks.
//
// A tape is read into Items in tape order: files, and the data no block took ("unrecognised data", kept as it is).
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

} // namespace Cfs
} // namespace Acorn
