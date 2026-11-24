# Third-party notices

Acorn Disk Wrangler is Copyright (C) 2026 Daniel Jameson and is licensed under the GNU General Public License,
version 3 or (at your option) any later version (see `LICENSE`). This file lists everything in it, or used to make
or test it, that comes from someone else, and on what terms.

## Included in the source
Nothing. All the code, the icon and the documentation were written for this program (one part of DiskWranglerCore
follows another program's method, and is credited below: puff.c).

**DiskWranglerCore** (`core/`, <https://github.com/drdpj/DiskWranglerCore>) is shared with
**CP/M Disk Wrangler** (<https://github.com/drdpj/CPMDiskWrangler>), by the same author and under the same licence.
Its `docs/retro-files-format.md` is the specification of the drag format; the `cpm` namespace in it was settled
with CP/M Disk Wrangler. The access letters in
its `acorn` namespace are those of the Stardot `.inf` format (see below), adopted so that the two agree.

The disc image containers in DiskWranglerCore (`core/diskwrangler/device/`), which let this program read Acorn discs
in ImageDisk, DSK and TeleDisk files, came from CP/M Disk Wrangler. Its TeleDisk "advanced compression" decoder
(`core/diskwrangler/device/lzhuf.cpp`) implements the LZHUF algorithm and follows the structure of **LZHUF.C** by
**Haruyasu Yoshizaki** (1988, with English comments by **Haruhiko Okumura**, 1989), which builds on Okumura's
public-domain LZSS. LZHUF.C was published without any licence statement and has been redistributed and adapted
freely for over thirty years; no terms are known that restrict this use. The decoder was written for CP/M Disk
Wrangler (its tables are generated, not copied) and is distributed under the GPL; the credit is given because its
structure follows LZHUF.C.

The gzip reader in DiskWranglerCore's tape module (`core/diskwrangler/tape/gzip.cpp`), which opens compressed UEF
tape images, was written from RFC 1951 and RFC 1952; its Huffman decoding - codes counted by length and decoded a bit at
a time against those counts, without lookup tables - follows the method of **puff.c** by **Mark Adler** (part of zlib,
<https://github.com/madler/zlib/tree/develop/contrib/puff>). No code was copied from it; the credit is given because
the decoding follows its method, and its notice is:

> Copyright (C) 2002-2013 Mark Adler, all rights reserved
>
> This software is provided 'as-is', without any express or implied warranty. In no event will the author be held
> liable for any damages arising from the use of this software. Permission is granted to anyone to use this software
> for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the
> following restrictions: 1. The origin of this software must not be misrepresented; you must not claim that you wrote
> the original software. If you use this software in a product, an acknowledgment in the product documentation would
> be appreciated but is not required. 2. Altered source versions must be plainly marked as such, and must not be
> misrepresented as being the original software. 3. This notice may not be removed or altered from any source
> distribution.

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
| Watford Electronics DFS 1.44 and DDFS 1.54T ROMs (the owner's copies, disassembled; not in the repo) | how a 62-file disc is formatted, recognised and written: the split between the two catalogues, the first catalogue's lowered size, which cycle number counts on | facts only (the ROMs are Watford Electronics') |
| Watford Electronics DFS manual, section 9.2 "Disc Catalogue" (1983; text at bbc.nvg.org `doc/WatfordDFS.txt`) | the 62-file disc's second catalogue in sectors 2-3 and its &AA recognition bytes | facts only |
| BeebWiki, "Acorn DFS disc format" (<https://beebwiki.mdfs.net/Acorn_DFS_disc_format>) and J.G. Harston's DFS format notes (<https://mdfs.net/Docs/Comp/Disk/Format/DFS>) | the `!.!!!!!!!` guard entry some tools write, and telling a 62-file disc from a file whose data starts with &AA | facts only |
| Feedback from Rob Smallshire, author of oaknut (GitHub issue #1) | public access, name character set, typed loads | - |
| Acorn's Atom DOS ROM (4K at &E000, the owner's copy in `testdata/ATOM`, disassembled with MAME's `unidasm`; not in the repo) and BM-Soft's FORMAT program for it (the owner's `FORM40`) | the Atom's catalogue: the space directory, the lock bit, sector 1 bytes 4 and 6, an entry's byte 6, 16-bit addresses, where `*SAVE` puts a file, what `*TITLE` writes and what a new disc holds (`docs/disc-formats.md`, "Acorn Atom DOS") | facts only (the ROM is Acorn's) |
| MAME's Acorn Atom driver (`src/mame/acorn/atom.cpp`, its `.atm` quickload) | the `.atm` file header: a 16-byte name, load, exec and length | BSD-3-Clause; facts only, no code taken |
| The AtomInPC documentation (quoted by the owner) | the AtomInPC file header: load, exec, the data's length, 8 reserved bytes | facts only |
| Acorn's MOS 1.20 ROM (the BBC Micro's OS; the copy jsbeeb ships, disassembled with MAME's `unidasm` and watched running in jsbeeb; not in the repo) | the cassette filing system: block layout, CRC, flags, how blocks make a file, and how `*SAVE` writes a tape (the dummy byte, leader, gaps and trailer) (`docs/disc-formats.md`, "Tapes") | facts only (the ROM is Acorn's) |
| The UEF specification 0.10 (Thomas Harte, Fraser Ross, Greg Cook; document draft 28, 2006; in PlayUEF's `docs/UEFspecification.html`) | the UEF tape image format, read by DiskWranglerCore's tape module | facts only |
| The TZX specification 1.20 (World of Spectrum, 2006) and CPCWiki's "Format:CDT tape image file format" page | TZX and CDT files, which the program recognises and names (it doesn't open them) | facts only |
| PlayUEF (<https://github.com/8bitkick/PlayUEF>, GPL v3) | how another UEF reader takes the `&0112` gap's length (agreeing with the core's reading of the specification) | facts only; no code taken |

## Used only for testing (not included, not distributed)
* **Real disc images**: from b-em's `discs/` folder (the Master Welcome disc, ARM Evaluation System discs 1-5,
  Master 512 Disc 1, the Pan*.dsd discs) and oaknut's test data (Acorn User Tau85), and the owner's own hard disc and Master 512 discs. They belong to their owners
  and are not part of this repository; the tests read them from outside it.
* **Real tape images** from The Stairway to Hell (<http://www.stairwaytohell.com>, each with its `.txt`): they belong to
  their owners and are not part of this repository; the tests read them from outside it.
* **jsbeeb** (Matt Godbolt and contributors, <https://github.com/mattgodbolt/jsbeeb>, GPL v3 or later), driven
  through **jsbeeb-mcp** (4.5.0) on **Node.js**: a BBC Micro emulator, used to run MOS 1.20 on the real tapes
  (`*CAT`, `CHAIN ""`, breakpoints in the cassette code) while the tape format was settled, and (jsbeeb 2.22.1, its
  `MachineSession` driven by a script outside this repository) to load the tapes this program writes and to catch
  what the MOS sends to the cassette when it saves. Nothing of it is in this program.
* **dosfstools** (`mkfs.fat`, `fsck.fat`; GPL v3+) and **mtools** (GPL v3+) are run by the tests as independent
  checks; **Xvfb** and **xdotool** drive the real drag-and-drop test on Linux. **Pillow** (HPND licence) made
  `icons/acorndiskwrangler.ico` from the PNGs and was used to check `icons/acorndiskwrangler.icns`.

## Linked at run time / bundled in binary releases
* **Qt 6** (or Qt 5.15 in a Qt 5 build; The Qt Company and contributors), used under the **GNU LGPL v3** (also
  available under GPL v2/v3).
  The program links to Qt's shared libraries; it does not modify Qt.
* A binary release also ships the libraries Qt needs on that platform. Which ones depends on the platform and
  the packages it was built from, so they are not listed here: `core/ci/collect-licences.sh` traces each one to its
  package (MSYS2 on Windows, Ubuntu for the Linux AppImage, Homebrew for a Qt 5 Mac build) and gathers that
  package's licence
  texts. A release build compiles them into the program - **Help > Licences**, with an index of which library
  came from which package and its stated licence - and also ships them as files in `licences/bundled`. On
  Windows they include the GCC runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`; GPL v3 with the GCC Runtime
  Library Exception) and `libwinpthread-1.dll` (MIT and BSD-style); on every platform, things like ICU (Unicode
  licence), HarfBuzz (MIT), FreeType (FreeType Licence), zlib, libpng, PCRE2 and zstd (BSD-style).
  The Mac build uses Qt's official binaries, which contain their own copies of such libraries; their licences
  come from Qt's source at the same version, where Qt records each one (`core/ci/qt-attributions.py`).
  Under the LGPL, Qt's own licence text accompanies it (compiled in, and as `licences/LGPL-3.0.txt`), and
  recipients must be able to replace the Qt libraries with their own builds (they can: every build links to
  Qt's ordinary shared libraries) and to obtain this program's source (each GitHub release carries it).
