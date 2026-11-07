[![Latest release](https://img.shields.io/github/v/release/drdpj/AcornDiskWrangler?label=release)](https://github.com/drdpj/AcornDiskWrangler/releases/latest)

# Acorn Disk Wrangler

View and edit disc images from Acorn's 8-bit machines - the BBC Micro, Master and Master 512. It opens ADFS hard
discs and floppies, DFS floppies, Acorn DOS Plus discs, PC floppies and the DOS Plus partition inside an ADFS hard
disc. You can add, extract, rename and delete files, set attributes and boot options, read BASIC programs and text,
and make new blank discs. Files drag between discs, to and from your desktop, and to and from
[CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler) and other tools that speak
[retro-files](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/retro-files-format.md).

## Download
Get the latest build from the [Releases page](https://github.com/drdpj/AcornDiskWrangler/releases/latest).
None of the builds are signed, so your system may warn you the first time you run it.

* **Windows** (10 or later, 64-bit): run the `...-setup.exe` installer (it installs into Program Files; without
  administrator rights, choose to install it just for you), or unzip the `.zip` anywhere and run
  `acorndiskwrangler.exe`. If Windows warns you, choose **More info**, then **Run anyway**.
* **macOS** (12 or later, Intel or Apple Silicon): open the `.dmg` and drag the app to Applications. The first time,
  right-click it and choose **Open**.
* **Linux** (x86_64):
  * **Ubuntu 22.04+ or Debian 12+:** `sudo apt install ./acorndiskwrangler-<version>-linux-amd64.deb` (the `./`
    matters). It uses the system's Qt and adds the program to the applications menu. Remove it with
    `sudo apt remove acorndiskwrangler`.
  * **Any other distribution:** make the `.AppImage` executable (`chmod +x`) and run it. It doesn't add itself to
    the applications menu, so docks show a generic icon unless a tool such as Gear Lever or AppImageLauncher
    integrates it.

Help > About shows the version; Help > Licences shows the licences of the program and everything shipped with it.

## What it opens
| Disc | Files | Notes |
|---|---|---|
| ADFS hard disc (old map) | BeebSCSI `scsi0.dat` and similar | Unzip a zipped image first. A DOS Plus partition (`$.DOS.DRIVE_C`) opens in a second tab. |
| ADFS S / M / L floppy (160K, 320K, 640K) | `.ads` `.adm` `.adl` | |
| DFS, single- or double-sided, 40 or 80 track | `.ssd` `.dsd` | The two sides of a `.dsd` are separate discs, shown in one tree as **Drive 0** and **Drive 2**. |
| Acorn DOS Plus 800K | `.img` | |
| Acorn 640K ADFS/DOS Plus hybrid (Master 512 boot disc) | `.adl` | The ADFS side is read-only; the DOS side can be edited. |
| PC floppy 360K, 720K, 1.2M, 1.44M | `.img` and others | FAT12. |

Any floppy above can also be in an **ImageDisk** (`.imd`), **DSK** or extended DSK (`.dsk`) or **TeleDisk**
(`.td0`) file instead of a plain sector dump: it opens the same way and saves back in the same kind of file
(TeleDisk files can only be read).

The format is worked out from the disc's contents, then its name and size. **File > New disc image** makes a blank
disc of any of these types. A new DOS Plus 640K disc needs Acorn's DOSBOOT boot code, which isn't included: point
it at an existing Master 512 disc image or a DOSBOOT file.

## Using it
* **One disc per window.** File > Open uses a new window if this one already has a disc. Dropping a disc image on
  a window opens it too; on a hard disc you're asked whether to open it or copy it on as a file.
* **Copy files** by dragging them between windows, between the tabs of one window, or between the two sides of a
  DFS disc (drop them on the other drive's row), or with Copy and Paste (Ctrl+C / Ctrl+V, Cmd+C / Cmd+V on a Mac).
  Names, load/exec addresses and attributes are converted to suit the destination: a DFS file `P.GAME` dropped into
  `$` on an ADFS disc becomes `$.P.GAME`, and an ADFS directory with a one-letter name can become a DFS directory
  letter.
* **Add files** by dropping files or folders from your file manager onto the tree (onto a directory row to put
  them there), or by pasting files copied there. On ADFS and DFS discs `.inf` sidecar files are read, so load/exec
  addresses and attributes come through (Tools > *Read .inf sidecar files when adding*, on by default).
* **Extract files** by dragging them out, by copying and pasting them into another program, or with Extract
  (Ctrl+E). A tool that supports retro-files receives their addresses and attributes too. `.inf` sidecars are
  written if Tools > *Write .inf sidecar files when extracting* is on.
* **View** (double-click or F3) shows text, hex, or a listing of a BBC BASIC program; the text can be copied or
  saved.
* **Rename** (F2) files, directories and the disc itself. **Properties** (Alt+Enter) sets access attributes and
  load/exec addresses, for one file or many; on a DFS disc, giving several files a directory letter moves them all
  into that directory. **New directory** (F7) works on ADFS and DOS discs.
* **Boot option** (Edit menu): what SHIFT+BREAK does - nothing, `*LOAD`, `*RUN` or `*EXEC !BOOT` - for ADFS discs
  and each side of a DFS disc.
* **Side order** (Tools menu): a double-sided image stores its sides in one of two orders. The order is detected
  when the image opens; if files look scrambled, *Read as* the other order. *Convert* rewrites the image in the
  other order, for emulators that expect it.
* **Save** (Ctrl+S). Nothing is written until you save, and the whole disc is checked first: if anything doesn't
  add up, or another program changed the file meanwhile, it won't save. A disc that fails the check when opened is
  read-only. Saving writes a complete new copy of the image beside it, checks that copy, and only then puts it in
  the old file's place, so the original is never left half-written; this needs free space for a second copy, a
  large hard disc image takes a second or two longer, and on Windows the save is refused while another program
  (an emulator) has the image open. A `.bak` copy of the previous file is kept unless you turn that off (Tools).

### Settings, and carrying the program with you
Settings (the Tools options and the last folders used) live in the usual place for your system: the registry on
Windows (`HKEY_CURRENT_USER\Software\acorndiskwrangler\acorndiskwrangler`),
`~/Library/Preferences/com.acorndiskwrangler.acorndiskwrangler.plist` on macOS and
`~/.config/acorndiskwrangler/acorndiskwrangler.conf` on Linux.

To keep them with the program instead - on a USB stick, say - choose **File > Create local config file**. It writes
`acorndiskwrangler.ini` beside the program (beside the `.exe`, the `.app` or the `.AppImage`) with a copy of the
current settings, and uses it from then on; an empty file of that name does the same. Delete it to go back to the
system's settings. Help > About says which is in use. Copies installed by the Windows installer or the `.deb` always
use the system's settings, so they don't offer this.

## Drag and drop between disc tools
An ordinary drag between programs carries only a file's name and contents, but a file on a retro disc has more that
matters: an Acorn file's load and execution addresses (without them a program may not run), its access bits and DFS
directory letter, DOS attributes, a CP/M user number. Both Wranglers implement
**[retro-files](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/retro-files-format.md)** (`application/x-retro-files+json`), an open format that carries these
too:

* **Nothing is lost between disc tools.** Drag a file between Acorn Disk Wrangler and CP/M Disk Wrangler, between two
  copies of either, or to any other tool that adopts the format, and its attributes go with it. The receiving
  program converts what the destination can hold (an Acorn file's lock becomes CP/M's read-only flag, for example)
  instead of guessing from a bare file.
* **Nothing else breaks.** The same drag carries ordinary files, so dropping on your desktop, a file manager or any
  other program works as usual.
* **Any tool can join in.** It is a small, versioned JSON document with attributes grouped by filesystem (`acorn`,
  `fat`, `cpm`): a tool for another kind of disc adds its own group and reads the ones it understands. The
  [specification](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/retro-files-format.md) explains the format and why it exists.

## Limitations
* ADFS new-map discs (D and E format, from the Archimedes) are recognised but not supported.
* ADFS: names of up to 10 characters, at most 47 entries in a directory, and each file needs a contiguous run of
  free space. It can't compact a fragmented disc (`*COMPACT` on the machine can).
* DFS: at most 31 files per side (62 on a Watford 62-file disc), names of 7 characters plus a directory letter.
  Watford's double-density DDFS isn't supported.
* DOS: names are made upper-case 8.3. Only `$.DOS.DRIVE_C` is looked for as a hard disc's DOS partition.
* DFS images saved with their blank end trimmed off open normally and are saved full length.
* Discs with non-standard or copy-protected tracks aren't handled.
* The viewer shows the first 1 MiB of a very large file.
* A new DOS Plus 640K disc hasn't yet been tried on a real Master 512.

## Building from source
You need Qt 6 (or Qt 5.15) with its widgets module, and a C++17 compiler.

    qmake acorndiskwrangler.pro && make            # or: cmake -S . -B build && cmake --build build
    ./acorndiskwrangler [image ...]                # each image opens in its own window

On Linux, `make install` (or `qmake PREFIX=$HOME/.local ...`, then `make install`, for just you) adds a menu entry
so docks show the program's icon. [building.md](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/building.md) (in DiskWranglerCore, which is the `core/` submodule: clone with `--recurse-submodules`) covers Windows (MSYS2), macOS, Qt 5, and how
the release builds are made and carry their licences.

Working on the code? Start with [AGENTS.md](AGENTS.md): the design rules, the source map, the window and the tests.
The disc formats are described in [docs/disc-formats.md](docs/disc-formats.md).

## Licence
Copyright (C) 2026 Daniel Jameson. Licensed under the GNU General Public License v3.0 or later - see
[LICENSE](LICENSE). Uses Qt under the LGPL v3. Everything else that was used or consulted is listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
