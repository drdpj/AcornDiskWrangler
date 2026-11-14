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
// windowhelp.h - what the GUI tests use to reach a window's panes and windows through the window kit's API
// (DiscWindow::pane(group), DiscWindow::all(), the typed document()): the shims MainWindow used to have for this
// (tree(), dfsTree(), dosTree(), windows()) are gone from the program.
#pragma once

#include <QList>
#include "../src/mainwindow.h"

namespace wt {

inline DropTree *adfsTree(MainWindow &w) { return static_cast<DropTree *>(w.pane("ADFS")); }
inline DropTree *dosTree(MainWindow &w) { return static_cast<DropTree *>(w.pane("DOS")); }
inline DropTree *dfsTree(MainWindow &w) { return static_cast<DropTree *>(w.pane("DFS")); }
inline DropTree *adfsTree(MainWindow *w) { return adfsTree(*w); }
inline DropTree *dosTree(MainWindow *w) { return dosTree(*w); }
inline DropTree *dfsTree(MainWindow *w) { return dfsTree(*w); }

// Every main window, oldest first.
inline QList<MainWindow *> windows() {
    QList<MainWindow *> l;
    for (dw::ui::DiscWindow *w : dw::ui::DiscWindow::all())
        if (auto *m = qobject_cast<MainWindow *>(w)) l << m;
    return l;
}

} // namespace wt
