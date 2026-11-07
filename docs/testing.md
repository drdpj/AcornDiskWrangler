# Testing - Acorn Disk Wrangler

Each test is its own qmake project in `tests/`, built against the sources in `src/` and the shared code in the `core/` submodule
(`git submodule update --init` if it is empty):

    mkdir b && cd b && qmake ../tests/X.pro && make

Run GUI tests with `QT_QPA_PLATFORM=offscreen`. Tests modify the images they are given, so always pass **copies**.
The format facts the tests check are in [disc-formats.md](disc-formats.md).

## The tests
| Test | Args | Needs |
|---|---|---|
| `viewtest` | none | nothing |
| `settingstest` (`core/tests/settingstest.pro`) | none | nothing (offscreen): where settings live on Linux - an installed program has no portable mode; an AppImage keeps `acorndiskwrangler.ini`-style files beside the `.AppImage` file. In DiskWranglerCore, shared with CP/M Disk Wrangler |
| `devicetest` (`core/tests/devicetest.pro`) | none | nothing: DiskWranglerCore's device layer - raw files read as needed and staged in blocks, the physical view, ImageDisk / DSK Save As and the stored view, `Linear` with side orders and slices, and the checked save (temporary copy, read-back, verify veto, `.bak` as a hard link, padding, permissions). Shared with CP/M Disk Wrangler |
| `containertest` | `<workdir> <raw image> <kind> ...` (kind `adfs`, `ssd`, `dsd`, `hybrid`, `dos800`, `pc`; interleaved images) | Acorn discs in ImageDisk, extended and standard DSK, and TeleDisk files: each raw image is copied into each; detection, every file, the main window's panes and Tools > Side order, read-only TeleDisk, and the same change made to the raw image and to each copy must give the same disc (byte for byte on ADFS and DFS). `acorn_all.sh` gives it the ADFS L and M, blank DSD and SSD, PC 720K and 1.44M, the hybrid and 800K test images and the real Master 512 discs; offscreen |
| `mkimg` | `<outdir>` | writes blank ADFS S/M/L, a truncated L, a fake ADFS D/E, a junk file |
| `detecttest` | image paths | prints the detected kind, side order and whether the order could be told, for each (eyeball it) |
| `floptest` | `plainl <blank ADFS L>` (must not detect as hybrid) / `bpb <img>` / `table <img> <idx 0-3>` / `acorn800 <out>` / `hybrid640 <out>` / `real800 <copy of a real 800K disc>` / `real640 <copy of a real hybrid disc> [<dir of files from extract_hd_dos.py>]` | PC images via `mkfs.fat -C x.img 360\|720\|1200\|1440`; the Acorn modes create their own |
| `dfstest` | `<img> ssd\|dsd <sectors-per-side>` | DFS images (see below) |
| `watfordtest` | `<outdir>` [`<copy of a real Watford 62-file .dsd>`] | new 62-file discs of every size filled to 62 (the 63rd refused), fewer than 32 files, where a new file goes and which entry moves when a catalogue is full (Watford's DFS 1.44 save routine), only a changed catalogue's cycle number counting on, the guard entry, an ordinary disc whose first file starts with &AA. With the real disc: a save with no changes is byte-identical, renames change only the cycle bytes, an add keeps every other file, and drive 2's first-catalogue size is set as Watford's DFS would. Then run `tests/acorncheck.py` on its output |
| `selftest` | `<hard-disc img> [<copy with a disc name in its map>]` | **the owner's real `scsi0.dat`**; make the 2nd arg with `make_testdata.py mapname <copy>` |
| `dostest` | `<hard-disc img>` | **the real `scsi0.dat`** (has `$.DOS.DRIVE_C`) |
| `screenshot` | `<outdir> <dsd> [<image> ...]` | saves `dfs.png` (the DSD with host files dropped), `dialog_<slot>.png` for each dialog the DSD's window opens (newImageDialog, editProperties, viewSelected, bootOptionDialog, checkImage, about; exits 1 if one opens none), and each tab of every further image as `<image>_<tab>.png` and `<image>_<tab>_narrow.png`; renders PNGs of each pane for eyeballing (see GUI layout conventions in AGENTS.md); host files from `make_testdata.py host` |
| `guitest` | `<hard-disc img>` | **the real `scsi0.dat`**; run with `QT_QPA_PLATFORM=offscreen` |
| `guitest2` | `<dir>` [`<copy of a real 640K hybrid>`] containing `a.dsd b.adl c.img d.dat e.ssd f.hybrid g.a800` | copies of a blank DSD, ADFS L, PC 720K, **the real hard disc**, a truncated SSD, the hybrid and Acorn800 images; host files in `/tmp/drop/proj` (`make_testdata.py host`); offscreen. Also checks the menu and button order, the title and status bar, and Copy / Paste with the real keys (`QTest`, so the `.pro` has `QT += testlib`) |
| `sidetest` | `<outdir>` [`<copy of a real 640K hybrid>` [`<copy of a real 800K disc>` [`<copies of real ADFS L discs, named t2_il_<name>.adl in <outdir>>` ...]]] | nothing else (a made-up DOSBOOT is used without the real hybrid; with it, new 640K discs are compared with it): makes every format with `NewDisc` in both side orders, fills, detects, converts both ways, reads in the wrong order, drives MainWindow (offscreen). The real discs are converted and back (must stay byte-identical). Then run `tests/sidecheck.py <outdir>`: an independent reader (own de-interleave, own ADFS/DFS parser, `fsck.fat` + `mtools` for FAT, with the Acorn formats wrapped in a BPB) |
| `dragtest` | `<outdir>` | nothing else: makes its own discs. Drives the drag data as Explorer / another instance / another tool would: lazy writing, names, .inf, dates, JSON, direct copy in-process, JSON path cross-process with attributes kept (ADFS, DFS, DOS), DFS directory letters -> ADFS subdirectories (drop on a directory row, reuse, JSON path), single-letter ADFS directories -> DFS letters (Yes/No/Yes to all via JSON/Cancel), disc images dropped (empty window, floppy, hard disc Open/Copy/Cancel/two images), self-drop refused, a foreign (cpm) list, unsafe lists refused, read-only image, stale cleanup. Then run `tests/retrofilescheck.py <outdir>/drag-*` (independent check against the spec) |
| `xdragtest` | `<outdir>`, run as `xvfb-run -a -s "-screen 0 1280x800x24" xdragtest <outdir>` | Linux/X11 only, needs Xvfb and xdotool. Real drags with the real pointer to a stand-in Explorer in another process: dropped (files arrive, message), passed over and let go back in the window (no message), refused (message not reliable on X11 - printed only) |
| `acorncheck.py` | images ... [`--sequential` images ...] | independent ADFS/DFS checker, see "Verify independently" below |
| `boottest` | `<outdir>` [`<copy of a hard disc image>`] | ADFS S/M/L and the hard disc: options 0-3 set, saved, re-read, only byte &FD (+ checksums) changed; DFS sides independent; refusals; MainWindow per pane; a 640K disc with an earlier version's sector-1 check byte (00) is corrected. `tests/acorncheck.py` re-checks the checksums independently |

`selftest`, `dostest`, `guitest` and `guitest2` assert details of the owner's image (12 root
entries, 69 DOS root entries, `BeebSCSI`, `!Config`, ...). That image is **not** in the repo; if
you don't have it, those tests cannot run - say so rather than loosening them. It is 512 MB: delete the copies
afterwards (the sandbox disk filled up once).

