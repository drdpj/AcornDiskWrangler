# retro-files: dragging files out of disc images, with their attributes

**MIME type:** `application/x-retro-files+json` - **version 1**

A small, open convention for drag-and-drop (and clipboard) between tools that edit retro disc images, so
that a file dragged out of one tool and into another keeps the attributes a plain host file can't hold:
load/exec addresses, access bits, a DFS directory letter, DOS attribute bits, CP/M user numbers, and so on.

Acorn Disk Wrangler produces and accepts it. Nothing else is required to understand it: a drag always carries
ordinary files as well (see *How a drag is put together*), so any program that accepts dropped files works,
and a program that knows this format just gets more.

## Why this format is needed

A drag between programs normally carries only what the operating system understands about a file: its name,
its contents and perhaps its dates. Files on retro discs have more than that, and it is part of the file:

* An Acorn file has a **load address and an execution address**. Without them a machine-code program or
  BASIC program may not run, and the pair is also how ADFS stores a file's type and time stamp.
* Each filesystem has its own **attribute bits**: ADFS locked/read/write/execute, a DFS directory letter and
  lock flag, DOS read-only/hidden/system/archive, CP/M user numbers.
* Names can use characters or lengths a host file system refuses, and the same file may have to be renamed
  differently depending on where it lands.

A host file system has no place to keep any of this. A plain drag to Explorer or a file manager therefore
loses it, and a drag from one disc tool to another has no way to carry it at all. The usual workaround is a
sidecar file, and the Acorn `.inf` file is exactly that. It works, but it is specific to Acorn, so a CP/M
tool can't be expected to write or read it. It is also separate from the file, so it is easily left behind.

Drag and drop between programs, including between a virtual machine and its host, only passes standard types
such as `text/uri-list` and `text/plain`. There is no standard type for "a file plus the attributes of the
disc it came from". This format fills that gap:

* **No loss between disc tools.** Moving a file from one tool to another, or from one disc type to another,
  keeps everything the destination can use. The receiver converts what it can (for example widening or
  narrowing addresses) and drops the rest knowingly, rather than guessing from a bare file.
* **Nothing breaks for other programs.** The same drag also carries ordinary files, so Explorer, file
  managers and editors work as they always did.
* **One format for many tools.** Attributes are grouped by filesystem (`acorn`, `dfs`, `fat`, `cpm`), so a
  tool for a new kind of disc only has to add its own group and read the ones it understands.
* **Open and self-describing.** A small, versioned JSON document that anyone can implement or inspect
  without this program's source.

No existing standard covers this. Windows' virtual-file formats (`FileGroupDescriptorW` / `FileContents`) carry
names, sizes and dates only; CiderPress II has a JSON drag format, but it is private to that program and
specific to Apple II / Macintosh files. The Acorn `.inf` sidecar file is the nearest convention, and it is
Acorn-only, so it can't be expected of other tools.

## How a drag is put together

A drag offers several formats at once. A receiver takes the richest one it understands:

| Format | Who uses it | Contents |
|---|---|---|
| `application/x-retro-files+json` | tools that know this convention | the JSON document below |
| `text/uri-list` (Windows: `CF_HDROP`) | Explorer, file managers, any program | the top-level files and folders of the drag |
| tool-private formats | the sending tool itself | whatever it likes (Acorn Disk Wrangler uses `application/x-acorndiskwrangler-*` between its own windows in one process) |

The files are real files in a folder (the **base** folder) that the sending tool creates, usually under the
system temporary folder. The base folder may contain more than the listed items: Acorn Disk Wrangler writes
Acorn `.inf` sidecar files next to the files when its *Write .inf sidecar files when extracting* option is on,
for the benefit of Explorer and Acorn tools. **A receiver that uses the JSON must take only the items listed
in it** and ignore anything else in the base folder or in `text/uri-list`.

The sender may create the files only when a receiver first asks for `text/uri-list` or the JSON (Acorn Disk
Wrangler does), and keeps them until it exits. A receiver must copy the files during the drop and must not
keep references to them.

### On each platform

* **Windows:** the format is the registered clipboard format named exactly `application/x-retro-files+json`
  (`RegisterClipboardFormatW`), delivered as `TYMED_HGLOBAL` containing the UTF-8 JSON bytes, no terminator
  required. This is what Qt 5 does for a custom MIME type, so a Qt receiver just asks for the MIME type.
* **Linux / BSD (X11, Wayland):** the MIME type itself, UTF-8 bytes.
* **macOS:** not tested.

## The JSON document

