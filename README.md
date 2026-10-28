# Acorn Disk Wrangler

Qt5 (C++) tool for Acorn 8-bit disc images: ADFS hard discs and floppies (old map), DFS
(.ssd/.dsd), PC FAT floppies, Acorn DOS Plus floppies, and the DOS Plus partition stored in
$.DOS.DRIVE_C on an ADFS hard disc. Each disc opens in its own window and files can be
dragged between windows. Unzip scsi0.zip first; the tool works on the raw .dat.

> **Drag and drop between disc tools:** files dragged out of a disc keep their load/exec addresses and
> attributes in an open format, `application/x-retro-files+json`, that other tools can adopt.
> **Specification and rationale: [docs/retro-files-format.md](docs/retro-files-format.md)**

Build:  qmake acorndiskwrangler.pro && make      (or: cmake -S . -B build && cmake --build build)
Needs:  Qt 5 widgets dev package (Ubuntu: qtbase5-dev)
Run:    ./acorndiskwrangler [image ...]          (each image gets its own window)

## Installing on Linux (menu entry and dock icon)
GNOME, KDE and other Linux desktops take an application's name and icon from a `.desktop` file, not from the
running program, so a program run straight from the build folder shows a generic icon (a gear in Ubuntu's
dock). `make install` puts the program, `acorndiskwrangler.desktop` and the icons where they look:

    qmake PREFIX=$HOME/.local acorndiskwrangler.pro && make && make install      # just for you
    qmake acorndiskwrangler.pro && make && sudo make install                     # everyone (/usr/local)

(CMake: `cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local && cmake --build build && cmake --install build`.)
It then appears in the applications menu, and its windows get the floppy icon in the dock, also when you run a
copy from the build folder. `~/.local/bin` must be on your PATH for the menu entry to start it (Ubuntu adds it at
login once the folder exists). If the dock still shows a gear, log out and in again.

## macOS
Untested so far. A qmake or CMake build makes `acorndiskwrangler.app` with the floppy icon
(`icons/acorndiskwrangler.icns`) for Finder and the Dock; `macdeployqt acorndiskwrangler.app` makes it
self-contained.

## Building on Windows (MSYS2 / MinGW64)
1. Install [MSYS2](https://www.msys2.org/), then open the **MSYS2 MINGW64** shell (not "MSYS2 MSYS"
   or "UCRT64") and install the compiler and Qt 5:

       pacman -Syu
       pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-qt5-base mingw-w64-x86_64-qt5-tools

   MSYS2 gives the Qt 5 tools a `-qt5` suffix (`qmake-qt5`, `windeployqt-qt5`); plain `qmake` is
   Qt 6 and may not be installed at all.

2. From the source folder, build a release and deploy it (Qt and MinGW DLLs and plugins copied beside the exe,
   so it runs from Explorer or on another PC), collect the licences of everything it ships, and build again
   with them compiled in:

       bash ci/build.sh windows
       bash ci/collect-licences.sh windows deploy
       bash ci/build.sh windows
       cp -r LICENSE THIRD-PARTY-NOTICES.md licences deploy/

   `deploy/` is then ready to zip (`pacman -S zip`, then `cd deploy && zip -r ../acorndiskwrangler-win64.zip .`).
   qmake prints `WARNING: Failure to find: acorndiskwrangler_resource_res.o`; that is harmless. For a quick
   build to run from the MINGW64 shell, `build/acorndiskwrangler.exe` from the first step is enough.

   Because the program is GPL, publish the matching source alongside the zip (e.g. a link to the
   repository or a source zip); the LGPL also lets recipients swap in their own Qt DLLs.

For the UCRT64 environment instead, use the UCRT64 shell and replace `mingw-w64-x86_64-` with
`mingw-w64-ucrt-x86_64-` in the package names; the scripts follow the shell's prefix.

