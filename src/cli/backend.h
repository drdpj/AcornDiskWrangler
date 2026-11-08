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
// backend.h - Acorn Disk Wrangler's side of the command-line program (acorndw): how an image is opened, made,
// converted, and how host files become items. The commands themselves are DiskWranglerCore's (diskwrangler/cli). QtCore only.
#pragma once

#include <diskwrangler/cli/cli.h>

namespace AcornCli {

class Backend : public dw::cli::Backend {
public:
    dw::fs::OtherApp self() const override;
    dw::cli::OpenOutcome open(const QString &path, const dw::cli::OpenOptions &o) override;
    QJsonArray formats() const override;
    bool create(const QString &file, const dw::cli::NewOptions &o, QString *err, int *exit) override;
    std::shared_ptr<dw::xfer::HostPolicy> hostPolicy(const dw::cli::GetOptions &o, QString *err, int *exit) override;
    bool readHost(const QStringList &paths, const dw::cli::PutOptions &o, QVector<dw::TransferItem> *items,
                  QStringList *warnings, QString *err, int *exit) override;
    void configureTarget(dw::fs::Volume *v, const dw::cli::ImportOptions &o) override;
};

} // namespace AcornCli
