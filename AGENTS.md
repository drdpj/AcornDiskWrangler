# AGENTS.md - working on Acorn Disk Wrangler

Read this before changing code. It records the design rules, the facts about the disc formats
(and how sure we are of each), how to build and test, and the mistakes already made once.

## What this is
A Qt 6 / Qt 5.15 (C++17) desktop tool for editing Acorn 8-bit disc images. It opens ADFS old-map hard
discs and S/M/L floppies, DFS (.ssd/.dsd), PC FAT floppies, Acorn DOS Plus 800K, the Acorn
640K ADFS/DOS hybrid, and the FAT16 DOS Plus partition stored in `$.DOS.DRIVE_C` on an ADFS
hard disc. One disc per window; files can be dragged between windows. GPLv3-or-later; the
copyright line in every header is `Copyright (C) 2026 Daniel Jameson`; put the same header on any
new source file (`#` comments for Python). The notice also appears in Help > About (`MainWindow::about`)
and the Windows version info (`QMAKE_TARGET_COPYRIGHT` in the `.pro`, `LegalCopyright` in the `.rc`). Build files
(`.pro`, `CMakeLists.txt`, `.desktop`, `.rc`, the SVG) carry a short form of it. **Anything taken from or closely
modelled on someone else's work goes in `THIRD-PARTY-NOTICES.md`** with its terms (and in About if it ships in the
program); material only consulted for facts goes in its "Consulted" table. Never commit third-party disc images
(or DOSBOOT): the tests read them from outside the repo. `docs/retro-files-format.md` is shared with CP/M Disk
Wrangler (github.com/drdpj/CPMDiskWrangler): keep the two copies identical.

## Source map (all in the repo root; tests in `tests/`)
| File | Role |
|---|---|
| `adfs.h/.cpp` | `Adfs::Image`: the staged sector store used by **every** format, plus the ADFS old-map filesystem (`FsMap`, `Dir`, `Entry`), validation, commit |
| `dosfs.h/.cpp` | `Dos::Volume`: FAT12/FAT16 inside an `Image` (BPB, table-driven `Dos::Format`, Acorn layouts); browse/add/mkdir/delete/rename/label/check |
| `dfs.h/.cpp` | `Dfs::Disc`: one side of a DFS disc (catalogue in sectors 0-1); a .dsd = two `Disc`s |
| `diskformat.h/.cpp` | `detectDisk(path, force)` -> `DiskInfo` (kind, full size, DFS layout, side layout + whether the side order was confirmed). Decides how a file is opened |
| `newdisc.h/.cpp` | `NewDisc::create()`: blank images of every format (File > New image); pure QtCore, checks its own output with `detectDisk`. The 640K DOS Plus disc needs a `bootSource` (an existing 640K disc to copy the boot track from, or a DOSBOOT file): DOSBOOT is Acorn's code and must never be put in the repo |
| `viewtext.*` | text / hex / BBC BASIC detokeniser (pure QtCore, no GUI) |
| `viewer.*` | the read-only viewer window |
| `appsettings.h` | where settings live: the system's place, or `<program>.ini` beside the program (portable; for an AppImage, beside the `.AppImage` file - `$APPIMAGE` - not in its temporary mount; a program installed on Linux has no portable mode, so File > Create local config file isn't shown). Use `AppSettings` instead of `QSettings` in program code (tests may use `QSettings` to clear the system store). Header only; shared with CP/M Disk Wrangler, keep identical |
| `buttonbar.h` | the main window's button bar: real QPushButtons kept in step with their QActions (a QToolBar's own buttons draw as flat text on macOS). Header only; shared with CP/M Disk Wrangler, keep identical |
| `fileopen.h` | macOS opens files from the Finder (double-click, Open With, the Dock icon) by sending the running program a `QFileOpenEvent`, never a command-line argument: `FileOpen::FileOpenFilter` (installed on the application in `main.cpp`) opens the image like File > Open - in an empty window, the active one first, else a new window; one already open just comes forward. Windows and Linux never send it. `guitest2` / CP/M Disk Wrangler's `guitest` send it. Header only; shared with CP/M Disk Wrangler, keep identical |
| `icon.*` | the application icon (a 5.25" floppy), drawn with QPainter; `icons/` holds PNG exports (`tests/mkicon <dir>` regenerates them) and a matching `acorndiskwrangler.svg` - keep the SVG in step if the geometry changes |
| `acorndiskwrangler.desktop` | Linux/BSD: name and icon for docks and menus (GNOME ignores the window icon and shows a gear without it). Installed with the hicolor PNGs + SVG by `make install` (qmake `INSTALLS`, CMake `install()`); matched to the windows by `StartupWMClass` (X11 `WM_CLASS` is `acorndiskwrangler`) and `QGuiApplication::setDesktopFileName` (Wayland). Check with `desktop-file-validate` |
| `icons/acorndiskwrangler.icns` | macOS: the .app's icon (qmake `ICON`, CMake `MACOSX_BUNDLE_ICON_FILE`). Made by `tests/make_icns.py` from `mkicon`'s PNGs, 32-1024 px, every size an exact rendering. Not tested on a Mac |
| `acorndiskwrangler.rc`, `icons/acorndiskwrangler.ico` | Windows only: the .exe icon in Explorer/taskbar. CMake compiles `acorndiskwrangler.rc`; qmake uses `RC_ICONS` in the `.pro` (it generates its own .rc, so the two are independent). Regenerate the .ico from the PNGs with Pillow if the design changes. Not testable on Linux |
| `mainwindow.h/.cpp` | main window, `DropTree`, open/save/refresh/updateUi, ADFS pane |
| `mainwindow_dos.cpp` | DOS pane, viewer hookup, renaming, import summary |
| `mainwindow_dfs.cpp` | window registry, tab management, the DFS pane (both sides in one tree), **cross-disc copy engine**, Copy / Paste |
| `mainwindow_p.h` | shared private header: item roles, `ImportCtx`, `Pane`, `XItem`, `DragRef`, name helpers |
| `main.cpp` | opens each command-line image in its own window; installs `fileopen.h`'s filter for the Finder's |

## The rules that must not be broken
1. **Nothing is written to the image except by `Image::commit()`.** All edits (ADFS, DOS, DFS)
   are *staged* as sector extents in `Image::m_pending` via `writeSectors`/`writeFileBytes`.
   Reads (`readSectors`/`readFileBytes`) overlay staged data on the file, so every layer sees
   its own pending changes. Never write to the `QFile` directly from a filesystem layer.
2. **Validate before writing.** `commit()` runs `validate()` (ADFS map + tree) and the *extra
   validator* (set by `MainWindow`: DOS volume and DFS catalogue checks, only when their area
   has pending changes). Any error vetoes the whole save. It also refuses if sectors 0-1 on
   disk changed since the image was opened. Write order: data, then directories, then
   sectors 0-1 last. Then it re-validates.
3. **Never save a truncated image - and never grow a complete one.** `Image::fullSize()` is the true disc
   size; a short file is padded in memory on open (`needsPadding()`), Save is enabled for pad-only, and
   `commit()` resizes the file to `fullSize` before writing. For ADFS, padding only happens up to a
   *standard floppy size* (160K/320K/640K); never trust an ADFS map's size claim to grow a file (see the
   hybrid gotcha below). A save with nothing changed must leave the file byte-identical.
