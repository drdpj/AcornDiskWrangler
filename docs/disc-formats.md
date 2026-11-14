# Disc formats - Acorn Disk Wrangler

The facts the code relies on for each disc format, how sure we are of each, and how they were found. Read this
before changing `src/adfs`, `src/diskformat`, `src/newdisc` or DiskWranglerCore's DFS and FAT modules (`core/diskwrangler/fs/dfs`, `fs/fat`). The design rules (staged
writes, validation, atomic edits) are in [AGENTS.md](../AGENTS.md); the tests named here are described in
[testing.md](testing.md).

## Format facts (confidence noted)
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
- **Watford DFS 62-file discs** (`.ssd` or either side of a `.dsd`). Checked against Watford's own ROMs, DFS 1.44
  and DDFS 1.54T (disassembled; their 62-file code is the same), a real disc (Superior Software 8) and
  `tests/acorncheck.py`. Neither ROM is in the repo.
  - **Layout:** a second catalogue in sectors 2-3, laid out like sectors 0-1, so files start at sector 4. Watford
    loads one catalogue at a time into the same buffer (&E00/&F00).
  - **Recognition:** eight &AA bytes at sector 2 bytes 0-7 (where the title would be) are all the ROM checks.
    `watfordCatalogue()` also wants both catalogues plausible and no first-catalogue file below sector 4, so a file
    at sector 2 whose data happens to start with &AA leaves an ordinary disc (Watford's manual warns of this case).
    The exception is a guard entry `!.!!!!!!!` (start 2, length &200; from BeebWiki). Neither ROM makes or knows it,
    so it comes from some other tool. It is hidden in the window, kept on writes, and takes a slot (61 files then).
  - ***FORM ... L** (DFS 1.44 at &9A73): writes a blank first catalogue, then the same buffer with &AA in bytes 0-7
    to sectors 2-3. Sector 3 bytes 0-3 are zero, byte 4 is its own cycle number, and bytes 6-7 hold the true size
    with boot bits 0. The boot option lives in sector 1 only (the real disc: 3 there, 0 in sector 3). Sector 3's
    bytes 0-3 and byte 6's other bits are kept as found.
  - **Saving a file** (DFS 1.44 at &8D0E, DDFS at the same code): the lowest free gap that fits is used. The file
    goes in the first catalogue unless it lies above the second's lowest file. If the chosen catalogue is full, the
    entry at the boundary moves across: the first's highest to the second, or the second's lowest to the first. So
    the first catalogue's files always lie below the second's, and existing files otherwise stay where they are.
    Only the catalogue that changes is written, and only its cycle number goes up. `Disc::placeWatford` and the
    Watford part of `writeCatalogue` do the same.
  - **The first catalogue's size:** every save sets it to the start of the second's lowest entry (the true size
    while the second is empty), so that Acorn DFS finds no room among files it can't see. Drive 0 of the real disc
    shows this (587). Drive 2 claims the full 800 although its second catalogue has files: some other tool, or no
    save since. Our next write there sets it as Watford would. The true size is the larger of the two catalogues'
    claims; detection uses it, which matters for placing side 1 of a sequential `.dsd`.
- **PC FAT**: standard; checked against `fsck.fat` and `mtools`. FAT12/16 chosen by cluster
  count. `dw::fat::Volume::attach` reads the BPB; `attachFormat` uses the table in the core's `fs/fat/fat.cpp`.
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
    (t >= 80)**. This is now done by `Adfs::Image` (`SideOrder::sideFirst`, the same as for ADFS L - see the
    side order notes below); `dw::fat::Volume` only ever sees the disc's own numbering. Detection
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
  The owner's rule: **PC floppies (360K/720K/1.2M/1.44M) are always interleaved** - they get no `SideOrder`, so
  no detection, Read as, Convert or sequential creation; only the Acorn formats can be odd): *interleaved* = cylinder 0 side 0, cylinder 0
  side 1, ...; *sequential* = all of side 0, then all of side 1. Two separate things must not be mixed up,
  and `Adfs::Image` handles both (DiskWranglerCore's `dw::device::SideOrder`, translated by the
  `Linear` it reads and writes through; everything above `Image` sees the disc in its own sector numbering): (1) the file's side order (`sequential`); (2) how the disc
  numbers its sectors (`sideFirst`). **ADFS L and DOS Plus 640K number side 0 first** - the machine reads all
  of side 0, then side 1 - so even an interleaved file is always remapped (verified on real discs: the Master
  Welcome disc, Acorn User Tau85 and ARM Evaluation disc 4, from b-em's `discs/` and oaknut's test data: only
  this mapping gives a valid directory tree, and every BBC BASIC program on them reads intact end to end).
  **DOS Plus 800K (and PC FAT) number sector, side, track**, i.e. the same as an interleaved file (verified on
  the real 800K disc). A DFS .dsd's sides are separate discs, mapped by `dw::dfs::Disc` on top of an interleaved
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
  - **Conversion** (`Image::setConvertOnSave`) is staged like any edit (`dirty()`); `commit()` stages the whole
    disc laid out in the other order (pending edits included) and saves it as a checked copy like any other save.
    Only a raw file has a side order: an ImageDisk, DSK or TeleDisk file records where each track is, so it is
    always read as interleaved and Tools > Side order does nothing for it. Converting and converting back
    must give a byte-identical file (`sidetest`, also on the real discs).
- **A damaged ADFS map** with a valid root directory is still detected as ADFS (and opens read-only) so that it is
  read with the right geometry - an ADFS L floppy needs its side-first numbering even to be inspected
  (`dragtest` caught this: before, such a disc read scrambled beyond the first track).
- **ADFS D/E (new map)** is detected and refused; Watford 62-file DFS is supported (above); Watford's double-density DDFS is not.
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

## Behaviour that follows from the formats
User-visible behaviour the README doesn't spell out:

* **Side order** that can't be told (a blank ADFS L or DOS Plus 800K disc, or one with only a few files in the
  root) falls back to the format's usual order - interleaved for every Acorn format - and the status bar says
  "Sides: interleaved (assumed)". *Read as* applies to this window only; the next open detects again.
* **Saving**: data is written first, directories next, the ADFS free space map last, then the image is read back
  and verified. Save is refused if the map changed on disk since the image was opened.
* **DOS names** that clash after conversion to 8.3 become `NAME~1.EXT`; new DOS files get the host clock's time.
* **New DOS Plus 640K disc**: ADFS free space map of 2720 sectors with none free, root "Acorn DOS Boot Disc"
  holding DOSBOOT, then an empty DOS Plus FAT and root. A DOSBOOT file may be at most 2304 bytes; from a disc
  image the whole boot track is copied exactly.

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
