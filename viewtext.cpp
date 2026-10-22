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
#include "viewtext.h"

namespace ViewText {

static const char *const kTokens[129] = {   // index = byte - 0x7F
    "OTHERWISE",                                                               // 7F
    "AND", "DIV", "EOR", "MOD", "OR", "ERROR", "LINE", "OFF", "STEP", "SPC",   // 80-89
    "TAB(", "ELSE", "THEN", nullptr /*8D line number*/, "OPENIN", "PTR",       // 8A-8F
    "PAGE", "TIME", "LOMEM", "HIMEM", "ABS", "ACS", "ADVAL", "ASC", "ASN", "ATN", // 90-99
    "BGET", "COS", "COUNT", "DEG", "ERL", "ERR", "EVAL", "EXP", "EXT", "FALSE",   // 9A-A3
    "FN", "GET", "INKEY", "INSTR(", "INT", "LEN", "LN", "LOG", "NOT", "OPENUP",   // A4-AD
    "OPENOUT", "PI", "POINT(", "POS", "RAD", "RND", "SGN", "SIN", "SQR", "TAN",   // AE-B7
    "TO", "TRUE", "USR", "VAL", "VPOS", "CHR$", "GET$", "INKEY$", "LEFT$(",       // B8-C0
    "MID$(", "RIGHT$(", "STR$", "STRING$(", "EOF", "AUTO", "DELETE", "LOAD",      // C1-C8
    "LIST", "NEW", "OLD", "RENUMBER", "SAVE", "EDIT", "PTR", "PAGE", "TIME",      // C9-D1
    "LOMEM", "HIMEM", "SOUND", "BPUT", "CALL", "CHAIN", "CLEAR", "CLOSE", "CLG",  // D2-DA
    "CLS", "DATA", "DEF", "DIM", "DRAW", "END", "ENDPROC", "ENVELOPE", "FOR",     // DB-E3
    "GOSUB", "GOTO", "GCOL", "IF", "INPUT", "LET", "LOCAL", "MODE", "MOVE",       // E4-EC
    "NEXT", "ON", "VDU", "PLOT", "PRINT", "PROC", "READ", "REM", "REPEAT",        // ED-F5
    "REPORT", "RESTORE", "RETURN", "RUN", "STOP", "COLOUR", "TRACE", "UNTIL",     // F6-FD
    "WIDTH", "OSCLI"                                                              // FE-FF
};

static QChar shown(quint8 c, bool bbc) {
    if (bbc && c == 0x60) return QChar(0x00A3);          // BBC character set: 0x60 is the pound sign
    if (c >= 0x20 && c < 0x7F) return QChar(c);
    if (c >= 0xA0) return QChar(c);
    return QChar('.');
}

QString plain(const QByteArray &d) {
    QString s;
    s.reserve(d.size());
    for (int i = 0; i < d.size(); ++i) {
        quint8 c = quint8(d[i]);
        if (c == 0x0D) { s += '\n'; if (i + 1 < d.size() && d[i + 1] == 0x0A) ++i; }
        else if (c == 0x0A || c == 0x09) s += QChar(c);
        else if (c == 0x1A && i == d.size() - 1) { /* DOS end-of-file marker */ }
        else s += shown(c, false);
    }
    return s;
}

QString hexDump(const QByteArray &d) {
    QString out;
    out.reserve(d.size() * 4 + 64);
    for (int o = 0; o < d.size(); o += 16) {
        QString hex, asc;
        for (int i = 0; i < 16; ++i) {
            if (o + i < d.size()) {
                quint8 c = quint8(d[o + i]);
                hex += QString("%1 ").arg(c, 2, 16, QChar('0')).toUpper();
                asc += (c >= 0x20 && c < 0x7F) ? QChar(c) : QChar('.');
            } else hex += "   ";
            if (i == 7) hex += ' ';
        }
        out += QString("%1  %2 |%3|\n").arg(o, 8, 16, QChar('0')).toUpper().arg(hex, asc);
    }
    return out;
}

bool looksLikeText(const QByteArray &d) {
    if (d.isEmpty()) return true;
    int n = qMin(d.size(), 4096), good = 0;
    for (int i = 0; i < n; ++i) {
        quint8 c = quint8(d[i]);
        if (c == 0) return false;
        if ((c >= 0x20 && c < 0x7F) || c == 0x0D || c == 0x0A || c == 0x09 || (c == 0x1A && i == d.size() - 1)) ++good;
    }
    return good * 100 >= n * 95;
}

// Walks the line structure; returns the offset just past the terminator or -1.
static int walkBasic(const QByteArray &d, QString *err) {
    int i = 0, n = d.size(), lastNo = -1;
    while (true) {
        if (i >= n || quint8(d[i]) != 0x0D) { if (err) *err = QString("expected a line start (&0D) at offset %1").arg(i); return -1; }
        if (i + 1 >= n) { if (err) *err = "program has no end marker"; return -1; }
        if (quint8(d[i + 1]) == 0xFF) return i + 2;
        if (i + 4 > n) { if (err) *err = "truncated line header"; return -1; }
        int no = (quint8(d[i + 1]) << 8) | quint8(d[i + 2]), len = quint8(d[i + 3]);
        if (len < 4 || i + len > n) { if (err) *err = QString("bad line length %1 at offset %2").arg(len).arg(i); return -1; }
        if (no <= lastNo) { if (err) *err = QString("line numbers out of order at line %1").arg(no); return -1; }
        lastNo = no;
        i += len;
    }
}

bool looksLikeBasic(const QByteArray &d) { return d.size() >= 3 && walkBasic(d, nullptr) >= 0; }

bool detokenise(const QByteArray &d, QString *out, QString *err) {
    out->clear();
    int i = 0, n = d.size();
    while (true) {
        if (i >= n || quint8(d[i]) != 0x0D) { *err = QString("expected a line start (&0D) at offset %1").arg(i); return false; }
        if (i + 1 >= n) { *err = "program has no end marker (&0D &FF)"; return false; }
        if (quint8(d[i + 1]) == 0xFF) return true;
        if (i + 4 > n) { *err = "truncated line header"; return false; }
        int no = (quint8(d[i + 1]) << 8) | quint8(d[i + 2]), len = quint8(d[i + 3]);
        if (len < 4 || i + len > n) { *err = QString("bad line length %1 at offset %2 (line %3)").arg(len).arg(i).arg(no); return false; }
        QString line = QString("%1 ").arg(no, 5);
        bool inStr = false;
        int j = i + 4, end = i + len;
        while (j < end) {
            quint8 c = quint8(d[j++]);
            if (inStr) { line += shown(c, true); if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; line += '"'; continue; }
            if (c == 0x8D) {                                  // encoded line number (GOTO 100 etc.)
                if (j + 3 > end) { line += "<?>"; break; }
                quint8 b1 = quint8(d[j]) ^ 0x54, b2 = quint8(d[j + 1]), b3 = quint8(d[j + 2]);
                int lo = (b2 & 0x3F) | ((b1 << 2) & 0xC0), hi = (b3 & 0x3F) | ((b1 << 4) & 0xC0);
                line += QString::number((hi << 8) | lo);
                j += 3;
                continue;
            }
            if (c == 0xF4) {                                  // REM: rest of the line is literal
                line += "REM";
                while (j < end) line += shown(quint8(d[j++]), true);
                break;
            }
            if (c >= 0x7F && kTokens[c - 0x7F]) line += kTokens[c - 0x7F];
            else line += shown(c, true);
        }
        *out += line + '\n';
        i += len;
    }
}

} // namespace ViewText
