# bin2mzf - Build an MZF File from a Binary

Tool version: 0.2.0. Part of the [bin2mzf](https://github.com/michalhucik/bin2mzf/) project.

> **Disclaimer:** The author provides no warranty and accepts no
> liability for data loss or damage resulting from use of the bin2mzf
> project tools. Distributed under the GPLv3 license, without warranty.

Builds an MZF file (Sharp MZ tape format) from a binary input. Assembles
the 128-byte header from the given options (file type, name, load/exec
address, comment) and appends the body from the binary.

The tool is primarily intended as the final step of a build pipeline
for developers targeting the Sharp MZ platform (MZ-700, MZ-800,
MZ-1500). It belongs to the project's CLI tool family alongside
`mzf-info`, `mzf-hdr`, `mzf-strip`, `mzf-cat` and `mzf-paste`.

## Usage

```
bin2mzf -n NAME -l LOAD_ADDR [options] [INPUT]
bin2mzf -n NAME -l LOAD_ADDR -i INPUT -o OUTPUT.mzf
bin2mzf -n NAME -l LOAD_ADDR --header-only -o OUTPUT.mzf
bin2mzf --cpm --cpm-name NAME [--cpm-ext EXT] [--cpm-attr LIST] -i INPUT -o OUTPUT.mzf
bin2mzf --version
bin2mzf --lib-versions
bin2mzf --help
```

## Arguments

| Argument | Description |
|----------|-------------|
| `[INPUT]` | Input binary file (optional; alternative to `-i` or stdin) |

## Options

### Required options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-n`, `--name NAME` | text (max 16 B after conversion) | - | File name written into the MZF header |
| `-l`, `--load-addr ADDR` | 0..0xFFFF | - | Z80 load address (decimal or 0x-prefixed) |

### Optional options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-t`, `--type TYPE` | 0x01..0xFF or `obj`, `btx`, `bsd`, `brd`, `rb` | `obj` (0x01) | File type |
| `-e`, `--exec-addr ADDR` | 0..0xFFFF | equal to `--load-addr` | Z80 execution address |
| `-c`, `--comment TEXT` | text (max 104 B after conversion) | - | Comment text (longer is truncated) |
| `-i`, `--input FILE` | path | - | Input binary file (alternative to positional argument) |
| `-o`, `--output FILE` | path | stdout | Output MZF file |
| `-C`, `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Charset for `--name` and `--comment` |
| `--upper` | - | off | Uppercase a-z to A-Z before charset conversion |
| `-s`, `--size N\|auto` | 1..65535 or `auto` | `auto` | Force exact body size (alias for `--align-to N`) |
| `--align-block N` | 1..65535 | - | Pad body to next multiple of N bytes |
| `--align-to N` | 1..65535 | - | Force body to exactly N bytes (truncate or pad) |
| `--filler BYTE` | 0..255 or 0x00..0xFF | 0x00 | Padding byte for `--align-*` / `--size` |
| `--header-only` | - | off | Emit only the 128B header, ignore input |
| `--auto-size` | - | on | Header `fsize` field equals the actual body length |
| `--no-auto-size` | - | off | Header `fsize` is taken from `--size N` |
| `--cpm` | - | off | Enable CP/M preset (SOKODI CMT.COM) |
| `--cpm-name NAME` | max 8 ASCII | - | CP/M filename (requires `--cpm`) |
| `--cpm-ext EXT` | max 3 ASCII | `COM` | CP/M extension (requires `--cpm`) |
| `--cpm-attr LIST` | `ro`, `sys`, `arc` (any combination) | - | CP/M attributes (requires `--cpm`) |
| `--version` | - | - | Print tool version and exit |
| `--lib-versions` | - | - | Print linked library versions and exit |
| `-h`, `--help` | - | - | Print help and exit |

Numeric values accept decimal or hexadecimal with `0x` prefix
(for example `0x1200` = `4608`).

### Charset conversion details

The `name` and `comment` fields are stored in Sharp MZ ASCII. The
`--charset` option controls how the input string is converted:

| Value | Meaning |
|-------|---------|
| `eu` | Sharp MZ ASCII European variant (MZ-700/800), input in ASCII/Latin |
| `utf8-eu` | Synonym of `eu`, input in UTF-8 (umlauts, ß, ...) |
| `jp` | Sharp MZ ASCII Japanese variant (MZ-1500) |
| `utf8-jp` | Synonym of `jp`, input in UTF-8 (kanji, katakana) |
| `none` | No conversion, raw bytes (suitable for ASCII names like CP/M) |

The `eu` / `utf8-eu` and `jp` / `utf8-jp` synonyms are semantically
interchangeable - both variants interpret the input as UTF-8.

The default `--charset` is `eu`.

#### The `--upper` option

Applied BEFORE charset conversion. Maps `a-z` to `A-Z` and acts the
same on both `--name` and `--comment`. Default is off.

The `--charset` and `--upper` options are **global** - they cannot be
specified independently for `--name` and `--comment`. The only way to
get per-field behaviour is via escape sequences (see next section): a
field containing `\` automatically disables both conversion and
`--upper` for itself only.

#### Escape sequences in `-n` and `-c`

Both `--name` and `--comment` accept C-style escape sequences:

| Sequence | Meaning |
|----------|---------|
| `\\` | Byte `\` (0x5C) |
| `\n` | Byte LF (0x0A) |
| `\r` | Byte CR (0x0D) |
| `\t` | Byte TAB (0x09) |
| `\xNN` | Arbitrary byte in hex |

If the field contains any escape sequence, both `--charset` and
`--upper` are turned off FOR THAT FIELD (it behaves as `--charset
none`). Granularity is per-field - an escape in `--name` does not
disable conversion of `--comment` and vice versa.

> **Caution:** The `-c` option (short form of `--comment`) is different
> from `-C` (short form of `--charset`). Mind the case.

The 16-byte limit on `--name` is checked **after conversion**. UTF-8
multi-byte sequences may expand or shrink - the byte length after
conversion must be <= 16.

### Body alignment details

Three options control body alignment/size. They are mutually exclusive
- you may use at most one of them:

| Option | Semantics |
|--------|-----------|
| `--align-block N` | Pads the body with zeros (or `--filler`) to the next multiple of N |
| `--align-to N` | Forces an exact size of N bytes (truncate if body is longer, pad if shorter) |
| `-s`, `--size N` | Alias for `--align-to N` (numeric value); `--size auto` = default (no modification) |

The padding byte defaults to `0x00`, can be overridden with
`--filler BYTE` (range 0..255). Using `--filler` without `--align-*`
or `--size` produces a "no effect" warning.

When `--align-to N` (or `--size N`) is smaller than the actual input
size, the body is truncated. The tool emits a warning in that case.

### Header-only mode details

`--header-only` skips reading the input and writes only the 128 B
header. The output size is therefore exactly 128 bytes.

`--auto-size` (default) sets `fsize` in the header to the actual body
length after any alignment. `--no-auto-size` forces the value passed
in `--size N`. The combination `--no-auto-size` without `--size` is a
no-op and produces a warning (auto-size is restored as the default).

In `--header-only` mode, `fsize` is set to 0, or to the value from
`--size N` when combined with `--no-auto-size`.

### CP/M mode details

The `--cpm` option enables the SOKODI CMT.COM convention for transferring
CP/M files inside MZF. Once enabled, the tool applies a preset:

- `ftype` = `0x22` (CP/M convention marker)
- `fstrt` = `fexec` = `0x0100` (CP/M TPA, where .COM files belong)
- `--charset` = `none` (the CP/M filesystem uses ASCII names)

`--cpm` is mutually exclusive with `--type`, `--load-addr`,
`--exec-addr`, `--charset` and `--name`. Any such combination is a
hard error. The `-c`/`--comment` option remains available.

Sub-options:

- `--cpm-name NAME` - max 8 ASCII characters; the tool automatically
  converts to uppercase and pads with spaces. Requires `--cpm`.
- `--cpm-ext EXT` - max 3 ASCII characters, default `COM`. Requires
  `--cpm`.
- `--cpm-attr LIST` - any combination of `ro`, `sys`, `arc`. Items can
  be separated by comma, space or concatenated (`ro,sys`, `ro sys`,
  `roSys`). Case-insensitive. Requires `--cpm`.

Layout of the `fname` field (16 chars + 0x0D terminator) in CP/M mode:

| Offset | Meaning |
|--------|---------|
| 0..7 | CP/M name (8 chars, padded with spaces, ASCII) |
| 8 | `.` (0x2E) - separator |
| 9 | Extension[0] - bit 7 = R/O (Read-Only) |
| 10 | Extension[1] - bit 7 = SYS (System) |
| 11 | Extension[2] - bit 7 = ARC (Archived) |
| 12 | 0x0D (terminator) |

No attribute forces the body size - `--cpm` can be combined with
`--align-block`, `--align-to`, `--size` and `--filler`.

### Input and output details

Input source priority:

1. `--input FILE` (when given)
2. Positional argument `[INPUT]` (when given)
3. `stdin` (when neither of the above is given)

The combination `--input` + positional argument is a hard error.

Output goes to the file given by `--output FILE`, or to stdout if not
given. On MSYS2/Windows the tool switches stdin and stdout to binary
mode (`setmode`) to avoid LF -> CRLF conversion or input being cut off
at byte 0x1A.

The maximum body size is 65535 bytes (limit of the 16-bit `fsize`
field). Larger input results in an error.

## Exit codes

- `0` - success (including `--help`, `--version`, `--lib-versions`
  output).
- non-`0` - error (argument parsing, mutually exclusive options, I/O
  error, allocation, size limit overflow).

## Examples

Basic creation of an OBJ MZF from a binary:

```bash
bin2mzf -n HELLO -l 0x1200 -o hello.mzf hello.bin
```

Named MZF with a comment and a custom execution address:

```bash
bin2mzf -n LOADER -l 0x2000 -e 0x2010 -c "Built 2026-04-28" -o loader.mzf loader.bin
```

Symbolic type (BTX = BASIC text program):

```bash
bin2mzf -n GAME -t btx -l 0x1200 -o game.mzf game.bin
```

JP charset with a Japanese name (UTF-8 input):

```bash
bin2mzf --charset utf8-jp -n "テスト" -l 0xD000 -o test.mzf test.bin
```

CP/M .COM file with R/O+SYS attributes:

```bash
bin2mzf --cpm --cpm-name NEWCCP --cpm-ext COM --cpm-attr ro,sys -o newccp.mzf newccp.bin
```

Aligning to a 256B block with 0xFF filler:

```bash
bin2mzf -n MYBOOT -l 0x2000 --align-block 256 --filler 0xFF -o myboot.mzf myboot.bin
```

Forced body size of 8192 bytes (truncate or pad):

```bash
bin2mzf -n PROG -l 0x1200 --size 8192 -o prog.mzf prog.bin
```

Header-only skeleton (just the 128B header):

```bash
bin2mzf -n EMPTY -l 0x1200 --header-only -o skeleton.mzf
```

Piping stdin -> stdout and inspecting with a sibling tool:

```bash
cat foo.bin | bin2mzf -n FOO -l 0x1200 > foo.mzf
mzf-info foo.mzf
```

## Related tools

- `mzf-info` - inspect an existing MZF (header, hexdump, validation).
- `mzf-hdr` - manipulate the header without touching the body.
- `mzf-strip` - extract the body from an MZF (inverse of `bin2mzf`).
- `mzf-cat` - join several binaries into a single MZF (e.g. CCP+BDOS+BIOS).
- `mzf-paste` - insert data at a given offset of an existing MZF.

## MZF format reference

128-byte header + body (max 65535 bytes):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | `ftype` | File type (0x01 OBJ, 0x02 BTX, 0x03 BSD, 0x04 BRD, 0x05 RB) |
| 0x01 | 16 | `fname.name` | Name (Sharp MZ ASCII) |
| 0x11 | 1 | `terminator` | Name terminator (always 0x0D) |
| 0x12 | 2 | `fsize` | Body size in bytes (LE) |
| 0x14 | 2 | `fstrt` | Z80 load address (LE) |
| 0x16 | 2 | `fexec` | Z80 execution address (LE) |
| 0x18 | 104 | `cmnt` | Comment / reserved space |