## Release builds (Windows, macOS, Linux)
`.github/workflows/build.yml` builds all three on GitHub on every push and pull request (the packages are kept
as the run's artifacts), and pushing a tag `v*` publishes them as a release:

| Platform | Built with | Package |
|---|---|---|
| Windows x64 | MSYS2 MINGW64, `windeployqt-qt5` | `.zip` of the program, its DLLs and plugins |
| macOS (Apple Silicon) | Homebrew `qt@5`, `macdeployqt` | `.dmg`; signed ad hoc only, so the first time it must be opened with right-click > Open |
| Linux x86_64 | Ubuntu 22.04, `linuxdeploy` | AppImage |

Every build links to Qt's **shared** libraries, as the LGPL expects (people can replace them with their own),
and carries the licence of everything it ships twice over: compiled into the program (**Help > Licences**) and
as files beside it (`LICENSE`, `THIRD-PARTY-NOTICES.md`, `licences/`). To make that work each platform is built
twice - the first build is deployed to find out which libraries ship, `ci/collect-licences.sh` gathers those
libraries' licences from the packages they came from (it fails if one can't be traced), and the second build
compiles them in. The same scripts work by hand, from the source root:

    bash ci/build.sh windows                       # or macos / linux: build and deploy
    bash ci/collect-licences.sh windows deploy     # macos: build/<app>.app   linux: build/AppDir
    bash ci/build.sh windows                       # again, with the licences compiled in

`ci/`, `licences.cpp/.h`, `licences.qrc`, `licences/` and the workflow are shared, unchanged, with [CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler).

## Supported discs (detected from content, then name/size)
| Format | Notes |
|---|---|
| ADFS old-map hard disc | e.g. BeebSCSI scsi0.dat; plus DOS Plus partition $.DOS.DRIVE_C as a second tab |
| ADFS S / M / L floppy (160K, 320K, 640K) | .ads .adm .adl; same filesystem as the hard disc |
| DFS single/double sided (.ssd, .dsd) | 40 or 80 track; a .dsd opens as two tabs, "DFS drive 0" and "DFS drive 2", each a separate disc |
| PC FAT floppy 360K / 720K / 1.2M / 1.44M | geometry from the boot sector, falling back to a size table |
| Acorn DOS Plus 800K | 1024-byte sectors, no boot sector (layout and file contents checked against a real disc) |
| Acorn 640K ADFS/DOS hybrid | ADFS catalogue + DOSBOOT + DOS Plus FAT12 on one disc (layout and file contents checked against a real Master 512 disc; DOS Plus numbers this disc's sectors side 0 first, which is handled transparently); ADFS tab is read-only, DOS tab editable |

Not supported: ADFS D/E (new map) discs - they are recognised and refused with a message.

## Double-sided images: side order
An image file of an Acorn double-sided floppy (ADFS L, DFS .dsd, DOS Plus 800K and 640K) stores its tracks in one
of two orders:

* **interleaved** - cylinder 0 side 0, cylinder 0 side 1, cylinder 1 side 0, ... This is the usual layout,
  and the one the real discs this tool was checked against use.
* **sequential** - all of side 0 (cylinders 0-79), then all of side 1.

PC floppy images (360K, 720K, 1.2M, 1.44M) are always interleaved, so they have no side order to detect or change.

This is only about the image *file*. How each disc numbers its own sectors is handled separately: ADFS L and
DOS Plus 640K discs count all of side 0, then side 1 (as the machine reads them), while DOS Plus 800K and PC
discs count sector, side, track. Both are checked against real discs. The order is **detected** when an image is opened, by checking which order makes the
structures beyond track 0 line up: DFS catalogues, ADFS directories, DOS directories and the FAT. It can't
always be told: a blank ADFS L or DOS Plus 800K disc, or one with only a few files in the root directory, reads
the same either way. Then the format's usual order is used - interleaved for every Acorn format - and the
status bar says so ("Sides: interleaved (assumed)").

Tools > Side order:
* **Read as interleaved / Read as sequential** - re-reads the image the other way. Nothing is written; use it
  when files look scrambled. It applies to this window only: the next time the image is opened it is detected
  again.
* **Convert to sequential / interleaved...** - rewrites the image file in the other order when you save (the
  disc's contents don't change; Revert cancels it). Use it when an emulator or tool expects the other order.

## New blank images
File > New image... makes a blank, valid disc of any supported type: ADFS S/M/L; DFS 40/80-track single- or
double-sided; Acorn DOS Plus 800K and 640K; PC 360K, 720K, 1.2M and 1.44M. You can give it a disc name, title
or volume label (the save dialog then suggests it as the file name) and, for the Acorn double-sided formats, choose
the side order (interleaved by default). PC
discs get a standard boot sector (not bootable).

A new DOS Plus 640K disc is set up like Acorn's own, with no DOS files: the ADFS free space map (2720 sectors,
none free), the root directory "Acorn DOS Boot Disc" holding DOSBOOT, then an empty DOS Plus FAT and root
directory. DOSBOOT is Acorn's boot code, so it isn't built in: under "Boot code from" choose either an existing
DOS Plus 640K disc image (its whole boot track is copied exactly) or a DOSBOOT file (at most 2304 bytes), and a
catalogue matching Acorn's is built around it (the field keeps your last choice). Booting a new disc on a real
Master 512 has not been tried yet.

