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

// The access bits of an ADFS entry (Adfs::Image's API, the .inf reader): a header of their own, so that code that only
// needs the bits (hostpolicy.h, and through it CP/M Disk Wrangler's interop test) doesn't bring in adfs.h.
namespace Adfs {
enum Flag { FlagR = 1, FlagW = 2, FlagL = 4, FlagE = 8 };
} // namespace Adfs
