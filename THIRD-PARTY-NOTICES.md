# Third-party notices

Acorn Disk Wrangler is Copyright (C) 2026 Daniel Jameson and is licensed under the GNU General Public License,
version 3 or (at your option) any later version (see `LICENSE`). This file lists everything in it, or used to make
or test it, that comes from someone else, and on what terms.

## Included in the source
Nothing. All the code, the icon and the documentation were written for this program.

`docs/retro-files-format.md` is shared with **CP/M Disk Wrangler** (<https://github.com/drdpj/CPMDiskWrangler>),
by the same author and under the same licence; the `cpm` namespace in it was settled there. The access letters in
its `acorn` namespace are those of the Stardot `.inf` format (see below), adopted so that the two agree.

## Not included: Acorn's DOSBOOT
A new DOS Plus 640K disc needs Acorn's DOSBOOT boot code. It is **not** part of this program or its repository:
File > New image copies it from a disc image or a DOSBOOT file that the user supplies.

## Consulted, not copied
These were read for facts about formats or behaviour. No code or text was copied from them.

| Source | Used for | Terms |
|---|---|---|
| MAME's Acorn floppy formats (`src/lib/formats`) | the usual side order of Acorn double-sided images | BSD-3-Clause |
| Gerald Holdsworth's Disc Image Manager and disc image notes | side order of Acorn images; checking the ADFS free space map checksum | facts only |
| Stardot `.inf` format specification (<https://github.com/stardot/inf_format>) | the access letters used in `docs/retro-files-format.md` | facts only |
| RPCEmu 0.9.5 host file system (`src/hostfs.c`) | the `,ttt` and `,llllllll-eeeeeeee` host file name conventions described in the spec | facts only |
| Qt 5.15 `qwindowsmime.cpp` | when Windows asks a drag source for its files | facts only |
| Feedback from Rob Smallshire, author of oaknut (GitHub issue #1) | public access, name character set, typed loads | - |

## Used only for testing (not included, not distributed)
* **Real disc images**: the Master Welcome disc, Acorn User Tau85 and ARM Evaluation disc 4 from b-em's `discs/`
  folder and oaknut's test data, and the owner's own hard disc and Master 512 discs. They belong to their owners
  and are not part of this repository; the tests read them from outside it.
* **dosfstools** (`mkfs.fat`, `fsck.fat`; GPL v3+) and **mtools** (GPL v3+) are run by the tests as independent
  checks; **Xvfb** and **xdotool** drive the real drag-and-drop test on Linux. **Pillow** (HPND licence) made
  `icons/acorndiskwrangler.ico` from the PNGs and was used to check `icons/acorndiskwrangler.icns`.

## Linked at run time / bundled in binary releases
* **Qt 5** (The Qt Company and contributors), used under the **GNU LGPL v3** (also available under GPL v2/v3).
  The program links to Qt's shared libraries; it does not modify Qt.
* A Windows build made with MSYS2/MinGW64 and `windeployqt` also ships libraries from MSYS2 packages: the GCC
  runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`; GPL v3 with the GCC Runtime Library Exception),
  `libwinpthread-1.dll` (MIT and BSD-style), and whatever Qt itself needs on that system (for example ICU - Unicode
  licence; HarfBuzz - MIT; FreeType - FreeType Licence; zlib - zlib licence; libpng - libpng licence; PCRE2 -
  BSD; zstd - BSD; double-conversion - BSD; MD4C - MIT; Brotli - MIT; Graphite2 - LGPL/MPL).
  **When distributing such a build, include their licence texts**: the README's deployment steps copy them from
  MSYS2's `/mingw64/share/licenses/<package>` folders into `deploy/licenses`, alongside this file and `LICENSE`.
  Under the LGPL, Qt's own licence text must accompany it, and recipients must be able to replace the Qt DLLs
  with their own builds (they can: the program uses the ordinary DLLs `windeployqt` copies) and to obtain this
  program's source (publish the repository link or a source zip with each release).