4. **Every mutating operation is atomic**: take `Image::snapshot()`, do the work, `restore()` on
   failure (see `DOS_TXN` in `dosfs.cpp`, and the ADFS `addFile`/`makeDir`/... wrappers). A
   failed add (disc full, directory full) must leave no trace.
5. **A disc that fails validation on open is read-only.** The ADFS half of the 640K hybrid is
   locked (`Image::setAdfsLocked`) so ADFS edits cannot corrupt the FAT. Don't weaken this.
6. ADFS-only operations go through `Image::checkAdfs()`; raw containers (`openRaw`) never
   parse or write an ADFS map. `m_map.total` is reused as a bounds limit for raw images.

## Disc format facts (confidence noted)
- **ADFS old map** (verified against a real BeebSCSI image): sectors 0-1 = free space map;
  checksum = ADC-style over bytes 254..0, carry propagated, final carry dropped; root dir at
  sector 2 (5 sectors, "Hugo", 47 x 26-byte entries, 53-byte tail: name, parent, 19-byte
  title, end sequence). Dir sequence numbers are BCD; a new entry's seq = the parent's bumped
  seq. Names: max 10 chars, bit 7 of bytes 0-4 = R W L D E, terminated/padded by CR (junk may
  follow). Entries are kept sorted case-insensitively. Files are contiguous; best-fit
  allocation. Zero-length files use no sectors (real ADFS behaviour **unconfirmed**). The disc
  name limit is deliberately 10 chars (title field is 19 bytes, but >10 is unproven safe).
- **DFS** (verified with an independent Python reader): catalogue = sectors 0-1 per side,
  <=31 files, names 7 chars + directory letter (bit 7 = locked), 18-bit load/exec/length,
  10-bit start sector, entries kept in *descending start* order, BCD cycle number. .dsd
  interleaves by track (`phys = ((n/10)*2+side)*10 + n%10`); a sequential-side variant is
  detected too. 204800 bytes is ambiguous (80-track SSD vs 40-track DSD): the catalogue
  sector count and the second catalogue's plausibility decide, then the file suffix.
- **PC FAT**: standard; checked against `fsck.fat` and `mtools`. FAT12/16 chosen by cluster
  count. `Dos::Volume::attach` reads the BPB; `attachFormat` uses the table in `dosfs.cpp`.
- **New DOS Plus 640K discs** copy the real layout: track 0 side 0 = free space map (total 2720, no free runs),
  root "Acorn DOS Boot Disc" with one entry `dosboot` (WR, load/exec &04000000, length of the DOSBOOT file - 2303
  on the real disc - at sector 7), DOSBOOT in sectors 7-15; then the empty FAT at interleaved track 2. From a disc
  image the 4096-byte boot track is copied byte for byte (`sidetest` checks this against `M512_1.ADL`).
