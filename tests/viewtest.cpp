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
#include <cstdio>
#include <QByteArray>
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)
int main(){
    // 10 PRINT "HELLO" / 20 GOTO 10 / 30 REM it's £ ok / 40 IF A>1 THEN PRINT "X" ELSE END
    QByteArray p = QByteArray::fromHex("0D000A0DF1202248454C4C4F22");
    p += QByteArray::fromHex("0D0014 0A E5 20 8D 54 4A 40");           // 0A = length 10
    p += QByteArray::fromHex("0D001E 0F F4 20 69 74 27 73 20 60 20 6F 6B");
    p += QByteArray::fromHex("0D0028 15 E7 20 41 3E 31 20 8C 20 F1 20 22 58 22 20 8B 20 E0");
    p += QByteArray::fromHex("0DFF");
    CHECK(ViewText::looksLikeBasic(p));
    QString out, err; CHECK(ViewText::detokenise(p,&out,&err));
    printf("%s",qPrintable(out));
    CHECK(out.contains("   10 PRINT \"HELLO\"\n"));
    CHECK(out.contains("   20 GOTO 10\n"));
    CHECK(out.contains(QString::fromUtf8("   30 REM it's \xC2\xA3 ok\n")));
    CHECK(out.contains("   40 IF A>1 THEN PRINT \"X\" ELSE END\n"));
    // line number encoding round trip for several values
    for (int n : {0,1,10,255,256,1000,32767,65535}) {
        quint8 lo=n&255, hi=n>>8;
        quint8 b1=(((lo&0xC0)>>2)|((hi&0xC0)>>4))^0x54, b2=(lo&0x3F)|0x40, b3=(hi&0x3F)|0x40;
        QByteArray q; q.append(char(0x0D)); q.append(char(0)); q.append(char(1)); q.append(char(10)); q.append(char(0xE5)); q.append(char(0x20)); q.append(char(0x8D)); q.append(char(b1)); q.append(char(b2)); q.append(char(b3)); q.append(char(0x0D)); q.append(char(0xFF));
        QString o,e; CHECK(ViewText::detokenise(q,&o,&e)); CHECK(o.contains(QString("GOTO %1\n").arg(n)));
    }
    // token inside a string must not expand
    QByteArray s = QByteArray::fromHex("0D000A08F122E5220DFF");
    QString o2,e2; CHECK(ViewText::detokenise(s,&o2,&e2)); CHECK(o2.contains("PRINT\"")); CHECK(!o2.contains("GOTO"));
    // invalid
    CHECK(!ViewText::looksLikeBasic("Hello world\r\n")); 
    CHECK(!ViewText::looksLikeBasic(p.left(p.size()-2)));
    CHECK(ViewText::looksLikeText("Hello\r\nworld\r\n")); CHECK(!ViewText::looksLikeText(QByteArray("\x00\x01\x02\x03",4)));
    CHECK(ViewText::plain("a\r\nb\rc\nd")=="a\nb\nc\nd");
    printf("%s",qPrintable(ViewText::hexDump("Hello, hex dump!\x01\xFF")));
    printf("%s (%d failures)\n",fails?"FAILED":"ALL OK",fails); return fails?1:0;
}