## Using it
* **Several discs at once:** File > Open opens in a new window if this one already holds a disc
  (File > New window for an empty one). Drag files/folders from one window's tree and drop them
  on another window's tree (or on the other tab/side of the same window) to **copy**. Names and
  attributes are converted: ADFS/DOS/DFS names are mapped to the target's rules (and made unique),
  load/exec addresses are widened or narrowed (DFS keeps 18 bits), locked <-> read-only. DFS has
  no subdirectories, so folders dropped on a DFS disc are flattened, except that for an ADFS
  directory with a one-letter name (e.g. `$.F`) you are asked whether to put its files in DFS
  directory `F` (`F.NAME`) instead of `$` (Yes / No, or to all such directories in the drop). The other way, a DFS file's
  directory letter becomes an ADFS subdirectory of wherever it is dropped: `P.FILE` dropped on `$`
  becomes `$.P.FILE`, and on `$.Library` becomes `$.Library.P.FILE` (an existing `P` directory is
  reused; files in `$` go straight in). Load/exec addresses keep their low 18 bits on DFS (RISC OS
  style time-stamped files too: a stamp can't be told from an address for certain, so it is never zeroed).
* Drop files/folders from your file manager onto the tree (onto a directory row to target
  it; onto empty space for the root). Works on both tabs.
* **Drop a disc image to open it.** On an empty window it opens there (several images: the rest in new windows).
  On a window holding a floppy, a dropped disc image opens in a new window instead of being copied onto the
  floppy; other files dropped with it are added as usual. On a **hard disc** you are asked whether to open the
  image or copy it onto the hard disc as a file (Open / Copy onto this disc / Cancel), since keeping images there
  is common. To copy an image onto a floppy as a file, use Add files.
* **Drag files out** onto Explorer, the desktop or any program that accepts dropped files. They come out
  exactly as Extract to host writes them (names made safe for the host, directories as folders, `.inf`
  sidecars if *Write .inf sidecar files when extracting* is on, dates from the disc where it has them). This
  works from read-only images too, and a drag only ever copies. The files are written to a temporary folder
  only when another program asks for them (on Windows, as soon as the drag passes over another program or the
  desktop); they are deleted when Disk Wrangler exits.
* Between copies of Disk Wrangler, and to other tools that support it, a drag also carries the files'
  attributes (load/exec, access, DFS directory, DOS attributes) in the open
  **[retro-files format](docs/retro-files-format.md)** (which also explains why it exists), so nothing is
  lost; `tests/retrofilescheck.py` checks a drag against that spec.
* ADFS tab: the whole disc. If $.DOS.DRIVE_C exists and is a FAT12/FAT16 volume, a
  second tab "DOS Plus (DRIVE_C)" appears showing C:\ - browse, drop files/folders in,
  New directory, Delete, Properties (rename 8.3 name, R/H/S/A), Extract.
* View (double-click a file, F3, or Edit > View): opens a window with Text, Hex dump
  or "BBC BASIC listing" (tokenised programs are detokenised, line numbers in
  GOTO/GOSUB decoded). The best view is picked automatically; the text is selectable
  (Ctrl+C) and there are Copy all / Save as text buttons. Works for files on either tab.
* F2 / Edit > Rename: rename the selected file or directory (ADFS: max 10 characters; DOS: 8.3).
  With the C:\ or $ root row selected, F2 renames the volume instead.
* Edit > Rename volume: ADFS tab sets the disc name (the root directory title shown by *CAT,
  max 10 characters; a disc name already stored in the free space map is kept in step).
  DOS tab sets the volume label (max 11 characters, blank removes it).
* Edit > Boot option: what SHIFT+BREAK does (*OPT 4): off, *LOAD, *RUN or *EXEC !BOOT. Works on ADFS floppies
  and hard discs (byte &FD of the free space map) and on each side of a DFS disc separately; the current
  setting is shown in the status bar ("Boot: *RUN !BOOT").
* Alt+Enter / Edit > Properties: ADFS = name, R W L E, load/exec; DOS = name and R H S A.
  Multi-select sets attributes in bulk.
* Ctrl+S saves.

## Truncated images
Some DFS images are saved with the blank tail trimmed off. These are padded with zeros to a full
disc in memory when opened, the status bar says so, Save is enabled, and a truncated image is
**never written back truncated** (this also applies to the other formats). ADFS images are only padded up to a *standard floppy size* (160K/320K/640K); an odd size claimed by an ADFS map is never used to grow a file.

## Safety
Every change (ADFS, DOS or DFS) is staged in memory. Ctrl+S first re-validates the ADFS free
space map (both checksums, run ordering/overlap/bounds) and the whole directory tree,
and - if a DOS volume or DFS catalogue was touched - that too (FAT chains/cross-links, or DFS catalogue entries, ordering and overlaps) (boot sector geometry, chain
integrity, no cross-linked or out-of-range clusters, chain lengths vs file sizes). It
refuses to write if anything is wrong, or if the map changed on disk since you opened the
image. Data is written first, directories next, the free space map last, then re-verified.
An image (or DOS partition) that fails validation on open is read-only.
DOS timestamps on new files come from your PC's clock.

## Notes
ADFS names: max 10 chars, no . : " # $ & % * @ \ ^ | or space. DOS names are converted to
upper-case 8.3 (clashes become NAME~1.EXT). NAME.inf sidecars (NAME LOAD EXEC LEN ATTR)
are read on ADFS add and written on ADFS extract (Tools menu). ADFS files need contiguous
space; max 47 entries per ADFS directory. Only DRIVE_C is looked for as the DOS partition.
The viewer shows the first 1 MiB of very large files in Text/Hex.
DFS: up to 31 files per side, names 7 characters plus a directory letter, one catalogue per side (Watford 62-file DFS is not supported).
Tests (tests/): viewtest (detokeniser), selftest (ADFS), dostest and floptest (FAT), dfstest (DFS), detecttest (format
detection), mkimg (makes blank ADFS test images), guitest and guitest2 (offscreen GUI, incl. multi-window copying),
sidetest (new images of every type, side order detection and conversion) with sidecheck.py (independent check),
acorncheck.py (independent check of ADFS maps, directories and side order, written from Acorn's ADFS ROM and b-em),
boottest (boot option), dragtest (dragging out) with retrofilescheck.py (checks a drag against the
retro-files spec), xdragtest (real drags on X11 under Xvfb).

## Licence
Copyright (C) 2026 Daniel Jameson. Licensed under the GNU General Public License v3.0 or
later - see LICENSE. Uses Qt 5 under the LGPL v3. No third-party code is included; what was consulted or used
for testing, and the licences a Windows build must ship, are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). The drag-and-drop format is shared with
[CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler).

## For developers / agents
See AGENTS.md (design rules, format notes, build and test instructions, pitfalls).