- **Acorn 640K ADFS/DOS Plus hybrid** (real Master 512 disc `M512_1.ADL`, 655360 bytes; **file content
  verified**: 39 files are byte-identical to the same files on the hard disc's `DRIVE_C`, and the edited
  image passes `fsck.fat` / `mtools` after de-interleaving). A *valid* ADFS catalogue (title "Acorn DOS
  Boot Disc", one file `dosboot` at sector 7, no free space) shares the disc with a DOS Plus FAT12 volume.
  - **Logical DOS layout** (256-byte sectors): 16 reserved sectors (= ADFS cat 0-6 + DOSBOOT 7-15),
    **FAT at logical 16-17** (1 copy, media FF), **root dir 18-31** (112 entries), data from **logical
    sector 32**, 8-sector (2048-byte) clusters, **316 clusters**, 2560 sectors.
  - **Track interleave (the bug that corrupted every file):** DOS Plus numbers sectors side 0 first
    (cylinders 0-79), then side 1, but the image file is stored cylinder by cylinder (side 0, side 1, ...).
    A track is 16 x 256 = 4096 bytes; **logical track t is at file track 2t (t < 80) or 2(t-80)+1
    (t >= 80)**. This is now done by `Adfs::Image` (`SideLayout::sideFirst`, the same as for ADFS L - see the
    side order notes below); `Dos::Volume` only ever sees the disc's own numbering. Detection
    (`diskformat.cpp`) looks for the FAT signature at *file* offset 8192 (= logical sector 16; 4096 in a
    sequential file).
  - **How it was found:** extract the DOS files from the hard disc's `DRIVE_C` (`tests/extract_hd_dos.py`),
    search the hybrid for each cluster's first 64 bytes, compare with the cluster the FAT says. Two earlier
    "verified" versions (16 reserved sectors from a table, then FAT at file offset 8192 read as sector 32)
    both passed every structural check and both read garbage data.
  - **Gotcha:** the ADFS map claims **2720 sectors** while the file holds 2560, so the ADFS map size must
    never be used to pad or size this disc (an earlier version would have grown the file by 40960 bytes).
    `Image::open(path, err, forceFullSize)` handles it; detection is by content (valid map + consistent DOS
    volume), not by the map size.
- **Acorn DOS Plus 800K** (1024-byte sectors, no boot sector, 1 FAT, 192 root entries, media FD): built
  verified against a real GEM disc (`M512_3.IMG`, 819200 bytes, label "M512 Disk 3"): logical layout is
  FAT at sector 0-1, root dir 2-7 (192 entries), data from **sector 8**, **1 sector (1024 bytes) per cluster**,
  792 clusters. The file is **interleaved** (the usual layout), and DOS Plus numbers this format's sectors in that
  same order (sector, side, track), so no remapping is needed - unlike the hybrid. (An earlier version of this
  note called it "sequential", meaning "no remap"; that was misleading.) ISSUE.BAT, GEM
  fonts (`DIBHSS07.FNT` header "Swiss") and the `.RSC` file all read correctly, FAT chain lengths match
  every file size, and after edits `fsck.fat`/`mtools` (via a prepended BPB sector) are clean.
  `floptest real800 <copy of the disc>` checks all of that. The image is not in the repo.
- **Side order of the image FILE** (Acorn double-sided floppies only: ADFS L, DFS .dsd, DOS Plus 800K / 640K.
  The owner's rule: **PC floppies (360K/720K/1.2M/1.44M) are always interleaved** - they get no `SideLayout`, so
  no detection, Read as, Convert or sequential creation; only the Acorn formats can be odd): *interleaved* = cylinder 0 side 0, cylinder 0
  side 1, ...; *sequential* = all of side 0, then all of side 1. Two separate things must not be mixed up,
  and `Adfs::Image` handles both (`SideLayout`, translated in `readRaw`/`writeRaw`; everything above `Image`
  sees the disc in its own sector numbering): (1) the file's side order (`sequential`); (2) how the disc
  numbers its sectors (`sideFirst`). **ADFS L and DOS Plus 640K number side 0 first** - the machine reads all
  of side 0, then side 1 - so even an interleaved file is always remapped (verified on real discs: the Master
  Welcome disc, Acorn User Tau85 and ARM Evaluation disc 4, from b-em's `discs/` and oaknut's test data: only
  this mapping gives a valid directory tree, and every BBC BASIC program on them reads intact end to end).
  **DOS Plus 800K (and PC FAT) number sector, side, track**, i.e. the same as an interleaved file (verified on
  the real 800K disc). A DFS .dsd's sides are separate discs, mapped by `Dfs::Disc` on top of an interleaved
  view (`sideFirst` false). Geometry (track bytes x cylinders): ADFS L / hybrid 4096 x 80, DFS 2560 x 40|80,
  Acorn 800K 5120 x 80.
  - **Bug fixed with this (owner, please note):** earlier versions read and wrote ADFS L floppies as if ADFS
    numbered sides alternately. Real .adl discs with data beyond the first track failed validation (and so
    opened read-only), and ADFS L discs created blank (tests/mkimg, or by hand) and then filled by those
    versions are self-consistent in the tool but scrambled on a real machine or an emulator.
  - **Detection** (`diskformat.cpp`) reads the disc both ways and keeps the order in which structures *beyond
    track 0* line up (track 0 side 0 is first in both orders, so the boot sector, ADFS map and side-0 DFS
    catalogue prove nothing): DFS - the side-1 catalogue at file sector 10 vs <sectors per side>; hybrid - the
    DOS FAT signature at file offset 8192 vs 4096; ADFS L - `validate()` of the directory tree; FAT - `check()`
    plus correct `.`/`..` in every subdirectory, then the order with **fewer lost clusters** wins (needed for
    1.44M, whose root directory is entirely on side 1 of cylinder 0: read the wrong way it is an empty, "valid"
    directory while the FAT says the disc is full). When both orders pass equally (blank discs; files only in a
    root directory on track 0) `sidesConfirmed` is false and the format's usual order is used: interleaved for
    every Acorn format (ADFS L, DSD, DOS Plus 800K and 640K - b-em's `src/sdf-geo.c` geometry table and MAME's
    Acorn formats agree). That ADFS numbers an L disc's sectors side 0 first comes from Acorn's own floppy driver
    (ADFS 1.30 `floppy.asm` in dominicbeesley/ADFS-multi-target: track = sector / 16, track >= 80 is side 1). The owner's rule: **never** fall back on an order used before for that file - no
    per-file memory. `sidetest` asserts the rule that matters: detection may say it can't tell, but it must
    **never be sure of the wrong order**.
  - **Conversion** (`Image::setConvertOnSave`) is staged like any edit (`dirty()`); `commit()` rewrites the whole
    file from the interleaved view (pending edits included), sectors 0-1 last. Converting and converting back
    must give a byte-identical file (`sidetest`, also on the real discs).
- **A damaged ADFS map** with a valid root directory is still detected as ADFS (and opens read-only) so that it is
  read with the right geometry - an ADFS L floppy needs its side-first numbering even to be inspected
  (`dragtest` caught this: before, such a disc read scrambled beyond the first track).
- **ADFS D/E (new map)** is detected and refused; Watford 62-file DFS is not supported.
- **Acorn Z80 CP/M discs** carry a DFS catalogue on side 0 (title "Acorn CP/M", one locked file `$.CPMDISC` from
  sector 2 to the end of the side; seen on a real disc) so that DFS leaves them alone. Detection reports them as
  `AcornCpm` and Open refuses them, pointing at CP/M Disk Wrangler: editing one as DFS, or writing on side 1 (no
  catalogue), would destroy the CP/M files. CP/M Disk Wrangler writes the same catalogue on new Acorn Z80 discs.
- **Torch CP/N discs** (Torch Z80 second processor; 409,600 bytes, cylinder by cylinder) have no DFS catalogue; they
  are recognised by cylinder 0 side 1: the allocation map in sectors 6-7 (every word's top six bits set, tracks 0-1
  in use, words 160-255 FC00 or FFFF) and the formatter's bytes in sectors 8-9
  (all but sector 9's byte 254, which varies). Detection reports `TorchCpn` and Open
  refuses them, pointing at CP/M Disk Wrangler, which reads and writes them (checked on five of the owner's real
  discs, which detected as Unknown here before).
- The BBC BASIC token table and line-number encoding (`viewtext.cpp`) are from memory,
  checked on hand-made programs and one real program; report oddities rather than guess.

## Behaviour details (moved from the README)
User-visible behaviour the README no longer spells out:

* **Side order** that can't be told (a blank ADFS L or DOS Plus 800K disc, or one with only a few files in the
  root) falls back to the format's usual order - interleaved for every Acorn format - and the status bar says
  "Sides: interleaved (assumed)". *Read as* applies to this window only; the next open detects again.
* **Saving**: data is written first, directories next, the ADFS free space map last, then the image is read back
  and verified. Save is refused if the map changed on disk since the image was opened.
* **DOS names** that clash after conversion to 8.3 become `NAME~1.EXT`; new DOS files get the host clock's time.
* **New DOS Plus 640K disc**: ADFS free space map of 2720 sectors with none free, root "Acorn DOS Boot Disc"
  holding DOSBOOT, then an empty DOS Plus FAT and root. A DOSBOOT file may be at most 2304 bytes; from a disc
  image the whole boot track is copied exactly.

## GUI architecture
- Boot option (Edit > Boot option): `MainWindow::bootOption()/setBootOption()` act on the current pane -
  `Image::setBootOption` (ADFS floppy or hard disc: map sector 1 byte &FD, staged via `stageFsMap`, which
  recomputes both checksums) or `Dfs::Disc::setBootOption` (catalogue sector 1 byte 6 bits 4-5, per side).
  DOS panes have none; the locked ADFS side of a 640K DOS Plus disc refuses it. Note: every map save clears
  unused free-list slots past the end-of-list pointer (a real hard disc had a stale entry there) - harmless,
  ADFS ignores them.
- Host drops of disc images (`MainWindow::dropImages`, called first by every pane's host-file drop handler; the same
  behaviour as CP/M Disk Wrangler): an empty window opens every dropped file (first here, rest in
  `openInNewWindow`); otherwise files `detectDisk` recognises are opened in new windows, except on a hard disc
  (`isHardDisc()`: ADFS over 640K, any of its panes) where one question asks Open / Copy onto this disc / Cancel.
  The rest are imported as before. All queued (no dialog or window inside the drop handler). An empty window also
  ignores the retro-files JSON and takes the files, so it can open an image dragged out of another window. Add
  files never opens images.
- Save as (`saveAsDialog`): with unsaved changes, `Image::moveToCopy()` copies the file to the new name and reopens
  it there, keeping the staged changes, then `save()` writes them into the copy (the original is untouched). With
  none, or a read-only image, it copies the file and opens the copy. `guitest2` checks both the copy (and a change
  made before it) and the untouched original.
- Side order (Tools > Side order): *Read as* re-opens the file with `openImage(path, 0|1)` (nothing written);
  *Convert* stages `Image::setConvertOnSave`. The status bar shows "sides ..." with how it was settled.
  New image: `newImageDialog()` -> `NewDisc::create()` -> opened like File > Open.
- One `MainWindow` = one disc. `m_img` is the `Image`. Tabs (`m_tabs`) are managed only by
  `ensureTab()` in canonical order ADFS, DOS, DFS; which exist depends on
  `m_info.kind`. `refreshTree()` is the single refresh entry point (it calls `refreshDos()` and
  `refreshDfs()`); call it after anything that changes the disc.
- **Every refresh deletes and rebuilds all tree items.** Never hold a `QTreeWidgetItem*` across
  an operation that refreshes (this caused a use-after-free in a test); keep names/sector
  numbers instead. Items carry `RoleParent/RoleName/RoleIsDir/RoleSector/RoleIsRoot`
  (see `mainwindow_p.h`): parent dir sector/cluster, name, is-dir, own sector/cluster or start
  sector, "this is the root row". DFS items use name `"D.NAME"`.
- **DFS: both sides in one tree** (`m_dfsTree`), a root row per side ("Drive 0", "Drive 2"), the way CP/M Disk
  Wrangler shows a disc with a filesystem on each side (the owner asked for this so files can be dropped from one
  side onto the other). Every DFS row carries `RoleSide`, and `RoleParent` = its side, so a drag's or copy's
  `DragRef::parent` says which side (`exportRefs` reads it; the tree's own pane is `PaneDfs0`). `dfsSide()` is the
  side of the selection (else the current row) while the DFS tab shows, -1 otherwise; `dfsTargetSide(row)` and
  `dropPane()` turn a drop row into `PaneDfs0/1`. `dfsSelected()` covers both sides, so Delete, Properties, View and
  Extract work across them (Extract puts each side in a `Drive N` folder when both are involved). A drag within
  the tree is accepted only on a two-sided disc and only from one side to the other (`DropTree::acceptable`, then
  `onInternalDrop` drops the refs already on the target side). `refreshDfs()` keeps the selection, current row and
  collapsed sides across its rebuild.
  Naming (owner's rule): "side" is always 0/1 (the disc's physical side, as in side order); "drive" is always
  0/2 (what a BBC Micro calls the two sides of a disc). Never print a side number next to "drive" or the reverse.
  Moving several files into a directory (Properties with more than one file selected: the letter field, blank =
  unchanged) is a rename, `dfsMoveSelected()`: every new name is checked first, against files not being moved and
  against the other selected files (keys from `dfsKey`, so case-insensitive, as DFS itself is - `d.moo` and `D.MOO`
  are one file; the letter's case as typed is kept). With a clash nothing moves unless the user chooses "Move the
  others". Locked files move too (as for a single-file rename here).
- Slots (`deleteSelected`, `editProperties`, `renameSelected`, ...) dispatch on the active pane:
  `dfsSide()` (>=0), `dosActive()`, else ADFS. `updateUi()` enables actions per pane kind.
- **Menus, button bar and context menu are laid out like CP/M Disk Wrangler's** (owner's rule: the same order in
  both programs; one program's extra items may sit in between). File: New window (Ctrl+Shift+N), New disc image
  (Ctrl+N), Open | Save, Save as (Ctrl+Shift+S), Revert | Create local config file | Close (Ctrl+W), Quit (all windows). Edit:
  View (F3), Extract to folder (Ctrl+E) | Add files (Ctrl+Shift+F), [Add folder (Ctrl+Shift+D), New directory
  (F7)] | Copy, Paste (the platform's keys: `QKeySequence::Copy/Paste`) | Rename (F2), Properties (Alt+Return),
  Delete (`deleteKeys()`: Delete, plus Cmd+Backspace on macOS) | the disc's own items. Tools: Check disc (Ctrl+K)
  first, the program's own tools, then the options with *Keep a .bak copy when saving* last. Buttons: View,
  Extract, Add files, [Add folder, New dir], Rename, Properties, Delete. The context menu is the Edit menu's first
  four groups. `guitest2` and CP/M Disk Wrangler's `guitest` assert these; change both programs together.
- **Window title and status bar** as CP/M Disk Wrangler's: `<file> * - Acorn Disk Wrangler` (`no disc - ...` when
  empty; `*` = unsaved changes or padding). The status bar's left label (`m_status`, stretched) shows the disc and
  its space, then `  -  `-separated extras (side order, `read-only: <readOnlyReason()>`, padding); with no disc it
  says to open or drop an image. Short messages use `statusBar()->showMessage`, which covers the label for a while.
- **Copy and Paste** (`copySelected`, `paste` in `mainwindow_dfs.cpp`; the same design in CP/M Disk Wrangler): Copy
  exports the selection at once (`exportRefs`) and keeps the `XItem`s, so a paste gets what was selected then even
  if the source has changed or closed. The clipboard gets a `RetroFiles::DragOutMime` whose exporter returns that
  copy (files written to a drag folder only when another program asks: text/uri-list, the retro-files JSON and,
  `setForClipboard`, GNOME's `x-special/gnome-copied-files`), plus `WRANGLER_MIME_CLIP` = `pid:n`. Paste
  (`pasteItems`): our own token -> `importX` from the kept copy (nothing written); else retro-files JSON ->
  `onRetroFilesDrop` (another copy of this program, CP/M Disk Wrangler); else local files from a file manager
  (URLs or GNOME's list) -> the pane's `...ImportPaths`, like Add files. It goes where Add files would (current
  row's directory, or the DFS side in use). At exit a clipboard we still own is cleared (its files are deleted
  with the drag folders). Checked across real processes on X11 (Xvfb + xclip: CP/M copy -> Acorn paste, a
  GNOME-style file list -> paste); Windows Explorer and macOS Finder are not tested.
- **Copying between discs** (`mainwindow_dfs.cpp`): source pane -> `exportRefs()` -> neutral
  `XItem` tree (data + attributes) -> `importX()` -> `importXAdfs/Dos/Dfs`, which convert names
  and attributes and reuse the normal add paths (`addOne`, `dosAddOne`, `dfsAddOne`) so the
  overwrite prompts and error reporting behave identically to host drops. DFS -> ADFS: a file whose
  DFS directory isn't `$` goes into a subdirectory of the drop target named after the letter (found or
  created; a *file* of that name in the way is an error, never renamed around). This is in
  `importXAdfs`, so the in-process and the retro-files JSON paths both do it. ADFS -> DFS: folders are
  flattened, but a one-character ADFS directory name (valid as a DFS directory) prompts "keep letter?"
  (Yes/Yes to all/No/No to all/Cancel; `ImportCtx::letterPolicy`); on Yes its files, including those in
  its subfolders, go into that DFS directory (case kept). Single-letter *files* are never asked about.
  DFS -> DOS still keeps the name only. Drags carry only
  `(window id, pane, parent, name)` in a custom MIME type; the target looks up the source
  window in `MainWindow::windows()` (same process). The drop is queued (`QTimer::singleShot`)
  so no modal dialog runs inside the drop handler.
- **Dragging out** (`dragout.h/.cpp`, spec in `docs/retro-files-format.md` - keep the two in step): `makeMime`
  returns a `RetroFiles::DragOutMime` carrying the private formats *and* `text/uri-list` + the retro-files JSON.
  Those two are produced lazily in `retrieveData()`: the selection is exported (`exportRefs`) and written by
  `writeHost()` (same names/.inf as Extract to host; clashing names get `_2`; Windows device names and trailing
  dots made safe in `hostSafeName`) into a `QTemporaryDir` kept until exit (`cleanStale()` at start-up removes
  ones older than a day). Rules: (1) `acceptable()`/`dropEvent()` must only look at format *names*, never ask
  for a drag's URLs or JSON unless it is about to import them - asking makes the source write its files.
  (2) The private tag is `pid:window:pane`; `fromThisProcess()` decides a direct copy, because window ids are
  addresses and another copy of the program can have a window at the same address. (3) A drop from another
  process / tool goes through `onRetroFilesDrop` -> `RetroFiles::readJson` (path-safety checked) -> `importX`,
  so it behaves exactly like a copy between windows. (4a) The owner's rule for the JSON: **no redundant keys**. A
  DFS item's `name` is `D.NAME` (the letter is part of its identity; no `dfs` namespace), and there is no
  file-type key (RISC OS types live in `load`/`exec`; the spec only explains the encoding). Agreed after
  feedback from oaknut's author (GitHub issue #1): `access` uses the Stardot `.inf` letters, case-sensitive
  (`RWEL` owner, `rwel` public, no `D`); names carry the disc's bytes one character per byte (U+0000-U+00FF,
  what `fromLatin1`/`toLatin1` do), never converted; a load that might be a RISC OS stamp is treated as an
  address when unsure and **never changed on a guess** (so ADFS -> DFS keeps the low 18 bits of a stamped
  file's load/exec instead of zeroing them). Disc-level properties (DFS title, boot option) are out of scope
  for drags. **Windows**: Qt answers a drop target's "can you give me
  files?" (QueryGetData / EnumFormatEtc for `CF_HDROP`) by calling `urls()`, so the files are written when the
  drag first passes over another program or the desktop, not at the drop (checked in qtbase 5.15
  `qwindowsmime.cpp`); in-process drops use the source `QMimeData` directly and write nothing. (4)
  `DropTree::startDrag` runs its own `QDrag` so it knows the result: "Dragged out" is reported only if the files
  were written **and** the drop was accepted (the owner saw the message after a drag that passed over Explorer
  and was let go back in the window). (5) Never delete the `QDrag` after `exec()`: on X11 the target fetches the
  data after `exec()` returns and Qt deletes the drag itself (`xdragtest` caught this). Not testable here: a real
  drag into Explorer (ask the owner).
- To add an operation to a pane kind: add the pane-specific function, add a dispatch line at
  the top of the slot, extend `updateUi()`, and add a GUI test. To add a new disc format: add
  a `DiskInfo::Kind`, detect it in `diskformat.cpp`, open it with `Image::openRaw`, add a
  filesystem class in the style of `Dfs::Disc`/`Dos::Volume` (staged I/O only, snapshot-
  guarded mutations, a `check()`), hook its `check()` into the extra validator in the
  `MainWindow` constructor, then add panes and cross-copy cases.

## GUI layout conventions
- Every tree is set up by `setupTreeColumns()` (`mainwindow_p.h`) - don't configure a header by hand.
  The **Name column stretches**; every other column has a width computed from the font (no fixed pixel
  widths), so the last column always meets the right-hand edge in every pane, and a minimum width
  keeps Name readable. Columns shared between panes use the same sample text so they line up:
  ADFS = Name Attr Load Exec Length Sector "Type / date"; DFS = the same without "Type / date";
  DOS = Name Attr Size Cluster Modified (numbers, then the date, as in ADFS).
- Numeric columns are right-aligned **and so are their headers**; fill them with `setNumCell()`, which
  adds the same two-space pad as the header so a number never touches the next column. These are
  display strings only; real values live in the item roles - never parse the cell text.
- Root rows use `rootLabel()`: `$   [title]`, `C:\   [label]`, `Drive 0   [title]`.
- An action that works only on some kinds of disc says so in its tool tip and status tip (New directory: ADFS and
  DOS, not DFS); the button bar shows the tip on a greyed-out button too.
- Toolbar buttons use short `setIconText` labels so the bar fits without the ">>" overflow; menu texts
  stay descriptive. Healthy-but-unusual discs (e.g. the 640K hybrid) must not report "warnings".
- Check any layout change visually: `tests/screenshot <outdir> <dsd> [<image> ...]` renders the DSD
  (with files dropped on it) and every tab of every further image, at 1300px and at the default window
  size (`*_narrow.png`), offscreen, on copies of the images.

## Build and test

**Version.** `version.pri` (qmake) and `version.cmake` (CMake) compile in `git describe --tags --always --dirty`
as APP_VERSION, shown in Help > About; "unknown" without git, or set it with `qmake APP_VERSION=...` /
`-DAPP_VERSION=...`. It is read when qmake/CMake runs, so re-run them after a new commit or tag. Both files are
shared with the other wrangler.

How to build on each platform and how releases work, for people: `docs/building.md` (shared); a 32-bit Windows 7
build (Qt 5.15, MinGW 8.1 32-bit; not tried yet): `docs/building-windows-32bit.md` (shared).

**Release builds and licences.** `.github/workflows/build.yml` builds Windows, macOS and Linux packages, linked to
Qt's shared libraries (keep it that way: the LGPL lets users replace them). Each is built twice: `ci/build.sh`
builds and deploys, `ci/collect-licences.sh` gathers the licences of every shipped library into
`licences-bundled.qrc`, and `ci/build.sh` again compiles them in (Help > Licences, `licences.cpp`). On Linux, `ci/make-deb.sh` then packages the same build as a `.deb` that depends on the system's Qt
(dpkg-shlibdeps; built on 22.04 against Qt 6.2, it installs on Ubuntu 22.04+ and Debian 12+ - the t64-renamed
libraries of 24.04 provide the old names; checked in clean 22.04-built / 24.04-installed chroots, Qt 6 and Qt 5),
and the workflow installs it and starts it. On Windows, for a tag or Run workflow only, `ci/installer.iss` (Inno
Setup) packages `deploy/` as `...-setup.exe` and the workflow installs, starts and uninstalls it; each program's
AppId in it is fixed for good. A new source
file in the .pro also goes in CMakeLists.txt and any `tests/*.pro` that build `mainwindow.cpp` (they need
`../licences.cpp`). `ci/`, `licences.*`, `licences/`, `buttonbar.h`, `appsettings.h`, `fileopen.h` and the workflow are shared with CP/M Disk Wrangler: keep them
identical.
```
qmake acorndiskwrangler.pro && make          # or CMake (CMakeLists.txt)
```
Needs Qt 6 or Qt 5.15 widgets; both must keep building (qmake picks whichever `qmake` you run, CMake prefers
Qt 6, `-DUSE_QT5=ON` forces 5). Where the two differ the code uses `#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)`:
`DropTree::mimeData` (list by reference in 6), `DragOutMime::retrieveData` (QMetaType in 6) and `dropPos()`
(QDropEvent::position() in 6). Each test is its own project in `tests/`: `mkdir b && cd b && qmake ../tests/X.pro && make`.

| Test | Args | Needs |
|---|---|---|
| `viewtest` | none | nothing |
| `settingstest` | none | nothing (offscreen): where settings live on Linux - an installed program has no portable mode; an AppImage keeps `acorndiskwrangler.ini`-style files beside the `.AppImage` file. Shared with CP/M Disk Wrangler |
| `mkimg` | `<outdir>` | writes blank ADFS S/M/L, a truncated L, a fake ADFS D/E, a junk file |
| `detecttest` | image paths | prints the detected kind for each (eyeball it) |
| `floptest` | `plainl <blank ADFS L>` (must not detect as hybrid) / `bpb <img>` / `table <img> <idx 0-3>` / `acorn800 <out>` / `hybrid640 <out>` / `real800 <copy of a real 800K disc>` / `real640 <copy of a real hybrid disc> [<dir of files from extract_hd_dos.py>]` | PC images via `mkfs.fat -C x.img 360\|720\|1200\|1440`; the Acorn modes create their own |
| `dfstest` | `<img> ssd\|dsd <sectors-per-side>` | DFS images (see below) |
| `selftest` | `<hard-disc img> [<copy with a disc name in its map>]` | **the owner's real `scsi0.dat`**; make the 2nd arg with `make_testdata.py mapname <copy>` |
| `dostest` | `<hard-disc img>` | **the real `scsi0.dat`** (has `$.DOS.DRIVE_C`) |
| `screenshot` | `<outdir> <dsd> [<image> ...]` | renders PNGs of each pane for eyeballing (see GUI layout conventions); host files from `make_testdata.py host` |
| `guitest` | `<hard-disc img>` | **the real `scsi0.dat`**; run with `QT_QPA_PLATFORM=offscreen` |
| `guitest2` | `<dir>` [`<copy of a real 640K hybrid>`] containing `a.dsd b.adl c.img d.dat e.ssd f.hybrid g.a800` | copies of a blank DSD, ADFS L, PC 720K, **the real hard disc**, a truncated SSD, the hybrid and Acorn800 images; host files in `/tmp/drop/proj` (`make_testdata.py host`); offscreen. Also checks the menu and button order, the title and status bar, and Copy / Paste with the real keys (`QTest`, so the `.pro` has `QT += testlib`) |
| `sidetest` | `<outdir>` [`<copy of a real 640K hybrid>` [`<copy of a real 800K disc>` [`<copies of real ADFS L discs, named t2_il_<name>.adl in <outdir>>` ...]]] | nothing else (a made-up DOSBOOT is used without the real hybrid; with it, new 640K discs are compared with it): makes every format with `NewDisc` in both side orders, fills, detects, converts both ways, reads in the wrong order, drives MainWindow (offscreen). The real discs are converted and back (must stay byte-identical). Then run `tests/sidecheck.py <outdir>`: an independent reader (own de-interleave, own ADFS/DFS parser, `fsck.fat` + `mtools` for FAT, with the Acorn formats wrapped in a BPB) |
| `detecttest` | (see above) | also prints each image's side order and whether it could be told |
| `dragtest` | `<outdir>` | nothing else: makes its own discs. Drives the drag data as Explorer / another instance / another tool would: lazy writing, names, .inf, dates, JSON, direct copy in-process, JSON path cross-process with attributes kept (ADFS, DFS, DOS), DFS directory letters -> ADFS subdirectories (drop on a directory row, reuse, JSON path), single-letter ADFS directories -> DFS letters (Yes/No/Yes to all via JSON/Cancel), disc images dropped (empty window, floppy, hard disc Open/Copy/Cancel/two images), self-drop refused, a foreign (cpm) list, unsafe lists refused, read-only image, stale cleanup. Then run `tests/retrofilescheck.py <outdir>/drag-*` (independent check against the spec) |
| `xdragtest` | `<outdir>`, run as `xvfb-run -a -s "-screen 0 1280x800x24" xdragtest <outdir>` | Linux/X11 only, needs Xvfb and xdotool. Real drags with the real pointer to a stand-in Explorer in another process: dropped (files arrive, message), passed over and let go back in the window (no message), refused (message not reliable on X11 - printed only) |
| `acorncheck.py` | images ... [`--sequential` images ...] | independent ADFS/DFS checker, see "Verify independently" below |
| `boottest` | `<outdir>` [`<copy of a hard disc image>`] | ADFS S/M/L and the hard disc: options 0-3 set, saved, re-read, only byte &FD (+ checksums) changed; DFS sides independent; refusals; MainWindow per pane; a 640K disc with an earlier version's sector-1 check byte (00) is corrected. `tests/acorncheck.py` re-checks the checksums independently |

`selftest`, `dostest`, `guitest` and `guitest2` assert details of the owner's image (12 root
entries, 69 DOS root entries, `BeebSCSI`, `!Config`, ...). That image is **not** in the repo; if
you don't have it, those tests cannot run - say so rather than loosening them. Tests modify
the image they are given, so always run on a **copy** (it is 512 MB: delete copies afterwards;
the sandbox disk filled up once).

`tests/make_testdata.py` generates the rest (stdlib only; see its docstring): blank, truncated
and sequential-side DFS images, the PC floppies (via `mkfs.fat`), the host files `guitest2`
drops, and the map-name variant for `selftest`. ADFS floppies come from `tests/mkimg`, the
Acorn 800K/hybrid images from `floptest acorn800|hybrid640 <out>`.

**Verify independently, not just with our own code.** For FAT use `fsck.fat -n` and `mtools`
(`MTOOLSRC=/dev/null mdir -i img ::`); for ADFS and DFS run `tests/acorncheck.py` on real discs (b-em's `discs/`,
the owner's) and on everything the tests wrote. It is written only from Acorn's ADFS ROM (map checksum routine,
floppy sector numbering), b-em's image layouts and the RISC OS Open old-map description - not from this program -
and walks every directory and checks BBC BASIC programs read intact, so a wrong side order fails on any disc with
data beyond the first track. Our own reader agreeing with our own writer proves little: the map checksum started
at 0 instead of Acorn's &FF through all the earlier testing, which only matters for an all-zero sector 1 (new 640K DOS Plus
discs made from a DOSBOOT file got 00 where ADFS expects FF), and only acorncheck found it.
At the owner's request, Disc Image Manager is not used as a source or reference.

## Testing GUI code (things that bit us)
- Synthetic drops must send `QDragEnterEvent`, then `QDragMoveEvent`, then `QDropEvent` to the
  tree's **viewport**; a lone drop event is ignored by Qt.
- `QTreeWidget::mimeData` drags need `supportedDropActions() == Copy`, else Qt "moves" (and
  removes) the source items after the drop.
- Closed windows/viewers use `deleteLater`: call `QCoreApplication::sendPostedEvents(nullptr,
  QEvent::DeferredDelete)` before counting children. `scrollToItem` before computing an
  item's rect or the drop lands on the root.
- Modal `QMessageBox`/`QInputDialog` inside a test block forever unless something answers them. In `guitest2`, use
  `whenModal<T>()` (it polls every 50 ms, up to 3 s, for the dialog to appear) or `autoYes()`, built on it: a fixed
  `QTimer::singleShot` delay made the test fail now and then, when a dialog appeared after the timer had fired
  (`dragtest` still uses its own `answer()`). `importSummary` only pops a box when there are errors, so a test that
  provokes errors must handle it.

## Working practices (learned the hard way)
- **Check what already exists before editing.** Twice, a patch was applied on top of work that
  was already present and produced duplicate declarations. `grep` for the symbol first; apply
  multi-file patches atomically (assert every target text occurs exactly once, write only if
  all assertions pass).
- Keep the staging model; don't add "quick" direct writes. Keep user-facing limits conservative
  (e.g. 10-char ADFS disc name) unless verified.
- After any change run the whole suite from a clean unpack of the zip and the independent
  checks above. When a test fails, find out whether the code or the test is wrong, and say so.
- State plainly what is unverified (zero-length ADFS files, BASIC
  tokens) in any summary to the owner.

## Known gaps / ideas
ADFS D/E; Watford DFS; compacting a fragmented ADFS/DFS disc; Cut (a move
off a disc) is deliberately not offered.

## Lesson: test against real discs as soon as you can
The 640K hybrid was implemented from a table and passed every synthetic test (they all used the same
wrong assumption), yet the first real disc showed bugs: the FAT offset, a padding rule that would have
corrupted the file, and (found only when the owner opened a file) the track interleave, which made every
file read as garbage although the FAT and directory were perfectly valid. Synthetic images prove
self-consistency only, and **a valid directory/FAT does not prove the data is where you think it is:
always compare file CONTENT with a known-good copy** (`floptest real640 <copy> <dir from
tests/extract_hd_dos.py>`). The same thing happened again with ADFS L: synthetic images were consistent with the
wrong sector numbering, and only real .adl discs (b-em `discs/`, oaknut `tests/`) showed it - check new disc
formats against real images from those repos before trusting them. The real disc is not in
the repo (it contains Acorn's DOS Plus software), so `floptest real640` and `guitest2`'s optional
argument take its path; ask the owner for one if you need to change anything hybrid-related.