UTF-8, one JSON object. Example (a directory and a file dragged from an ADFS disc):

```json
{
  "format": "retro-files",
  "version": 1,
  "generator": "Acorn Disk Wrangler",
  "source": { "image": "Welcome.adl", "filesystem": "adfs" },
  "base": "file:///C:/Users/me/AppData/Local/Temp/acorndiskwrangler-drag-aB3xYz",
  "items": [
    { "path": "Games", "type": "directory", "name": "Games" },
    { "path": "Games/Elite", "type": "file", "name": "Elite", "size": 21504,
      "modified": "1987-03-01T12:00:00",
      "attributes": { "acorn": { "load": "FFFF1900", "exec": "FFFF8023", "access": "LWR" } } }
  ]
}
```

The same for a file dragged from a DFS disc: it has load and exec addresses too (here widened to 32 bits, see
`acorn` below), and its DFS directory letter is kept separately:

```json
{ "path": "A.BOOT", "type": "file", "name": "BOOT", "size": 300,
  "attributes": { "acorn": { "load": "FFFF1900", "exec": "FFFF8023", "access": "L" },
                  "dfs":   { "directory": "A" } } }
```

| Key | Required | Meaning |
|---|---|---|
| `format` | yes | always `"retro-files"` |
| `version` | yes | `1`. A receiver that finds a higher number should ignore the JSON and use the plain files. Version 1 receivers ignore keys they don't know, so new optional keys don't need a new version. |
| `generator` | no | the sending program, for information |
| `source.filesystem` | yes | the filesystem the items came from: `"adfs"`, `"dfs"`, `"fat"` (PC and Acorn DOS Plus), or another short lower-case name (suggested: `"cpm"`). A receiver uses it to interpret `name` and to decide how to convert. |
| `source.image` | no | file name of the disc image, for information |
| `base` | yes | `file:` URL of the base folder. Item paths are relative to it. |
| `items` | yes | every file and directory in the drag, **parents before their contents** |

Each item:

| Key | Required | Meaning |
|---|---|---|
| `path` | yes | path of the item relative to `base`, `/`-separated, exactly as written on the host. It never contains `..`, never starts with `/`, and a receiver must refuse an item whose path leaves the base folder. Its last component may differ from `name` (characters the host can't hold are replaced, and clashing names are made unique). |
| `type` | yes | `"file"` or `"directory"` |
| `name` | yes | the item's name on the source filesystem, unchanged (it may contain characters the host can't) |
| `size` | files | the file's length in bytes; equal to the size of the host file |
| `modified` | no | the item's date as the source filesystem records it, ISO 8601 `YYYY-MM-DDThh:mm:ss`, **no time zone** (retro filesystems don't record one; take it as the time shown on the original machine). The host file's modification time is set to the same value where the sender can. |
| `attributes` | no | an object of **namespaces**, each an object of filesystem-specific attributes. A receiver uses the namespaces it knows and ignores the rest. An item may carry several. |

## Attribute namespaces

### `acorn` - Acorn file attributes (ADFS, DFS, and RISC OS)

The attributes every Acorn filesystem has in common. **Both ADFS and DFS files carry `load` and `exec`**, and a
sender whose source is either of them must give both for every file.

| Key | Meaning |
|---|---|
| `load`, `exec` | load and execution addresses, 8 upper-case hex digits (32 bits). Files only. These are the authoritative values: everything else in this namespace is derived from them or is optional. |
| `access` | access letters, any of `L` (locked), `W`, `R`, `E` (ADFS execute-only), plus `D` on a directory (informative). DFS has only *locked*: a locked DFS file is `"L"`, an unlocked one `"WR"`. |
| `filetype` | optional, RISC OS files only: the file type as 3 upper-case hex digits, e.g. `"FFF"`. See below. |

**ADFS and DFS addresses.** ADFS keeps full 32-bit addresses. DFS keeps 18 bits (`&00000`-`&3FFFF`), so a
sender widens them the way `.inf` files do: an address with bits 16-17 both set (`&3xxxx`) is written as
`&FFFFxxxx`, the address in the I/O processor (second processor) address space. A receiver that stores them in
DFS narrows them again (keeps the low 18 bits). A receiver moving a file between the two keeps the 32-bit values
when the target is ADFS, and has no place for them if they won't fit an 18-bit DFS address. That is a loss the
receiver should report, not hide.

