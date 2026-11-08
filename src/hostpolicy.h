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
// hostpolicy.h - how this program's items become host files, and host files become items (QtCore only: the
// command-line program uses it too). The .inf sidecar reader, the policy Extract to host and drag-out write with, and
// readHostItems, which reads host files and folders the way Add files reads them.
#pragma once

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <diskwrangler/xfer/retrofiles.h>
#include <diskwrangler/xfer/transferitem.h>

#include "adfs.h"

// .inf sidecar: NAME LOAD EXEC [LENGTH] [ATTR|L]; NAME may be "D.NAME" (DFS directory letter). Read by
// DiskWranglerCore's DFS module (dw::dfs::readInf); this is it with the access as Adfs::Flag bits.
struct InfData {
    QString name;
    QChar dir;
    quint32 load = 0, exec = 0;
    quint8 flags = Adfs::FlagR | Adfs::FlagW;
};
bool readInf(const QString &hostPath, InfData *out);

namespace RetroFiles {

bool parseHex(const QJsonValue &v, quint32 *out);
QString hex8(quint32 v);

// DFS files named NAME or D.NAME, FAT names keep their extension apart, and (when asked) an Acorn .inf sidecar
// beside every ADFS and DFS file, exactly as Extract to host writes it.
class AcornPolicy : public dw::xfer::HostPolicy {
public:
    explicit AcornPolicy(bool writeInf) : m_inf(writeInf) {}
    QString hostName(const dw::TransferItem &it) const override;
    bool keepExtension(const dw::TransferItem &it) const override { return it.sourceFs == "fat"; }
    void afterFile(const dw::TransferItem &it, const QString &absDir, const QString &relDir, const QString &host,
                   dw::xfer::UsedNames &used, QStringList *topLevel, QStringList *errors) const override;
private:
    bool m_inf;
};

} // namespace RetroFiles
