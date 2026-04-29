# mzf-hdr - MZF Header Modification

Tool version: 0.1.0. Part of the [bin2mzf](https://github.com/michalhucik/bin2mzf/) project.

> **Disclaimer:** The author provides no warranty and accepts no
> liability for data loss or damage resulting from use of the bin2mzf
> project tools. Distributed under the GPLv3 license, without warranty.

Modifies the 128-byte header of an existing MZF file without touching
the body. The body bytes remain byte-identical - the tool only
rewrites selected header fields according to the CLI options and
writes the result either back to the input file (in place) or to a
new target file via `-o`.

The typical use case is a quick metadata fix (renaming, changing the
load address, correcting the file type) without rebuilding the whole
MZF via `bin2mzf`. The tool belongs to the project's CLI family
alongside `bin2mzf`, `mzf-info`, `mzf-strip`, `mzf-cat` and
`mzf-paste`. It complements `bin2mzf` - where `bin2mzf` creates an
MZF, `mzf-hdr` adjusts it. The semantics is read-modify-write on the
header: every field for which the user supplies an option is
overwritten; all other fields are preserved from the original MZF.

## Usage

```
mzf-hdr [options] [-o OUTPUT.mzf] <INPUT.mzf>
mzf-hdr -n NAME <INPUT.mzf>
mzf-hdr -l LOAD -e EXEC -o OUTPUT.mzf <INPUT.mzf>
mzf-hdr --cpm --cpm-name NAME [--cpm-ext EXT] [--cpm-attr LIST] <INPUT.mzf>
mzf-hdr --version
mzf-hdr --lib-versions
mzf-hdr --help
```

## Arguments

| Argument | Description |
|----------|-------------|
| `<INPUT.mzf>` | Input MZF file (mandatory, exactly one positional argument) |

There is no `-i`/`--input` flag - the input MZF path is given solely
via the positional argument (consistent with `mzf-info` and
`mzf-strip`). No stdin input.

## Options

### Header field options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-t`, `--type TYPE` | 0x01..0xFF or `obj`, `btx`, `bsd`, `brd`, `rb` | preserved from INPUT | File type |
| `-n`, `--name NAME` | text (max 16 B after conversion) | preserved from INPUT | File name in the MZF header |
| `-c`, `--comment TEXT` | text (max 104 B after conversion) | preserved from INPUT | Comment text (longer is truncated) |
| `--cmnt-bin FILE` | path | preserved from INPUT | Read raw 104 B from FILE into the `cmnt` field |
| `-l`, `--load-addr ADDR` | 0..0xFFFF | preserved from INPUT | Z80 load address (decimal or 0x-prefixed) |
| `-e`, `--exec-addr ADDR` | 0..0xFFFF | preserved from INPUT | Z80 execution address |
| `-s`, `--size N` | 1..65535 | auto-size | Force the exact value of the `fsize` field |
| `--no-auto-size` | - | off | Keep the original `fsize` from INPUT.mzf |

### Character set

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Charset for `--name` and `--comment` |
| `--upper` | - | off | Uppercase a-z to A-Z before charset conversion |

### CP/M

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `--cpm` | - | off | Enable CP/M preset (SOKODI CMT.COM) |
| `--cpm-name NAME` | max 8 ASCII | - | CP/M filename (requires `--cpm`) |
| `--cpm-ext EXT` | max 3 ASCII | `COM` | CP/M extension (requires `--cpm`) |
| `--cpm-attr LIST` | `ro`, `sys`, `arc` (any combination) | - | CP/M attributes (requires `--cpm`) |

### Output and info

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-o`, `--output FILE` | path | in-place | Target MZF file; without this option INPUT.mzf is rewritten in place |
| `--version` | - | - | Print tool version and exit |
| `--lib-versions` | - | - | Print linked library versions and exit |
| `-h`, `--help` | - | - | Print help and exit |

Numeric values (`-l`, `-e`, `-s`) accept decimal or hexadecimal with
`0x` prefix (for example `0x1200` = `4608`).

### Selective field update details

Key design contrast against `bin2mzf`: every header field is
optional. An option that the user does not supply means "keep the
original value from INPUT.mzf"; an option that is supplied means
"overwrite this field".

Example: `mzf-hdr -n NEWNAME prog.mzf` renames the file inside the
header and all other fields (`ftype`, `fstrt`, `fexec`, `fsize`,
`cmnt`) remain untouched.

There is one exception to selectivity: the `fsize` field is
**automatically recomputed** by default to match the actual body
length (see `### Size details`). If you want to preserve the
original `fsize` exactly as it stands in INPUT.mzf (typically when
you know the value differs from the body length on purpose), use
`--no-auto-size`.

### Character set conversion details

The `name` and `comment` fields are stored in Sharp MZ ASCII in the
header. The `--charset` option controls how the input string from
`--name` and `--comment` is converted:

| Value | Meaning |
|-------|---------|
| `eu` | Sharp MZ ASCII European variant (MZ-700/800), input in ASCII/Latin |
| `utf8-eu` | Synonym of `eu`, input in UTF-8 (umlauts, ß, ...) |
| `jp` | Sharp MZ ASCII Japanese variant (MZ-1500) |
| `utf8-jp` | Synonym of `jp`, input in UTF-8 (kanji, katakana) |
| `none` | No conversion, raw bytes (suitable for ASCII names like CP/M) |

The `eu` / `utf8-eu` and `jp` / `utf8-jp` synonyms are semantically
interchangeable - both variants interpret the input as UTF-8.

The `--upper` option is applied BEFORE charset conversion. It maps
`a-z` to `A-Z` and acts the same on both `--name` and `--comment`.
Default is off.

> **Caution:** In CP/M mode (`--cpm`) the `--charset` option is
> mutually exclusive with the preset - the combination is a hard
> error. A `-c` comment in CP/M mode is internally processed as
> `--charset none`.

### --cmnt-bin details

The `--cmnt-bin FILE` option reads raw bytes from a file and writes
them straight into the 104-byte `cmnt` field of the header. It
bypasses every charset conversion as well as the `--upper` transform.
Useful for embedding a binary watermark, build-system signature or a
proprietary metadata block.

| FILE size | Behavior |
|-----------|----------|
| = 104 B | Exact fit - raw copy into the `cmnt` field |
| < 104 B | Pre-pad with zeros: first N bytes from the file, the rest 0x00 |
| > 104 B | Truncate to 104 B + warning on stderr (substring `truncated`) |

> **Caution:** This mutual exclusion applies to the pair
> `-c`/`--comment` × `--cmnt-bin` - both channels write to the same
> `cmnt` field, so they cannot be specified together (combining them
> is a hard error). Neither `-c`/`--comment` nor `--cmnt-bin` is in
> conflict with `--cpm` (both comment channels are allowed in CP/M
> mode).

### Size details

The `fsize` field in the header gives the body length. `mzf-hdr`
handles it in three modes:

| Mode | Behavior |
|------|----------|
| Default (no `-s`, no `--no-auto-size`) | Auto-size: `fsize` = actual body length from INPUT.mzf |
| `-s N` | Force `fsize = N`. Implicitly disables auto-size (DWIM). |
| `--no-auto-size` (without `-s`) | Keep the original `fsize` from INPUT.mzf unchanged |

Auto-size is the default because `mzf-hdr` typically adjusts
headers where `fsize` is supposed to match the real body length.
If the body is untouched and the original header had a correct
`fsize`, auto-size is a no-op.

**Hard error:** if you pass `-s N` with a value larger than the
actual body length in INPUT.mzf, the tool exits with an error
containing the substring `exceeds body size` and writes no output.
`mzf-hdr` cannot extend the body - use `bin2mzf` (rebuild) or
`mzf-paste` (insert bytes at offset) for that.

The same hard error fires defensively before write if the INPUT.mzf
was already malformed (`fsize > body_size`) and the user passed
`--no-auto-size`. This prevents heap OOB read in the library layer.

### CP/M mode details

The `--cpm` option enables the SOKODI CMT.COM convention for
transferring CP/M files inside MZF. Once enabled, the tool
overwrites these header fields:

- `ftype` = `0x22` (CP/M convention marker)
- `fstrt` = `fexec` = `0x0100` (CP/M TPA, where .COM files belong)
- `fname` is built using the SOKODI 8.3 layout from `--cpm-name`,
  `--cpm-ext` and `--cpm-attr`

The body of INPUT.mzf remains byte-identical. The difference against
`bin2mzf --cpm` is that `mzf-hdr --cpm` only repairs the header of
an already-existing MZF, whereas `bin2mzf --cpm` builds it from
scratch.

`--cpm` is mutually exclusive with `--type`, `--load-addr`,
`--exec-addr`, `--charset` and `--name`. Any such combination is a
hard error.

| Option | Combination with `--cpm` |
|--------|--------------------------|
| `--type`, `--load-addr`, `--exec-addr`, `--charset`, `--name` | Hard error (the preset would overwrite them) |
| `--comment`, `--cmnt-bin`, `--upper`, `--size`, `--no-auto-size` | OK (orthogonal to the preset) |

Sub-options:

- `--cpm-name NAME` - max 8 ASCII characters; the tool automatically
  uppercases and pads with spaces. Requires `--cpm`. Without
  `--cpm-name`, `--cpm` is a hard error.
- `--cpm-ext EXT` - max 3 ASCII characters, default `COM`. Requires
  `--cpm`.
- `--cpm-attr LIST` - any combination of `ro`, `sys`, `arc`. Items
  can be separated by comma, space or concatenated (`ro,sys`,
  `ro sys`, `roSys`). Case-insensitive. Requires `--cpm`.

Layout of the `fname` field (16 chars + 0x0D terminator) in CP/M mode:

| Offset | Meaning |
|--------|---------|
| 0..7 | CP/M name (8 chars, padded with spaces, ASCII) |
| 8 | `.` (0x2E) - separator |
| 9 | Extension[0] - bit 7 = R/O (Read-Only) |
| 10 | Extension[1] - bit 7 = SYS (System) |
| 11 | Extension[2] - bit 7 = ARC (Archived) |
| 12 | 0x0D (terminator) |

### Atomic write details

The write operation is atomic regardless of mode. In in-place mode
(no `-o`) the tool never overwrites the target file directly -
instead, it writes the result to a helper file `<INPUT>.tmp`,
renames the finished file via `rename()` to `<INPUT>` and the old
file is atomically replaced. If the write fails midway (for example
when the disk is full), the original INPUT.mzf stays unchanged and
`<INPUT>.tmp` is removed/left as a leftover artifact depending on
the error state.

In `-o OUTPUT.mzf` mode the output goes straight into the target
file - INPUT.mzf is never touched.

> **Caveat (Win32):** Atomic rename on Windows has a small race
> window between `unlink` of the target and `rename` from tmp to
> target where the target briefly does not exist. On POSIX
> (Linux/MSYS2 via Cygwin runtime) `rename()` is atomic without
> this window. Keep a backup or use `-o` for critical data.

## Exit codes

- `0` - success (including `--help`, `--version`, `--lib-versions`
  output).
- non-`0` - error (argument parsing, mutually exclusive options,
  I/O error, allocation, `-s N` outside the body range, malformed
  INPUT.mzf).

## Examples

Rename a file in place:

```bash
mzf-hdr -n NEWNAME prog.mzf
```

Change the load and exec address:

```bash
mzf-hdr -l 0x6000 -e 0x6000 prog.mzf
```

Change the file type to BTX (BASIC text program):

```bash
mzf-hdr -t btx prog.mzf
```

Make a new copy under a different name, keep the original:

```bash
mzf-hdr -n NEW -o new.mzf orig.mzf
```

Set CP/M attributes R/O+SYS on CCP.COM:

```bash
mzf-hdr --cpm --cpm-name CCP --cpm-attr ro,sys ccp.mzf
```

Replace the comment with raw bytes from a binary:

```bash
mzf-hdr --cmnt-bin watermark.bin prog.mzf
```

Force body size to 8192 bytes (must be <= actual body length):

```bash
mzf-hdr -s 8192 prog.mzf
```

JP charset with a Japanese name (UTF-8 input):

```bash
mzf-hdr --charset utf8-jp -n "テスト" game.mzf
```

Pipeline: show, modify, show again:

```bash
mzf-info prog.mzf && mzf-hdr -n FOO prog.mzf && mzf-info prog.mzf
```

## Related tools

- `bin2mzf` - build an MZF from a binary (full rebuild).
- `mzf-info` - inspect an existing MZF (header, hexdump, validation).
- `mzf-strip` - extract the body from an MZF (inverse of `bin2mzf`).
- `mzf-cat` - join several binaries into a single MZF (e.g. CCP+BDOS+BIOS).
- `mzf-paste` - insert data at a given offset of an existing MZF.

## MZF format reference

128-byte header + body (max 65535 bytes):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | `ftype` | File type (0x01 OBJ, 0x02 BTX, 0x03 BSD, 0x04 BRD, 0x05 RB, 0x22 CPM) |
| 0x01 | 16 | `fname.name` | Name (Sharp MZ ASCII, optionally CP/M 8.3 layout) |
| 0x11 | 1 | `terminator` | Name terminator (always 0x0D) |
| 0x12 | 2 | `fsize` | Body size in bytes (LE) |
| 0x14 | 2 | `fstrt` | Z80 load address (LE) |
| 0x16 | 2 | `fexec` | Z80 execution address (LE) |
| 0x18 | 104 | `cmnt` | Comment / reserved space |