**RISC OS file types.** ADFS floppies and hard discs are also written by RISC OS, which doesn't store a
file type as a separate field. It packs the type and a date into the load and exec addresses (a "typed and
time-stamped" file):

```
load = &FFFtttdd      FFF in the top 12 bits marks the file as typed; ttt = the file type; dd = bits 32-39 of the time
exec = &xxxxxxxx      bits 0-31 of the time
time = centiseconds since 1900-01-01 00:00:00 (a 40-bit number; no time zone)
```

For example, `load` `FFFFFF5A` with any `exec` is a Text file (type `FFF`, dated in the 2020s); `FFB` is BBC BASIC,
`FF8` an absolute (machine-code) program, `FFD` data, `FFA` a module, `FEB` an Obey script, `FF9` a sprite file,
`DDC` an archive. (Registered types are listed by RISC OS Open.) Acorn Disk Wrangler doesn't interpret any of
this itself. It keeps `load` and `exec` exactly as they are on the disc and shows the stamp as `modified`, so a
typed file passes between two ADFS discs, or out to a tool that understands it, without losing the type.
The rules for a tool that does handle types are:

* **Sending.** `load` and `exec` stay raw, as above. `modified` is the date held in the stamp, as calendar time with no
  zone (the machine's own clock reading). A sender may also add
  `filetype` (just the `ttt` digits) so that receivers don't have to decode `load`. It is redundant, and if it
  disagrees with `load`, `load` wins.
* **Receiving with no `load`/`exec`.** A tool that only knows a file's type (for example a host file named
  `Name,ffb`: RPCEmu's host file system writes a typed file's type as a comma and three hex digits after the
  name, leaves the suffix off for Text, `&FFF`, and takes the date from the host file's time) may send `filetype` and
  `modified` and leave `load` and `exec` out. A receiver that stores Acorn addresses builds them from the
  formula above, using `modified` as the time (or its own clock if there is none). A receiver that doesn't
  understand types ignores `filetype` and stores the file with no addresses.
* **Which `load` values are typed.** A file is *typed* if the top 12 bits of `load` are all set. That is
  RISC OS's own rule, and it has one wrinkle: BBC I/O-processor addresses (`&FFFF1900`, `&FFFF8023`) satisfy
  it too, and a DFS file widened as above always does. A reader must therefore read `load` as a typed stamp
  only when `source.filesystem` is `"adfs"` and the date it gives is plausible (the program uses 1981-2100). A
  DFS file never has a file type. Acorn Disk Wrangler uses the same test when it copies an ADFS stamped file to
  DFS: the file gets load/exec `0`, because the stamp isn't an address.
* **Untyped files.** The same emulator writes a file that has plain load and exec addresses as
  `Name,LLLL-EEEE` (both in hex). That is a host-file convention, not part of this format: such a file is just
  `load` and `exec`, with no `filetype`.
* **Directories** don't have load and exec and so have no type.

### `dfs` - Acorn DFS

| Key | Meaning |
|---|---|
| `directory` | the single-character DFS directory, e.g. `"$"` or `"A"`. `name` is the name without it (`"ELITE"`, not `"$.ELITE"`). |

A DFS file's load and exec addresses are in the `acorn` namespace, not here, so a tool that reads Acorn files
needs one code path for ADFS and DFS. `dfs` holds only what DFS has and ADFS hasn't.

### `fat` - DOS FAT attributes (PC, Acorn DOS Plus)

| Key | Meaning |
|---|---|
| `readonly`, `hidden`, `system`, `archive` | booleans, the FAT attribute bits |

### `cpm` - CP/M (suggested, not produced by Acorn Disk Wrangler)

A suggestion for a CP/M tool, so that tools agree from the start:

| Key | Meaning |
|---|---|
| `user` | user number, 0-15 (or 0-31) |
| `readonly`, `system`, `archive` | booleans, the `T1'`-`T3'` attribute bits |

New namespaces should be short, lower-case and named after the filesystem or machine family. Please add them
here.

## What Acorn Disk Wrangler does with a drop

* From one of its own windows in the same process: copies directly, with full attributes (private format).
* A drop with this JSON (another copy of the program, or another tool): imports the listed items using
  `source.filesystem` and the attributes, exactly as a copy between two of its own windows would; files from a
  filesystem it doesn't know (e.g. `cpm`) are treated like files dropped from the host. Dates are not applied
  (new files get the current time, as for any other copy).
* Otherwise, plain files: imported as files from the host, reading `.inf` sidecars if that option is on.

## History

* Version 1 (2026): first version, Acorn Disk Wrangler. `acorn.filetype` (RISC OS) is an optional key added to
  version 1 without a version change, as the rules above allow.
