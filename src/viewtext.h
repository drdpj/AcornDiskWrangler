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
// viewtext.h - turn file bytes into readable text: plain text, hex dump, BBC BASIC listing.
#pragma once
#include <QByteArray>
#include <QString>

namespace ViewText {

// CR / LF / CRLF normalised to newlines; other control characters shown as '.'.
QString plain(const QByteArray &d);
// "00000000  41 42 ...  |AB..|" 16 bytes per line.
QString hexDump(const QByteArray &d);
// Heuristic: mostly printable, no NULs.
bool looksLikeText(const QByteArray &d);
// True if d is a structurally valid tokenised BBC BASIC (6502 BASIC II/IV) program.
bool looksLikeBasic(const QByteArray &d);
// Detokenise. On failure returns false with err set; *out still holds everything decoded so far.
bool detokenise(const QByteArray &d, QString *out, QString *err);

} // namespace ViewText