## Test data
`tests/make_testdata.py all <dir>` makes the synthetic images (stdlib only; see its docstring): blank, truncated and
sequential-side DFS images, the PC floppies (via `mkfs.fat`, with a few files on them), the host files `guitest2`
drops (in `/tmp/drop/proj`), and `mapname <copy>` makes the map-name variant for `selftest`. ADFS floppies come from
`tests/mkimg <dir>`, the Acorn 800K / hybrid images from `floptest acorn800|hybrid640 <out>`.

For `guitest2`'s folder: `a.dsd` = `blank40.dsd` (it checks for 204,800 bytes), `b.adl` = `adfs_l.adl`, `c.img` =
an **empty** 720K floppy (`mkfs.fat -C c.img 720`: the test makes a `SUB` directory, which `make_testdata.py`'s
`pc720.img` already has), `d.dat` = a copy of the real hard disc, `e.ssd` = `trunc40.ssd`, `f.hybrid` = the
`hybrid640` output and `g.a800` = the `acorn800` output.

## Verify independently, not just with our own code
For FAT use `fsck.fat -n` and `mtools` (`MTOOLSRC=/dev/null mdir -i img ::`); for ADFS and DFS run `tests/acorncheck.py` on real discs (b-em's `discs/`,
the owner's) and on everything the tests wrote. It is written only from Acorn's ADFS ROM (map checksum routine,
floppy sector numbering), b-em's image layouts and the RISC OS Open old-map description - not from this program -
and walks every directory and checks BBC BASIC programs read intact, so a wrong side order fails on any disc with
data beyond the first track. Our own reader agreeing with our own writer proves little: the map checksum started
at 0 instead of Acorn's &FF through all the earlier testing, which only matters for an all-zero sector 1 (new 640K DOS Plus
discs made from a DOSBOOT file got 00 where ADFS expects FF), and only acorncheck found it.
At the owner's request, Disc Image Manager is not used as a source or reference.

`boottest` leaves `boot_hyb_old.adl` with a deliberately wrong map check byte (&12), so acorncheck reports that one
file as failed; every other file it writes must pass.
