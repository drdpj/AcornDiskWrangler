# AGENTS.md - working on Acorn Disk Wrangler

Read this before changing code: what the program is, where things are, the rules that must not be broken, how
the window works and how to build and test. Two companion documents hold the detail you only need for some work:

* **[docs/disc-formats.md](docs/disc-formats.md)** - the facts about each disc format, how sure we are of each, side
  order, and how they were checked. Read it before touching `adfs`, `dfs`, `dosfs`, `diskformat` or `newdisc`.
* **[docs/testing.md](docs/testing.md)** - every test, what it needs, the test data and the independent checks.

## What this is
A Qt 6 / Qt 5.15 (C++17) desktop tool for editing Acorn 8-bit disc images: ADFS old-map hard discs and S/M/L
floppies, DFS (.ssd/.dsd), PC FAT floppies, Acorn DOS Plus 800K, the Acorn 640K ADFS/DOS hybrid, and the FAT16 DOS
Plus partition in `$.DOS.DRIVE_C` on an ADFS hard disc. One disc per window; files can be dragged between windows.
Its sibling is CP/M Disk Wrangler (github.com/drdpj/CPMDiskWrangler): the two look and behave alike and share some
files (see Build).

* **Licence and headers.** GPLv3-or-later. Every source file starts with the header naming
  `Copyright (C) 2026 Daniel Jameson` (`#` comments for Python); put it on any new file. The notice also appears in
  Help > About (`MainWindow::about`) and the Windows version info (`QMAKE_TARGET_COPYRIGHT` in the `.pro`,
  `LegalCopyright` in the `.rc`); build files (`.pro`, `CMakeLists.txt`, `.desktop`, `.rc`, the SVG) carry a short form.
