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
// dfs.h - Acorn DFS is DiskWranglerCore's DFS module now (core/diskwrangler/fs/dfs/dfs.h, namespace dw::dfs). This
// file keeps the name Dfs:: for the code and tests that use it.
#pragma once

#include <diskwrangler/fs/dfs/dfs.h>

#include "adfs.h"

namespace Dfs = dw::dfs;
