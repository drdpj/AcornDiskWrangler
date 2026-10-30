# Acorn Disk Wrangler

View and edit disc images from Acorn's 8-bit machines - the BBC Micro, Master and Master 512: ADFS hard discs
and floppies, DFS floppies, Acorn DOS Plus discs, PC floppies, and the DOS Plus partition kept inside an ADFS hard
disc. Add, extract, rename and delete files, set attributes and boot options, view BASIC programs and text, make
new blank discs, and drag files between discs, to and from your desktop, and to and from
[CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler) and other tools that support the
[retro-files](docs/retro-files-format.md) drag-and-drop format.

## Download
Get the latest build for Windows, macOS or Linux from the
[Releases page](https://github.com/drdpj/AcornDiskWrangler/releases).

[![Latest release](https://img.shields.io/github/v/release/drdpj/AcornDiskWrangler?label=release)](https://github.com/drdpj/AcornDiskWrangler/releases/latest)
[![Latest pre-release](https://img.shields.io/github/v/release/drdpj/AcornDiskWrangler?include_prereleases&label=pre-release)](https://github.com/drdpj/AcornDiskWrangler/releases)

* **Windows** (10 or later, 64-bit): run the `...-setup.exe` installer, which installs into Program Files and
  adds the program to the Start menu (without administrator rights, choose to install it only for you), or unzip
  the `.zip` anywhere and run `acorndiskwrangler.exe`. Neither is signed, so Windows may warn the first time: choose
  **More info**, then **Run anyway**.
* **macOS** (12 or later, Intel or Apple Silicon): open the `.dmg` and drag the app to Applications. It isn't
  signed by Apple, so the first time, right-click it and choose **Open**.
* **Linux** (x86_64):
  * **Ubuntu 22.04 or later, Debian 12 or later:** install the `.deb`, which uses the system's Qt and puts the
    program in the applications menu with its icon:
    `sudo apt install ./acorndiskwrangler-<version>-linux-amd64.deb` (the `./` matters; Ubuntu's App Center may not
    open a downloaded `.deb`). Remove it with `sudo apt remove acorndiskwrangler`.
  * **Any other Linux:** the `.AppImage` runs as it is - make it executable (`chmod +x`) and start it. It
    doesn't add itself to the applications menu, so docks show a generic icon for it unless a tool such as
    Gear Lever or AppImageLauncher integrates it.

Help > About shows the version, and Help > Licences the licences of the program and everything distributed with it.

## What it opens
| Disc | Files | Notes |
|---|---|---|
| ADFS hard disc (old map) | BeebSCSI `scsi0.dat` and similar | Unzip a zipped image first. If it has a DOS Plus partition (`$.DOS.DRIVE_C`), that opens as a second tab. |
| ADFS S / M / L floppy (160K, 320K, 640K) | `.ads` `.adm` `.adl` | |
| DFS, single- or double-sided, 40 or 80 track | `.ssd` `.dsd` | A `.dsd` shows its two sides in one tree, as **Drive 0** and **Drive 2**: each is a separate disc. |
| Acorn DOS Plus 800K | `.img` | |
| Acorn 640K ADFS/DOS Plus hybrid (Master 512 boot disc) | `.adl` | The ADFS side is read-only, the DOS side editable. |
| PC floppy 360K, 720K, 1.2M, 1.44M | `.img` and others | FAT12. |

The format is worked out from the disc's contents, then its name and size. File > New disc image makes a blank disc of
any of these types. A new DOS Plus 640K disc needs Acorn's DOSBOOT boot code, which isn't included: you point it at
an existing Master 512 disc image or a DOSBOOT file.

## Using it
* **One disc per window.** File > Open opens a new window if this one already has a disc in it. Dropping a disc
  image on a window opens it too (on a hard disc you're asked whether to open it or copy it on as a file).
* **Copy files** by dragging them between windows, between the tabs of one window, or between the two sides of a
  DFS disc (drop them on the other drive's row) - or with **Copy** and **Paste** (Edit menu, or your system's usual
  keys: Ctrl+C / Ctrl+V, Cmd+C / Cmd+V on a Mac). Names, load/exec addresses
  and attributes are converted to suit the destination: for example, a DFS file `P.GAME` dropped into `$` on an
  ADFS disc becomes `$.P.GAME`, and an ADFS directory with a one-letter name can become a DFS directory letter.
* **Add files** by dropping them (or folders) from your file manager onto the tree - onto a directory row to put
  them there - or copy them in your file manager and Paste them here. On ADFS and DFS discs, `.inf` sidecar files are read (Tools > *Read .inf sidecar files when
  adding*, on by default), so load/exec addresses and attributes come through.
* **Extract files** by dragging them out to your desktop or another program, by copying them here and pasting
  them there, or with Extract. Dragged to a tool
  that supports retro-files, they keep their addresses and attributes (see below). `.inf` sidecars are
  written too if *Write .inf sidecar files when extracting* (Tools) is on.
* **View** (double-click, F3): text, hex, or a BBC BASIC listing of a tokenised program. The text can be copied
  or saved.
* **New directory** (F7) on ADFS and DOS discs (DFS discs have none). **Rename** (F2) files, directories and the disc itself; **Properties** (Alt+Enter) for access attributes and
  load/exec addresses, for one file or many. On a DFS disc, select several files and give Properties a directory
  letter to move them all into that directory (it's a rename: names already taken there are pointed out first).
* **Boot option** (Edit > Boot option): what SHIFT+BREAK does - nothing, `*LOAD`, `*RUN` or `*EXEC !BOOT` - on
  ADFS discs and each side of a DFS disc.
* **Side order** (Tools > Side order): double-sided images come with their sides stored in one of two orders.
  The order is detected when the image opens; if files look scrambled, try *Read as* the other order. *Convert*
  rewrites the image in the other order, for emulators that expect it.
* **Save** (Ctrl+S). Nothing is written until you save, and the whole disc is checked first: if anything doesn't
  add up, or the file was changed by something else meanwhile, it won't save. A disc that fails the check when
  opened is read-only. A `.bak` copy of the previous file is kept (Tools; on unless you turn it off).

### Settings, and carrying the program with you
Options (Tools menu), the last folders used are saved in
the usual place for your system: the registry (`HKEY_CURRENT_USER\Software\acorndiskwrangler\acorndiskwrangler`) on Windows,
`~/Library/Preferences/com.acorndiskwrangler.acorndiskwrangler.plist` on macOS, `~/.config/acorndiskwrangler/acorndiskwrangler.conf` on Linux.
To keep them with the program instead - on a USB stick, say - choose **File > Create local config file**: it
makes **`acorndiskwrangler.ini`** in the program's folder (beside the `.exe` on Windows, beside the `.app` on macOS) with
a copy of the current settings, and uses it from then on. (Putting an empty file of that name there by hand does
the same.) Delete the file to go back to the system's settings. Help > About says which is in use. For the AppImage the file goes beside the
`.AppImage`; a program installed from the `.deb` or by the Windows installer always uses the system's place, so it
doesn't offer this.

## Drag and drop between disc tools
Both Wranglers implement **[retro-files](docs/retro-files-format.md)** (`application/x-retro-files+json`), an open
format for dragging files out of disc images and into other programs. An ordinary drag between programs carries
only a file's name and contents, but a file on a retro disc has more than that, and it matters: an Acorn file's
load and execution addresses (without them a program may not run, and they also hold its type and date), access
bits, a DFS directory letter, DOS attributes, a CP/M user number. retro-files carries those too. That means:

* **Files move between disc tools without losing anything.** Drag a file from a disc in one program that
  supports the format to a disc in another - between Acorn Disk Wrangler and CP/M Disk Wrangler, between two
  copies of either, or to any other tool that adopts it - and its attributes go with it. The receiving program
  converts what the destination disc can hold (for example, an Acorn file's lock becomes CP/M's read-only flag)
  instead of guessing from a bare file.
* **Nothing else breaks.** The same drag also carries ordinary files, so dropping on your desktop, a file
  manager or any other program works as usual.
* **Any tool can join in.** It is a small, versioned JSON document, with attributes grouped by filesystem
  (`acorn`, `fat`, `cpm`): a tool for another kind of disc adds its own group and reads the groups it
  understands. The [specification](docs/retro-files-format.md) explains the format and why it exists.

## Limitations
* ADFS new-map discs (D and E format, from the Archimedes) aren't supported; they're recognised and refused.
* ADFS: names up to 10 characters, at most 47 entries in a directory, and each file needs a contiguous run of
  free space. It can't compact a fragmented disc (`*COMPACT` on the machine can).
* DFS: at most 31 files per side, names of 7 characters plus a directory letter. Watford's 62-file DFS isn't
  supported.
* DOS: names are made upper-case 8.3. Only `$.DOS.DRIVE_C` is looked for as the hard disc's DOS partition.
* Some DFS images are saved with their blank end trimmed off. They open normally and are saved full length.
* Discs with non-standard or copy-protected tracks aren't handled.
* The viewer shows the first 1 MiB of a very large file.
* A new DOS Plus 640K disc hasn't yet been tried on a real Master 512.

## Building from source
Needs Qt 6 (or Qt 5.15) with its widgets module, and a C++17 compiler.

    qmake acorndiskwrangler.pro && make            # or: cmake -S . -B build && cmake --build build
    ./acorndiskwrangler [image ...]                # each image opens in its own window

On Linux, `make install` (or `qmake PREFIX=$HOME/.local ...` then `make install` for just you) adds a menu entry,
so the app gets its icon in the dock. Windows builds use MSYS2. See [docs/building.md](docs/building.md) for
Windows, macOS, Qt 5, the release builds and how they carry their licences.

## Licence
Copyright (C) 2026 Daniel Jameson. Licensed under the GNU General Public License v3.0 or later - see
[LICENSE](LICENSE). Uses Qt under the LGPL v3. Everything else that was consulted or used is listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Working on the code? See [AGENTS.md](AGENTS.md): design rules, format notes, tests and pitfalls.