* **Other people's work.** Anything taken from or closely modelled on someone else's work goes in
  `THIRD-PARTY-NOTICES.md` with its terms (and in About if it ships in the program); material only consulted for facts
  goes in its "Consulted" table. Disc Image Manager is not used as a source or reference (the owner's wish).
* **Never commit third-party disc images** (or Acorn's DOSBOOT): the tests read them from outside the repo.
* **`core/` is the git submodule DiskWranglerCore**, shared with CP/M Disk Wrangler (clone with `--recurse-submodules`, or
  `git submodule update --init`). Never edit files under `core/` from here: change DiskWranglerCore, tag it, and move the
  submodule to the tag. The retro-files specification is `core/docs/retro-files-format.md`.

## Source map (program in `src/`, tests in `tests/`)
| File | Role |
|---|---|
| `adfs.h/.cpp` | `Adfs::Image`: the opened disc used by **every** format - a DiskWranglerCore `Container` (raw, ImageDisk, DSK, TeleDisk) seen through a `Linear` with the side order - plus the ADFS old-map filesystem (`FsMap`, `Dir`, `Entry`), validation, commit |
| `core/diskwrangler/device/` | DiskWranglerCore's device layer: `Container` (the image file, staged writes, the checked save), `Linear` (the disc in its own numbering, slices), `SideOrder` (was `Adfs::SideLayout`; the old name stays as a `using` for the tests) |
| `core/diskwrangler/fs/fat/` | DiskWranglerCore's FAT module (was `dosfs.cpp`): `dw::fat::Volume` (still `Dos::Volume` here, through `src/dosfs.h`): FAT12/FAT16 on a `Linear` - an `Image`'s, a whole floppy or the DRIVE_C file in it (BPB, table-driven `Format`, Acorn layouts); browse/add/mkdir/delete/rename/label/check; `fatprobe` (detection), `fatvolume` (the `dw::fs::Volume` adapter) |
| `core/diskwrangler/fs/dfs/` | DiskWranglerCore's DFS module (was `dfs.cpp`): `dw::dfs::Disc` (still `Dfs::Disc` here, through `src/dfs.h`): one side of a DFS disc on an `Image`'s `Linear` (catalogue in sectors 0-1, plus 2-3 on a Watford 62-file disc); a .dsd = two `Disc`s. Also `.inf` sidecars (`inf.h`), addresses, the directory-letter mapping for copies to ADFS (`placeLetter`), Acorn Z80 CP/M's catalogue, `dfsprobe` (detection), `dfsvolume` (the adapter) |
| `diskformat.h/.cpp` | detection by tiers (`core/diskwrangler/fs/detection.h`): probes for ADFS and Torch CP/N here, FAT and DFS from the core; `detectCandidates(path, force)` -> `dw::fs::Verdict` (open, ask which, or hand to CP/M Disk Wrangler), each candidate's data a `DiskInfo` (kind, full size, DFS layout, side layout + whether the side order was confirmed); `detectDisk` = the best one's. Decides how a file is opened. `tests/legacydetect.cpp` is the detection before step 4, for `agreetest` |
| `adfsvolume.h/.cpp` | DiskWranglerCore's filesystem interface: `AdfsVolume` (an `Image` as a `dw::fs::Volume`) and `Adfs::OpenedDisc` (an opened disc as a `dw::fs::Disc`: ADFS + DOS, DFS drives 0 and 2, or FAT). `OpenedDisc` is the window's document (`dw::app::Document`) and `acorndw`'s opened image alike, so both save through the same checks: the volumes a change touches, the side order (`setConvertSides`), Save As into another kind (`writeCopy`, also `acorndw convert`) |
| `newdisc.h/.cpp` | `NewDisc::create()`: blank images of every format (File > New image); pure QtCore, checks its own output with `detectDisk`. The 640K DOS Plus disc needs a `bootSource` (an existing 640K disc to copy the boot track from, or a DOSBOOT file): DOSBOOT is Acorn's code and must never be put in the repo |
| `mainwindow.h/.cpp` | the main window on DiskWranglerCore's window kit (`MainWindow` derives from `dw::ui::DiscWindow`; see *GUI architecture*): `DropTree` (the kit's `VolumePane` plus drag and drop), the panes and their rows (`paneLayout`, `decorateRow`), opening, Save / Save As / Revert, side order, boot option, disc name, Check, New directory, Delete, Extract, ADFS Properties |
| `mainwindow_dos.cpp` | the DOS pane: its selection, DOS Properties, Rename and the viewer |
| `mainwindow_dfs.cpp` | the DFS pane (both sides in one tree), DFS Properties and the letter mover, copying out (`exportForCopy`, `exportRefs`), **every import** on the kit's import session (`importAll`, `importHooks`, `askReplace`, `importSummary`), drops between windows, Copy / Paste |
| `mainwindow_p.h` | shared private header: item roles, `Pane`, `DragRef`, `XItem` (kept for the tests), `fmtSectors` |
| `hostpolicy.h/.cpp` | QtCore only, shared with `acorndw`: the `.inf` reader, `acornHostRead` (how the core's one host reader, `dw::xfer::readHost`, reads files for this program: `.inf` sidecars by Tools > Read .inf files / `--sidecars`), and `RetroFiles::AcornPolicy`, how items become host files (Extract, drags) |
| `dragout.h/.cpp` | the retro-files drag format's Acorn side (spec in `core/docs/retro-files-format.md`; the format itself is the core's `dw::xfer` and `dw::DragOutMime`): `RetroFiles::DragOutMime`, the drag folders' prefix and clean-up, and `XItem` <-> `dw::TransferItem` (`toTransfer` / `fromTransfer`, `writeHost`, `makeJson`, `readJson`: kept for the tests and CP/M's `interop_adw`) |
| `cli/` | `acorndw`, the console program: `AcornCli::Backend` (how an image is opened, made and converted, and how host files become items); the commands are DiskWranglerCore's `diskwrangler/cli` |
| `viewtext.*` | text / hex / BBC BASIC detokeniser (pure QtCore, no GUI) |
| `viewer.*` | the read-only viewer window |
| `main.cpp` | opens each command-line image in its own window; installs `fileopen.h`'s filter for the Finder's |
| `core/diskwrangler/app/appsettings.h` | where settings live: the system's place, or `<program>.ini` beside the program (portable; for an AppImage, beside the `.AppImage` file - `$APPIMAGE` - not in its temporary mount; a program installed on Linux, or by the Windows installer - `installedHere()`: Inno Setup's `unins000.dat` beside it -, has no portable mode, so File > Create local config file isn't shown). Use `AppSettings` instead of `QSettings` in program code (tests may use `QSettings` to clear the system store). Header only; in DiskWranglerCore |
| `core/diskwrangler/ui/buttonbar.h` | the main window's button bar: real QPushButtons kept in step with their QActions (a QToolBar's own buttons draw as flat text on macOS). Header only; in DiskWranglerCore |
| `core/diskwrangler/app/fileopen.h` | macOS opens files from the Finder (double-click, Open With, the Dock icon) by sending the running program a `QFileOpenEvent`, never a command-line argument: `FileOpen::FileOpenFilter<MainWindow>` (installed on the application in `main.cpp`) opens the image like File > Open - in an empty window, the active one first, else a new window; one already open just comes forward. Windows and Linux never send it. `guitest2` / CP/M Disk Wrangler's `guitest` send it. Header only; in DiskWranglerCore |
| `core/diskwrangler/ui/licences.*` | Help > Licences: the texts compiled in from `src/licences.qrc` (this program's own, kept here) and `licences-bundled.qrc` in a release build. In DiskWranglerCore |
| `icon.*` | the application icon (a 5.25" floppy), drawn with QPainter; `icons/` (at the top level) holds PNG exports (`tests/mkicon <dir>` regenerates them) and a matching `acorndiskwrangler.svg` - keep the SVG in step if the geometry changes |
| `acorndiskwrangler.desktop` (top level) | Linux/BSD: name and icon for docks and menus (GNOME ignores the window icon and shows a gear without it). Installed with the hicolor PNGs + SVG by `make install` (qmake `INSTALLS`, CMake `install()`); matched to the windows by `StartupWMClass` (X11 `WM_CLASS` is `acorndiskwrangler`) and `QGuiApplication::setDesktopFileName` (Wayland). Check with `desktop-file-validate` |
| `icons/acorndiskwrangler.icns` | macOS: the .app's icon (qmake `ICON`, CMake `MACOSX_BUNDLE_ICON_FILE`). Made by `tests/make_icns.py` from `mkicon`'s PNGs, 32-1024 px, every size an exact rendering. Not tested on a Mac |
| `acorndiskwrangler.rc` (top level), `icons/acorndiskwrangler.ico` | Windows only: the .exe icon in Explorer/taskbar. CMake compiles `acorndiskwrangler.rc`; qmake uses `RC_ICONS` in the `.pro` (it generates its own .rc, so the two are independent). Regenerate the .ico from the PNGs with Pillow if the design changes. Not testable on Linux |

## The rules that must not be broken
1. **Nothing is written to the image except by `Image::commit()`.** All edits (ADFS, DOS, DFS)
   are *staged* in the container (`Image::linear()`, `writeSectors`/`writeFileBytes`): a raw file in 128-byte
   blocks, the other containers per sector. Reads overlay staged data on the file, so every layer sees its own
   pending changes. A raw file is read as needed, never loaded whole (a hard disc is 512 MB). A staged write that
   fails (a bad sector in an ImageDisk file) undoes the whole change.
2. **Validate before writing.** `commit()` runs `validate()` (ADFS map + tree) and the *extra
   validator* (set by `Adfs::OpenedDisc`: the DOS Plus and DFS volumes the pending changes touch, by
   `Disc::validateTouched`, with "DOS partition: " / "DFS drive N: " in front of their errors). Any error vetoes the whole save. It also refuses if sectors 0-1 on
   disk changed since the image was opened. Then a complete new copy is written beside the file, opened again as
   an `Image` and checked (`validate()`, and the *reopen validator* - DOS and DFS attached to the copy and checked);
   only then is the `.bak` made (a hard link where the system allows) and the copy renamed over the original
   (DiskWranglerCore's `Container::SaveOptions`). There is no in-place write: a save needs room for a second copy,
   and on Windows it is refused while an emulator holds the file ("in use by another program").
3. **Never save a truncated image - and never grow a complete one.** `Image::fullSize()` is the true disc
   size; a short file reads as zeros to that size (`needsPadding()`), Save is enabled for pad-only, and
   the saved copy is `fullSize` long. For ADFS, padding only happens up to a
   *standard floppy size* (160K/320K/640K); never trust an ADFS map's size claim to grow a file (see the
   hybrid gotcha in docs/disc-formats.md). A save with nothing changed must leave the file byte-identical.
4. **Every mutating operation is atomic.** The window changes a disc through `Disc::transact` (the
   window kit does this for Delete, Rename, Properties, disc attributes and every import, one transaction per file);
   underneath, the filesystem classes still take `Image::snapshot()` and `restore()` on failure (see `DOS_TXN` in
   `core/diskwrangler/fs/fat/fat.cpp`, and the ADFS `addFile`/`makeDir`/... wrappers). A failed add (disc full,
   directory full) must leave no trace.
5. **A disc that fails validation on open is read-only.** The ADFS half of the 640K hybrid is
   locked (`Image::setAdfsLocked`) so ADFS edits cannot corrupt the FAT. Don't weaken this.
6. ADFS-only operations go through `Image::checkAdfs()`; raw containers (`openRaw`) never
   parse or write an ADFS map. `m_map.total` is reused as a bounds limit for raw images.

## GUI architecture
- **The window kit.** `MainWindow` is a `dw::ui::DiscWindow` (DiskWranglerCore, `core/diskwrangler/ui/discwindow.h`:
  read its header comment first) showing an `Adfs::OpenedDisc` (`document()`). The kit makes the panes, the standard
  actions, the rows, the selection, the title and status, and does each change in one `Disc::transact`; this program
  fills its hooks with its own dialogs, texts and rules. A test-visible slot or function the kit took over stays on
  `MainWindow` as a forward (the list is in the plan's step-5 history; they go when the tests are ported).
- **Panes.** `paneGroups()` = "ADFS", "DOS", "DFS", shown as tabs that come and go with the disc (`paneShown`,
  `paneTitle`); each mount of the document is in one group (`paneOf(mount)` / `mountOf(pane)`, never `if (pane == ...)`
  in the kit). `refresh()` follows `$.DOS.DRIVE_C` (`OpenedDisc::followDriveC`) and makes every pane again;
  `refreshChanged` after a change remakes every pane for an ADFS change (it may move DRIVE_C) and only its own pane
  for a DOS or DFS change. `refreshTree()` / `refreshDos()` / `refreshDfs()` remain for the tests.
- **Every refresh deletes and rebuilds the tree items.** Never hold a `QTreeWidgetItem*` across an operation that
  refreshes (this caused a use-after-free in a test); keep names/sector numbers instead. The rows are made by the
  kit in the volume's `list()` order and written by `decorateRow` from this program's own classes (`Adfs::Image`, the
  FAT volume, the DFS catalogue, each directory read once per refresh: `m_rowDir`, `m_rowNext`), with the program's
  roles `RoleParent/RoleName/RoleIsDir/RoleSector/RoleIsRoot/RoleSide` (`mainwindow_p.h`) beside the kit's. Order
  (`orderRows`: DFS by letter, then name), where to stop (`listChildren`: a directory already shown, a loop on a
  damaged disc, depth ADFS 64 / DOS 32) and an unreadable directory (`listFailed`) are hooks. DFS rows use name
  `"D.NAME"`.
- **Opening** is the kit's open pipeline (`openFile`; `openImage(path, force)` forwards to it): `detectImage`
  (`detectCandidates`), `openDocument` (ADFS D/E refused, the DOS / DFS checks), `handOverMessage` (a CP/M disc goes
  to CP/M Disk Wrangler, with the kit's link and Locate...), `afterOpen` (the read-only notices). The document runs
  with `setWindowRules(true)`: an unknown disc opens as ADFS, an unreadable DFS side or DOS partition is left out.
- **Save, Save as, Revert** are this program's overrides calling the document. Save as on a floppy offers ImageDisk,
  extended DSK, DSK and raw (the filter chosen decides the kind, never the name typed): another kind is written by
  `OpenedDisc::writeCopy` (the same code as `acorndw convert`), checked, and opened in the window; the same kind with
  unsaved changes is `Image::moveToCopy()` then `save()` into the copy; with none (or read-only) the file is copied
  and the copy opened. A hard disc (or a disc nothing recognised) gets only a copy of its own kind. `guitest2` and
  `saveastest` check them.
- Side order (Tools > Side order): *Read as* opens the file again through `openFile(path, 0|1)` (nothing written);
  *Convert* is `OpenedDisc::setConvertSides`, done on Save. The status bar shows "sides ..." with how it was settled.
  New image: `newImageDialog()` -> `NewDisc::create()` (formats from the core's presets registry, `NewDisc::types()`)
  -> opened like File > Open.
- Boot option (Edit > Boot option): `MainWindow::bootOption()/setBootOption()` act on the current pane -
  `Image::setBootOption` (ADFS floppy or hard disc: map sector 1 byte &FD, staged via `stageFsMap`, which
  recomputes both checksums) or `Dfs::Disc::setBootOption` (catalogue sector 1 byte 6 bits 4-5, per side).
  DOS panes have none; the locked ADFS side of a 640K DOS Plus disc refuses it. Note: every map save clears
  unused free-list slots past the end-of-list pointer (a real hard disc had a stale entry there) - harmless,
  ADFS ignores them.
- **Changes on the kit's patterns** (the kit checks the mount is writable, does the change in one transaction, refreshes):
  Delete (`deleteItems`, `confirmDelete`, each item on its own, failures listed), New directory, Rename
  (`renameRow`), Properties (`propertiesItems`: the selected rows, folders too; `propertiesEditor` builds the ADFS,
  DOS or DFS dialog and says what changes, `eachOnItsOwn`), Rename disc / volume and Boot option (`volumeEditor`, keys
  `title` / `label` / `boot`), Check disc (`showCheck`: Acorn's report, with "DOS: " / "DFS drive N: " and the padding
  warning). The dialog is the program's; the change, its transaction and the refresh are the kit's. A dialog keeps its
  widgets in the same creation order (`guitest` and the screenshots depend on it).
- Host drops of disc images (`MainWindow::dropImages`, called first by every pane's host-file drop handler; the same
  behaviour as CP/M Disk Wrangler): an empty window opens every dropped file (`openInWindow`: first here, rest in new
  windows); otherwise files `detectDisk` recognises are opened in new windows (a CP/M disc in CP/M Disk Wrangler),
  except on a hard disc (`isHardDisc()`: ADFS over 640K, any of its panes) where one question asks Open / Copy onto
  this disc / Cancel. The rest are imported. All queued (no dialog or window inside the drop handler). An empty
  window also ignores the retro-files JSON and takes the files, so it can open an image dragged out of another
  window. Add files never opens images.
- **DFS: both sides in one tree** (`m_dfsTree`), a root row per side ("Drive 0", "Drive 2"), the way CP/M Disk
  Wrangler shows a disc with a filesystem on each side (the owner asked for this so files can be dropped from one
  side onto the other). Every DFS row carries `RoleSide`, and `RoleParent` = its side, so a drag's or copy's
  `DragRef::parent` says which side (the tree's own pane is `PaneDfs0`). `dfsSide()` is the side of the selection
  (else the current row) while the DFS tab shows, -1 otherwise; `dfsTargetSide(row)` and `dropPane()` turn a drop
  row into `PaneDfs0/1`. Delete, Properties, View and Extract work across both sides (Extract puts each side in a
  `Drive N` folder when both are involved). A drag within the tree is accepted only on a two-sided disc and only
  from one side to the other (`DropTree::acceptable`, then `onInternalDrop` drops the refs already on the target
  side). The DFS pane keeps its selection, current row and closed sides across a refresh (`PaneLayout::keepCurrent`).
  Naming (owner's rule): "side" is always 0/1 (the disc's physical side, as in side order); "drive" is always
  0/2 (what a BBC Micro calls the two sides of a disc). Never print a side number next to "drive" or the reverse.
  Moving several files into a directory (Properties with more than one file selected: the letter field, blank =
  unchanged) is a rename, `dfsMoveSelected()` (Properties' `dfsProperties` and `planDfsMove`): every new name is checked first, against files not being moved and
  against the other selected files (case-insensitive, as DFS itself is - `d.moo` and `D.MOO`
  are one file; the letter's case as typed is kept). With a clash nothing moves unless the user chooses "Move the
  others". Locked files move too (as for a single-file rename here).
- **Importing** (`mainwindow_dfs.cpp`, the kit's import session): Add files, Add folder, drops on each pane,
  Paste, retro-files drops and drops from another window all end in `importAll` -> the kit's `importItems` (the
  core's `Importer`, one `Disc::transact` per file, errors collected and the rest added). Host files are read by
  `hostImport` = the core's `readHost` with `acornHostRead` (DOS never reads `.inf` files). This program's rules are
  the session's hooks (`importHooks(mount)`): names (`sanitizeName`, `fat::Volume::fromHost`, DFS names), attributes
  (`.inf` numbers, an item's own, a plain file over a file keeps load, exec and R W L E), and the DFS letter rules.
  DFS -> ADFS: a file whose DFS directory isn't `$` goes into a subdirectory named after the letter (case kept, one
  of either case reused; a *file* of that name in the way is an error), or `X_NAME` for a letter ADFS can't have;
  with Tools > *DFS directories become ADFS directories* off (`dfsLetterDirs`), flattened (`placeDfsLetter`, read at
  each file). ADFS -> DFS: folders are flattened, but a one-character ADFS directory name prompts "keep letter?"
  (Yes / Yes to all / No / No to all / Cancel; the `dirHook`, its "to all" in `m_run.letterPolicy`); on Yes its
  files, including those in its subfolders, go into that DFS directory (case kept). Single-letter *files* are never
  asked about. The question (`askReplace`: "File already exists", No the default, "and is LOCKED") and the summary
  (`importSummary`) are this program's texts. A locked file replaced is written over by the volume itself
  (`unlockBeforeReplace` off, so the DFS cycle number counts once).
- **Copying between discs and windows**: the source side is `exportRefs()` / `exportForCopy` (the kit's
  `exportItems` plus this program's dates); drags carry only `(window id, pane, parent, name)` in a custom MIME
  type, and the target looks up the source window in `MainWindow::windows()` (same process), exports there and
  imports here. The drop is queued (`QTimer::singleShot`) so no modal dialog runs inside the drop handler.
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
- **Window title and status bar** as CP/M Disk Wrangler's: `<file> * - Acorn Disk Wrangler` (`no disc - ...` when
  empty; `*` = unsaved changes or padding; `title()`). The status bar's left label (`m_status`, stretched) shows the
  disc and its space, then `  -  `-separated extras (side order, `read-only: <readOnlyReason()>`, padding;
  `statusVariants`); with no disc it says to open or drop an image. Short messages use `statusBar()->showMessage`,
  which covers the label for a while. Which actions are enabled is `updateActions`.
- **Copy and Paste** (`copySelected`, `paste` in `mainwindow_dfs.cpp`; the same design in CP/M Disk Wrangler): Copy
  exports the selection at once (`exportForCopy`) and keeps the `dw::TransferItem`s, so a paste gets what was
  selected then even if the source has changed or closed. The clipboard gets a `RetroFiles::DragOutMime` whose
  exporter returns that copy (files written to a drag folder only when another program asks: text/uri-list, the
  retro-files JSON and, `setForClipboard`, GNOME's `x-special/gnome-copied-files`), plus `DW_MIME_CLIP` = `pid:n`.
  Paste (`pasteItems`): our own token -> `importAll` from the kept copy (nothing written); else retro-files JSON ->
  `onRetroFilesDrop` (another copy of this program, CP/M Disk Wrangler); else local files from a file manager (URLs
  or GNOME's list) -> the pane's import of host paths, like Add files. It goes where Add files would (current row's
  directory, or the DFS side in use). At exit a clipboard we still own is cleared (its files are deleted with the
  drag folders). Checked across real processes on X11 (Xvfb + xclip: CP/M copy -> Acorn paste, a GNOME-style file
  list -> paste); Windows Explorer and macOS Finder are not tested.
- **Dragging out** (`dragout.h/.cpp`, spec in `core/docs/retro-files-format.md`): `makeMime`
  returns a `RetroFiles::DragOutMime` carrying the private formats *and* `text/uri-list` + the retro-files JSON.
  Those two are produced lazily (the core's `dw::DragOutMime`): the selection is exported (`exportRefs`) and written by
  the core's `writeHost` with `AcornPolicy` (same names/.inf as Extract to host; clashing names get `_2`; Windows device names and trailing
  dots made safe in `hostSafeName`) into a `QTemporaryDir` kept until exit (`cleanStale()` at start-up removes
  ones older than a day). Rules: (1) `acceptable()`/`dropEvent()` must only look at format *names*, never ask
  for a drag's URLs or JSON unless it is about to import them - asking makes the source write its files.
  (2) The private tag is `pid:window:pane`; `fromThisProcess()` decides a direct copy, because window ids are
  addresses and another copy of the program can have a window at the same address. (3) A drop from another
  process / tool goes through `onRetroFilesDrop` -> the core's `dw::xfer::readJson` (path-safety checked) ->
  `importAll`, so it behaves exactly like a copy between windows. (4a) The owner's rule for the JSON: **no redundant keys**. A
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
- To add an operation: if the kit has a pattern for it (a change, a copy-out, an import), fill its hooks; otherwise
  write it to the same shape (the mounts writable, one `Disc::transact`, `refreshChanged`), extend `updateActions`,
  and add a GUI test. To add a new disc format: a filesystem module in DiskWranglerCore (a `dw::fs::Volume`, a probe
  for detection, its presets) is the way for a format other Wranglers may use, as DFS and FAT are; a format only this
  program has goes in `src/` with a `DiskInfo::Kind`, its probe in `diskformat.cpp`, its mount in `OpenedDisc` and a
  pane group (or an existing one) in the window.

## GUI layout conventions
- Every pane's columns come from `paneLayout()` (the kit's `PaneLayout` / `PaneColumn`) - don't configure a header by hand.
  The **Name column stretches**; every other column has a width computed from the font (no fixed pixel
  widths), so the last column always meets the right-hand edge in every pane, and a minimum width
  keeps Name readable. Columns shared between panes use the same sample text so they line up:
  ADFS = Name Attr Load Exec Length Sector "Type / date"; DFS = the same without "Type / date";
  DOS = Name Attr Size Cluster Modified (numbers, then the date, as in ADFS).
- Numeric columns are right-aligned **and so are their headers**; fill them with `dw::setNumCell()`, which
  adds the same two-space pad as the header so a number never touches the next column. These are
  display strings only; real values live in the item roles - never parse the cell text.
- Root rows use `dw::rootLabel()` (from the `rootLabel(mount)` hook): `$   [title]`, `C:\   [label]`, `Drive 0   [title]`.
- An action that works only on some kinds of disc says so in its tool tip and status tip (New directory: ADFS and
  DOS, not DFS); the button bar shows the tip on a greyed-out button too.
- Toolbar buttons use short `setIconText` labels so the bar fits without the ">>" overflow; menu texts
  stay descriptive. Healthy-but-unusual discs (e.g. the 640K hybrid) must not report "warnings".
- Check any layout change visually: `tests/screenshot <outdir> <dsd> [<image> ...]` renders the DSD
  (with files dropped on it) and every tab of every further image, at 1300px and at the default window
  size (`*_narrow.png`), offscreen, on copies of the images.

## Build
```
qmake acorndiskwrangler.pro && make          # or: cmake -S . -B build && cmake --build build
```
The `.pro`, `CMakeLists.txt`, `.rc`, `.desktop` and `icons/` stay at the top level (the CI scripts look for `./*.pro`);
the program's sources are in `src/`. A new source file goes in the `.pro`, in `CMakeLists.txt`, and in any
`tests/*.pro` that builds `mainwindow.cpp` (they also `include(../core/diskwranglercore.pri)`).

* **Qt 6 and Qt 5.15 must both keep building.** qmake uses whichever `qmake` you run; CMake prefers Qt 6 and
  `-DUSE_QT5=ON` forces 5. Where they differ the code uses `#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)`:
  `DropTree::mimeData` (list by reference in 6) and `dropPos()` (QDropEvent::position() in 6); the drag data's own
  (`retrieveData`, QMetaType in 6) is in the core's `dw::DragOutMime`.
* **Version.** `version.pri` (qmake) and `version.cmake` (CMake) compile in `git describe --tags --always --dirty` as
  APP_VERSION, shown in Help > About; "unknown" without git, or set it with `qmake APP_VERSION=...` /
  `-DAPP_VERSION=...`. It is read when qmake/CMake runs, so re-run them after a new commit or tag.
* **Releases.** `.github/workflows/build.yml` builds Windows, macOS and Linux packages linked to Qt's *shared*
  libraries (keep it that way: the LGPL lets users replace them). Each is built twice so that the licences of every
  shipped library (`core/ci/collect-licences.sh` -> `licences-bundled.qrc`) are compiled in (Help > Licences,
  `core/diskwrangler/ui/licences.cpp`). Linux also gets a `.deb` against the system's Qt (`core/ci/make-deb.sh`; built on Ubuntu 22.04, it
  installs on 22.04+ and Debian 12+); Windows, for a tag or Run workflow only, an Inno Setup installer
  (`core/ci/installer.iss`; each program's AppId in it is fixed for good). A tag `vX.Y.Z` publishes a GitHub release
  whose notes are `release-notes/<tag>.md` when that file exists. The details for people are in `core/docs/building.md`;
  a 32-bit Windows 7 build (not tried yet) in `core/docs/building-windows-32bit.md`.
* **Still identical copies in both programs - change both:** `.github/workflows/build.yml`, `src/licences.qrc`,
  `licences/`, `version.pri` and `version.cmake`. (The CI scripts, the building and retro-files documents, the settings
  helper, the button bar, the Finder-open filter, the licences viewer and `settingstest` are in the core.)

## Test
Each test is its own project in `tests/`: `mkdir b && cd b && qmake ../tests/X.pro && make`; GUI tests run with
`QT_QPA_PLATFORM=offscreen`. [docs/testing.md](docs/testing.md) lists them all with their arguments. The ones a UI
change most often needs: `guitest2` (menus, buttons, title, status bar, drops, Copy / Paste), `dragtest` (drag data
and retro-files), `screenshot` (look at the result). Several tests need the owner's real hard disc image, which is
not in the repo: if you don't have it, say so rather than loosening the tests.

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
- After any change run the whole suite and the independent checks ([docs/testing.md](docs/testing.md)). When a test fails, find out whether the code or the test is wrong, and say so.
- State plainly what is unverified (zero-length ADFS files, BASIC
  tokens) in any summary to the owner.
- **Test against real discs as soon as you can**: synthetic images only prove the code agrees with itself (see the
  lesson at the end of [docs/disc-formats.md](docs/disc-formats.md)).

## Known gaps / ideas
ADFS D/E; Watford DDFS (double density); compacting a fragmented ADFS/DFS disc; Cut (a move
off a disc) is deliberately not offered.
