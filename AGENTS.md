# AGENTS.md - working on Acorn Disk Wrangler

Read this before changing code. It records the design rules, the facts about the disc formats
(and how sure we are of each), how to build and test, and the mistakes already made once.

## What this is
A Qt 5 (C++11) desktop tool for editing Acorn 8-bit disc images. It opens ADFS old-map hard
discs and S/M/L floppies, DFS (.ssd/.dsd), PC FAT floppies, Acorn DOS Plus 800K, the Acorn
640K ADFS/DOS hybrid, and the FAT16 DOS Plus partition stored in `$.DOS.DRIVE_C` on an ADFS
hard disc. One disc per window; files can be dragged between windows. GPLv3-or-later; the
copyright line in every header is `Copyright (C) 2026 Daniel Jameson`; put the same header on any
new source file (`#` comments for Python). The notice also appears in Help > About (`MainWindow::about`)
and the Windows version info (`QMAKE_TARGET_COPYRIGHT` in the `.pro`, `LegalCopyright` in the `.rc`).

## Source map (all in the repo root; tests in `tests/`)
| File | Role |
|---|---|
| `adfs.h/.cpp` | `Adfs::Image`: the staged sector store used by **every** format, plus the ADFS old-map filesystem (`FsMap`, `Dir`, `Entry`), validation, commit |
| `dosfs.h/.cpp` | `Dos::Volume`: FAT12/FAT16 inside an `Image` (BPB, table-driven `Dos::Format`, Acorn layouts); browse/add/mkdir/delete/rename/label/check |
| `dfs.h/.cpp` | `Dfs::Disc`: one side of a DFS disc (catalogue in sectors 0-1); a .dsd = two `Disc`s |
| `diskformat.h/.cpp` | `detectDisk(path)` -> `DiskInfo` (kind, full size, layout). Decides how a file is opened |
| `viewtext.*` | text / hex / BBC BASIC detokeniser (pure QtCore, no GUI) |
| `viewer.*` | the read-only viewer window |
| `icon.*` | the application icon (a 5.25" floppy), drawn with QPainter; `icons/` holds PNG exports (`tests/mkicon <dir>` regenerates them) and a matching `acorndiskwrangler.svg` - keep the SVG in step if the geometry changes |
| `acorndiskwrangler.rc`, `icons/acorndiskwrangler.ico` | Windows only: the .exe icon in Explorer/taskbar. CMake compiles `acorndiskwrangler.rc`; qmake uses `RC_ICONS` in the `.pro` (it generates its own .rc, so the two are independent). Regenerate the .ico from the PNGs with Pillow if the design changes. Not testable on Linux |
| `mainwindow.h/.cpp` | main window, `DropTree`, open/save/refresh/updateUi, ADFS pane |
| `mainwindow_dos.cpp` | DOS pane, viewer hookup, renaming, import summary |
| `mainwindow_dfs.cpp` | window registry, tab management, DFS panes, **cross-disc copy engine** |
| `mainwindow_p.h` | shared private header: item roles, `ImportCtx`, `Pane`, `XItem`, `DragRef`, name helpers |
| `main.cpp` | opens each command-line image in its own window |

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
    (t >= 80)**. `Dos::Params::interleave` / `ilvMap()` in `dosfs.cpp` do this inside `Volume::rd/wr`;
    everything above them uses logical offsets. Detection (`diskformat.cpp`) looks for the FAT signature
    at *file* offset 8192 (= logical sector 16).
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
  792 clusters. Unlike the hybrid it is stored **sequentially** (no track interleave): ISSUE.BAT, GEM
  fonts (`DIBHSS07.FNT` header "Swiss") and the `.RSC` file all read correctly, FAT chain lengths match
  every file size, and after edits `fsck.fat`/`mtools` (via a prepended BPB sector) are clean.
  `floptest real800 <copy of the disc>` checks all of that. The image is not in the repo.
- **ADFS D/E (new map)** is detected and refused; Watford 62-file DFS is not supported.
- The BBC BASIC token table and line-number encoding (`viewtext.cpp`) are from memory,
  checked on hand-made programs and one real program; report oddities rather than guess.

## GUI architecture
- One `MainWindow` = one disc. `m_img` is the `Image`. Tabs (`m_tabs`) are managed only by
  `ensureTab()` in canonical order ADFS, DOS, DFS drive 0, DFS drive 2; which exist depends on
  `m_info.kind`. `refreshTree()` is the single refresh entry point (it calls `refreshDos()` and
  `refreshDfs()`); call it after anything that changes the disc.
- **Every refresh deletes and rebuilds all tree items.** Never hold a `QTreeWidgetItem*` across
  an operation that refreshes (this caused a use-after-free in a test); keep names/sector
  numbers instead. Items carry `RoleParent/RoleName/RoleIsDir/RoleSector/RoleIsRoot`
  (see `mainwindow_p.h`): parent dir sector/cluster, name, is-dir, own sector/cluster or start
  sector, "this is the root row". DFS items use name `"D.NAME"`.
- Slots (`deleteSelected`, `editProperties`, `renameSelected`, ...) dispatch on the active pane:
  `dfsSide()` (>=0), `dosActive()`, else ADFS. `updateUi()` enables actions per pane kind.
- **Copying between discs** (`mainwindow_dfs.cpp`): source pane -> `exportRefs()` -> neutral
  `XItem` tree (data + attributes) -> `importX()` -> `importXAdfs/Dos/Dfs`, which convert names
  and attributes and reuse the normal add paths (`addOne`, `dosAddOne`, `dfsAddOne`) so the
  overwrite prompts and error reporting behave identically to host drops. Drags carry only
  `(window id, pane, parent, name)` in a custom MIME type; the target looks up the source
  window in `MainWindow::windows()` (same process). The drop is queued (`QTimer::singleShot`)
  so no modal dialog runs inside the drop handler.
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
- Toolbar buttons use short `setIconText` labels so the bar fits without the ">>" overflow; menu texts
  stay descriptive. Healthy-but-unusual discs (e.g. the 640K hybrid) must not report "warnings".
- Check any layout change visually: `tests/screenshot <outdir> <dsd> [<image> ...]` renders the DSD
  (with files dropped on it) and every tab of every further image, at 1300px and at the default window
  size (`*_narrow.png`), offscreen, on copies of the images.

## Build and test
```
qmake acorndiskwrangler.pro && make          # or CMake (CMakeLists.txt)
```
Needs Qt 5 widgets (developed on 5.15; `DropTree::mimeData` uses the Qt5 signature, which
differs in Qt6). Each test is its own project in `tests/`: `mkdir b && cd b && qmake ../tests/X.pro && make`.

| Test | Args | Needs |
|---|---|---|
| `viewtest` | none | nothing |
| `mkimg` | `<outdir>` | writes blank ADFS S/M/L, a truncated L, a fake ADFS D/E, a junk file |
| `detecttest` | image paths | prints the detected kind for each (eyeball it) |
| `floptest` | `plainl <blank ADFS L>` (must not detect as hybrid) / `bpb <img>` / `table <img> <idx 0-3>` / `acorn800 <out>` / `hybrid640 <out>` / `real800 <copy of a real 800K disc>` / `real640 <copy of a real hybrid disc> [<dir of files from extract_hd_dos.py>]` | PC images via `mkfs.fat -C x.img 360\|720\|1200\|1440`; the Acorn modes create their own |
| `dfstest` | `<img> ssd\|dsd <sectors-per-side>` | DFS images (see below) |
| `selftest` | `<hard-disc img> [<copy with a disc name in its map>]` | **the owner's real `scsi0.dat`**; make the 2nd arg with `make_testdata.py mapname <copy>` |
| `dostest` | `<hard-disc img>` | **the real `scsi0.dat`** (has `$.DOS.DRIVE_C`) |
| `screenshot` | `<outdir> <dsd> [<image> ...]` | renders PNGs of each pane for eyeballing (see GUI layout conventions); host files from `make_testdata.py host` |
| `guitest` | `<hard-disc img>` | **the real `scsi0.dat`**; run with `QT_QPA_PLATFORM=offscreen` |
| `guitest2` | `<dir>` [`<copy of a real 640K hybrid>`] containing `a.dsd b.adl c.img d.dat e.ssd f.hybrid g.a800` | copies of a blank DSD, ADFS L, PC 720K, **the real hard disc**, a truncated SSD, the hybrid and Acorn800 images; host files in `/tmp/drop/proj` (`make_testdata.py host`); offscreen |

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
(`MTOOLSRC=/dev/null mdir -i img ::`); for DFS and ADFS write a small separate Python parser
and compare file hashes. Our own reader agreeing with our own writer proves little.

## Testing GUI code (things that bit us)
- Synthetic drops must send `QDragEnterEvent`, then `QDragMoveEvent`, then `QDropEvent` to the
  tree's **viewport**; a lone drop event is ignored by Qt.
- `QTreeWidget::mimeData` drags need `supportedDropActions() == Copy`, else Qt "moves" (and
  removes) the source items after the drop.
- Closed windows/viewers use `deleteLater`: call `QCoreApplication::sendPostedEvents(nullptr,
  QEvent::DeferredDelete)` before counting children. `scrollToItem` before computing an
  item's rect or the drop lands on the root.
- Modal `QMessageBox`/`QInputDialog` inside a test block forever unless a `QTimer::singleShot`
  clicks/fills them (see `autoYes` in `guitest2.cpp`). `importSummary` only pops a box when
  there are errors, so a test that provokes errors must handle it.

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
No "new blank image" command (blank images were needed mostly for tests); ADFS D/E; Watford
DFS; boot-option editing for DFS/ADFS; compacting a fragmented ADFS/DFS disc; dragging files
*out* to the desktop.

## Lesson: test against real discs as soon as you can
The 640K hybrid was implemented from a table and passed every synthetic test (they all used the same
wrong assumption), yet the first real disc showed bugs: the FAT offset, a padding rule that would have
corrupted the file, and (found only when the owner opened a file) the track interleave, which made every
file read as garbage although the FAT and directory were perfectly valid. Synthetic images prove
self-consistency only, and **a valid directory/FAT does not prove the data is where you think it is:
always compare file CONTENT with a known-good copy** (`floptest real640 <copy> <dir from
tests/extract_hd_dos.py>`). The real disc is not in
the repo (it contains Acorn's DOS Plus software), so `floptest real640` and `guitest2`'s optional
argument take its path; ask the owner for one if you need to change anything hybrid-related.
