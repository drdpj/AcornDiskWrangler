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
| Acorn's ADFS 1.30 ROM, as disassembled in Dominic Beesley's ADFS-multi-target (<https://github.com/dominicbeesley/ADFS-multi-target>, `src/adfs.asm`, `src/floppy.asm`) | the free space map checksum routine; how ADFS numbers an L disc's sectors (side 0 first) | facts only (the ROM is Acorn's) |
| b-em (<https://github.com/stardot/b-em>, `src/sdf-geo.c`) | the file layout of ADFS and DFS images (interleaved / sequential sides); its map checksum routine agrees with Acorn's | GPL v2 |
| RISC OS Open wiki, "FileCore Disc Map Old" | the old map layout and check bytes | facts only |
| MAME's Acorn floppy formats (`src/lib/formats`) | the usual side order of Acorn double-sided images | BSD-3-Clause |
| Stardot `.inf` format specification (<https://github.com/stardot/inf_format>) | the access letters used in `docs/retro-files-format.md` | facts only |
| RPCEmu 0.9.5 host file system (`src/hostfs.c`) | the `,ttt` and `,llllllll-eeeeeeee` host file name conventions described in the spec | facts only |
| Qt 5.15 `qwindowsmime.cpp` | when Windows asks a drag source for its files | facts only |
| Feedback from Rob Smallshire, author of oaknut (GitHub issue #1) | public access, name character set, typed loads | - |

## Used only for testing (not included, not distributed)
* **Real disc images**: from b-em's `discs/` folder (the Master Welcome disc, ARM Evaluation System discs 1-5,
  Master 512 Disc 1, the Pan*.dsd discs) and oaknut's test data (Acorn User Tau85), and the owner's own hard disc and Master 512 discs. They belong to their owners
  and are not part of this repository; the tests read them from outside it.
* **dosfstools** (`mkfs.fat`, `fsck.fat`; GPL v3+) and **mtools** (GPL v3+) are run by the tests as independent
  checks; **Xvfb** and **xdotool** drive the real drag-and-drop test on Linux. **Pillow** (HPND licence) made
  `icons/acorndiskwrangler.ico` from the PNGs and was used to check `icons/acorndiskwrangler.icns`.

## Linked at run time / bundled in binary releases
* **Qt 6** (or Qt 5.15 in a Qt 5 build; The Qt Company and contributors), used under the **GNU LGPL v3** (also
  available under GPL v2/v3).
  The program links to Qt's shared libraries; it does not modify Qt.
* A binary release also ships the libraries Qt needs on that platform. Which ones depends on the platform and
  the packages it was built from, so they are not listed here: `ci/collect-licences.sh` traces each one to its
  package (MSYS2 on Windows, Ubuntu for the Linux AppImage, Homebrew for a Qt 5 Mac build) and gathers that
  package's licence
  texts. A release build compiles them into the program - **Help > Licences**, with an index of which library
  came from which package and its stated licence - and also ships them as files in `licences/bundled`. On
  Windows they include the GCC runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`; GPL v3 with the GCC Runtime
  Library Exception) and `libwinpthread-1.dll` (MIT and BSD-style); on every platform, things like ICU (Unicode
  licence), HarfBuzz (MIT), FreeType (FreeType Licence), zlib, libpng, PCRE2 and zstd (BSD-style).
  The Mac build uses Qt's official binaries, which contain their own copies of such libraries; their licences
  come from Qt's source at the same version, where Qt records each one (`ci/qt-attributions.py`).
  Under the LGPL, Qt's own licence text accompanies it (compiled in, and as `licences/LGPL-3.0.txt`), and
  recipients must be able to replace the Qt libraries with their own builds (they can: every build links to
  Qt's ordinary shared libraries) and to obtain this program's source (each GitHub release carries it).
