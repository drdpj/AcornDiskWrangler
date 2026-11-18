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
  The **title** (sector 0 bytes 0-7, sector 1 bytes 0-3) is kept byte for byte, as DFS 2.45 (the Master's DFS, the
  version this program follows where DFS versions differ) keeps it: every catalogue write puts the 12 bytes back as
  read, and only setting the title (Rename disc, `acorndw title`) writes new ones, the characters then zeros, as 2.45's
  `*TITLE` (command table entry &898E) does. Some DFS versions and tools pad a title with spaces; those spaces stay. It
  is shown up to the first NUL, trimmed. (Step 6c, deliberate change 11; before, the first change wrote it back
  zero-padded.)
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

## Tapes: the cassette filing system (series cfs-1, cfs-2)
A tape image is a UEF file (gzip or not); DiskWranglerCore's tape module reads its chunks and turns them into tones,
gaps and data (`core/diskwrangler/tape/`); `src/cfs.cpp` reads the BBC Micro's files from the data. Every rule below
was read from **MOS 1.20** (the BBC B's OS, SHA-1 `0d9bcaf6a393c9ce2359ed700ddb53c232c2c45d`, the copy jsbeeb ships;
the ROM is Acorn's and is not in the repo), disassembled with MAME's `unidasm` and watched running in jsbeeb with the
real tapes (17 Nov 2026). The Electron's OS 1.00 has **not** been checked (no copy here yet); one Electron tape
(`JoeBlade2_E`) reads the same way.

- **The CRC** (&F7B0-&F7D3): CRC-16, polynomial &1021, starting at 0 (&FB78 / &FB7C clear &BE / &BF), each byte
  exclusive-ORed into the high byte, eight shifts, most significant bit first (CRC-16/XMODEM: "123456789" gives
  &31C3). The header's CRC is stored **high byte first**, and the MOS reads it through the same routine, so a good
  header leaves 0 (&F76D: `LDA &BE / ORA &BF / STA &C1`); the data's likewise. Checked live: a breakpoint at &F76D
  on Gomoku's first block shows &BE = &BF = 0 and the header at &03B2 as below.
- **A block** (read at &F742-&F778; written at &F7EC-&F87B): the sync byte **&2A**, which must be the first byte after
  carrier (the receive interrupt, &F5B4-&F5D1: state 1 waits for the serial ULA's carrier detect, state 2 takes one
  byte and starts again from state 1 unless it is &2A, so a stray byte such as the dummy &AA is passed over). Then
  the name, read until its &00 but never more than 11 bytes (a name of 11 non-zero bytes keeps the first 10, the
  11th stands for the &00); load (4 bytes), exec (4), block number (2), data length (2), flag (1), 4 spare bytes, the
  header CRC (2): the 19 bytes go to &03BE-&03D0. Numbers little-endian. Then, if the length isn't 0 (&F7DF), the
  data and its CRC. Only the length's **low byte** counts the data (&F601 `CPY &03C8`; 0 with a non-zero high byte
  is 256), so a block holds at most 256 bytes.
- **The flag**: bit 7 the last block (&F225, &F6F7, &F867); bit 0 **protected** (&F1ED-&F1F6: only `*RUN` loads it,
  and sets `*FX200,3`, &F204; any other load, `*LOAD` and BASIC's `CHAIN` among them, stops with "Locked": the flag
  pulled at &F1F1 says whether the load is a `*RUN`. cfs-1 wrote here that `CHAIN` loads it too; jsbeeb showed
  "Locked at line 30" for a `CHAIN` of a protected file, 18 Nov 2026). **Bit 6** ("empty block" in later documents)
  is neither set nor tested anywhere in MOS 1.20. `*SAVE` (&F2C8-&F2F0) gives every block 256 bytes and flag 0 but
  the last, which gets what is left (0 for an empty file: one block, no data) and flag &80; a `BPUT` file (&F478)
  ends with a block of whatever is in the buffer, maybe 0 bytes, and has load and exec &FFFFFFFF.
- **A file** is blocks with the same name numbered from 0. The MOS loads block n at load + 256 x n (&F96A adds 1 to
  the address's second byte after each block), checks each block's number (&F9E3: "Block?") and name (&FA72,
  letters compared without case: "File?"); a bad CRC is "Data?" (&FA8E), the header's too (there is no "Header?" in
  MOS 1.20). Names are at most 10 characters (OSFILE, &F25A-&F274). `*CAT` shows each block's name and number, and at
  a last block the length as block x 256 + its length (&F8FF-&F90E).
- **Framing**: the MOS reads 8N1 (the serial ULA at 1200 baud, or 300 with `*TAPE3`). Data framed otherwise in a UEF
  (`&0104` with parity or another bit count, a negative stop count's extra wave, the Atom's `&0105`) can't be blocks.
  Carrier between a block's bytes is the line idling and doesn't stop it; silence does.
