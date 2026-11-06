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
#pragma once
#include <QByteArray>
#include <diskwrangler/ui/viewerdialog.h>

// Read-only window showing a file as text, a hex dump, or a detokenised BBC BASIC listing.
// Text is selectable (Ctrl+C), and there are Copy all / Save as text buttons. The window itself is
// DiskWranglerCore's dw::ViewerDialog; this class gives it Acorn's views (the core's Text and Hex dump, and BBC BASIC,
// which is registered here) and the Mode names the tests use.
class ViewerDialog : public dw::ViewerDialog {
    Q_OBJECT
public:
    enum Mode { Text = 0, Hex = 1, Basic = 2 };
    ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent = nullptr);
    Mode mode() const { return Mode(viewIndex()); }
};