- **What else is on the tape**: after each block's CRC the MOS sends more bytes (&F855, &F858) before the motor
  stops, so a UEF made from a real save has a byte or two after the CRC in the block's chunk; and the dummy byte
  &AA (&FBF9) or noise sits alone between tones. Neither is a file. Data the reader can't take (a protected game's
  own loader format, Psycastria's 257 `&0104` chunks) is shown as "unrecognised data (chunks n-m)", one row between
  two files, read-only, its bytes as they are.
- **Repeated names**: one tape may hold two files of one name (Life of Repton has REPTON3 twice). Each is listed by
  its own name; underneath the second is `REPTON3 (2)` (what `acorndw` and the host file are called; `#7` names it
  too), its `.inf` says `REPTON3`. Names are made unique without case, as the MOS matches them.
- **Checked against**: the 11 real tapes in `testdata/UEF` read by `tests/cfs_check.py` (an independent reader from
  these rules, with its own UEF parsing) and by the program, row for row; Life of Repton's `*CAT` in jsbeeb gave the
  same names, block counts and lengths for the 11 files it was run through (both REPTON3s among them); `CHAIN ""` of
  Gomoku in jsbeeb loaded GOMOKU, which then loaded M/C, as listed.
- **Writing** (series cfs-2), as `*SAVE` does it (&F7EC-&F874, read in the ROM and measured in jsbeeb by catching
  what the MOS sends to the cassette, 18 Nov 2026). "RECORD then RETURN" starts the motor (&85 to the serial ULA);
  RETURN sends the dummy byte &AA (&FBE2-&FBFB). Before block 0: 5 waits of 51 frames, **5.1 s** of carrier (&F7FF,
  &F892-&F8A8; the line idles high while the 6850 sends nothing). After each block's CRC: two more turns of the
  transmit interrupt (&F855, &F858; it sends the last byte again, so a real recording has a byte or two after the CRC,
  cut off by the 6850's reset at &F85B), then 0.2 s (&F85E). Before each later block: 5 waits of &C7 + 1 frames
  (&F804), and &C7 is 6 (&FB2D) unless &03D1 holds another gap (&FB22): with the 0.2 s, **0.9 s**
  between blocks. After the last block: the 0.2 s and another **5.1 s** (&F86D), then the motor stops (&F246). The 4
  spare bytes are 0 (&F7ED-&F7F5). So a file written here is the chunks `&0111` (1 s of carrier, &AA, 5.1 s:
  `Cfs::Mos`), `&0100` per block with `&0110` of 0.9 s between, `&0110` of 5.3 s, and a file added after others has
  `&0116` of 1 s of silence first (the motor stopped; how long is the user's, as is the carrier before RETURN). The
  bytes after a CRC are not written (they are the 6850's leftovers, and the MOS ignores them).
- **Changing a tape** (`Cfs::Edit`): each file owns the chunks from the end of the file before it to its own last
  block (its silence, dummy byte and leader, with the trailer of the file before it); the information chunks a tape
  opens with stay first and what follows the last file stays last. A file is added at the end; replaced, renamed or
  given new attributes where it is (block for block if the number of blocks is the same, keeping the carrier between
  them and each block's other flag bits and spare bytes; otherwise its blocks again, the MOS's 0.9 s between); removed
  or moved with its own chunks, so no other file's chunks change. A file that opened the tape with no carrier before
  it gets the 5.1 s leader when it moves (the MOS needs carrier before &2A). The base frequency (`&0113`) and baud rate
  (`&0117`) each chunk was played at are put back where moved chunks need them. Damaged files and unrecognised data
  are kept as they are (never changed or removed; they move with the order). Unrecognised data copied to another
  tape takes its own chunks (from just after the item before it to its last; with the base frequency and baud rate
  they were played at in front, when not the defaults) and they go on the end of that tape as they are: it stays
  unrecognised data there. It travels as the retro-files namespace `uef`, key `chunks`: the UEF chunk records (id and
  length little-endian, then the body) in base64. Anything but a tape ignores the namespace, so on a disc or the
  computer the data arrives as a file of its bytes.
- **Checked against MOS 1.20 in jsbeeb** (`design/test-scripts/jsbeeb/cfs-jsbeeb.mjs`, 18 Nov 2026): a tape made by
  `acorndw` with every kind of change loads (`*CAT` as `acorndw ls`; `CHAIN ""` of a BASIC program that `*LOAD`s data
  and `CHAIN`s the next, which `*RUN`s a protected machine-code file; `*LOAD` of it says "Locked"); files the MOS
  `*SAVE`s (BASIC `SAVE`, 1000, 512 and 0 bytes, an `OPENOUT` / `BPUT` file) come back byte for byte with their
  addresses; and a tape whose files were moved loads in its new order, byte for byte.
- **Not yet**: the Electron's OS; the Atom's tapes; tapes recorded at the pulse level (UEF `&0102` is framed into
  bytes here; TZX and WAV aren't Acorn's).

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
