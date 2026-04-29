# mzf-cat - Concatenate Binaries into MZF

Tool version: 0.1.0. Part of the [bin2mzf](https://github.com/michalhucik/bin2mzf/) project.

> **Disclaimer:** The author provides no warranty and accepts no
> liability for data loss or damage resulting from use of the bin2mzf
> project tools. Distributed under the GPLv3 license, without warranty.

Joins several binary files into a single MZF with one shared load
address. The tool reads all positional inputs in order, optionally
inserts padding between non-last inputs (`--pad-between`), optionally
applies a final alignment to the resulting body (`--align-block` /
`--align-to` / `--size`), and assembles a single shared header. The
body is laid out as a contiguous block in Z80 memory.

The primary use case is a CP/M loader composed of CCP + BDOS + BIOS
that must follow each other in memory. Another typical use is joining
code/data segments into a single portable tape file. The tool belongs
to the `bin2mzf` CLI family alongside `bin2mzf`, `mzf-info`, `mzf-hdr`,
`mzf-strip` and `mzf-paste`. It serves as a specialized counterpart to
`bin2mzf` - while `bin2mzf` handles a single binary, `mzf-cat` handles
any number of binaries one after another.

## Usage

```
mzf-cat -n NAME -l LOAD_ADDR [options] -o OUTPUT.mzf INPUT1.bin [INPUT2.bin ...]
mzf-cat --version
mzf-cat --lib-versions
mzf-cat --help
```

## Arguments

| Argument | Description |
|----------|-------------|
| `INPUT1.bin [INPUT2.bin ...]` | Input binary files (at least 1, variadic - more inputs allowed) |

## Options

### Required options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-n`, `--name NAME` | text (max 16 B after conversion) | - | File name written into the MZF header |
| `-l`, `--load-addr ADDR` | 0..0xFFFF | - | Z80 load address (decimal or 0x-prefixed) |
| `-o`, `--output FILE` | path | stdout | Output MZF file |

### Header options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-t`, `--type TYPE` | 0x01..0xFF or `obj`, `btx`, `bsd`, `brd`, `rb` | `obj` (0x01) | File type |
| `-c`, `--comment TEXT` | text (max 104 B after conversion) | - | Comment text (longer is truncated) |
| `-e`, `--exec-addr ADDR` | 0..0xFFFF | equal to `--load-addr` | Z80 execution address |

### Body layout options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `--pad-between N` | 1..65535 | - | Before each non-last input pad body to a multiple of N |
| `--align-block N` | 1..65535 | - | Pad final body to next multiple of N bytes |
| `--align-to N` | 1..65535 | - | Force final body to exactly N bytes (truncate or pad) |
| `-s`, `--size N\|auto` | 1..65535 or `auto` | `auto` | Alias for `--align-to N` (numeric value) |
| `--filler BYTE` | 0..255 or 0x00..0xFF | 0x00 | Padding byte for `--pad-between` and final align |

### Charset options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-C`, `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Charset for `--name` and `--comment` |
| `--upper` | - | off | Uppercase a-z to A-Z before charset conversion |

### Info options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `--version` | - | - | Print tool version and exit |
| `--lib-versions` | - | - | Print linked library versions and exit |
| `-h`, `--help` | - | - | Print help and exit |

Numeric values accept decimal or hexadecimal with `0x` prefix
(for example `0x1200` = `4608`).

### Variadic inputs

`mzf-cat` accepts inputs only as positional arguments. There is no
`-i`/`--input` flag (unlike `bin2mzf`) - multi-input use would make
such a flag awkward without benefit. At least one input is required;
the upper limit is bounded only by the total body size (<= 65535 B).

The tool's optstring is POSIX-strict (leading `+`), which means all
options must precede the positional arguments. Once getopt reaches a
non-option argument it stops parsing and treats the rest as inputs.
A command like `mzf-cat input.bin -n NAME -l 0x1200 -o out.mzf` is
therefore invalid - `-n`, `-l` and `-o` would be interpreted as more
positional inputs. Missing all positional inputs results in an error
with the text "at least one input file is required".

### Operation order

The MZF body is assembled in a fixed order:

```
input1 -> [pad-between filler] -> input2 -> ... -> [pad-between filler] -> inputN -> [final align]
```

In detail:

1. `input1` is read as the first body block.
2. If `--pad-between N` is set and the current body offset is not a
   multiple of N, `--filler` bytes are inserted up to the next multiple
   of N. If the offset already aligns, padding is zero (`modv == 0` ->
   0 bytes).
3. `input2` is read, then optionally pad-between and the rest of the
   inputs, until all inputs are processed.
4. After the last input, pad-between is **not** applied (the last input
   has no successor to pad against).
5. If any of `--align-block`/`--align-to`/`--size` is set, the final
   alignment is applied to the resulting body.

The maximum body size is 65535 bytes. If the sum of all inputs together
with pad-between or final padding exceeds the limit, the tool aborts
with a hard error containing the substring `exceeds maximum body size`.

`--pad-between` and the final alignment are orthogonal - they may be
combined (typically `--pad-between` aligns individual modules to a
block and `--align-block` aligns the whole result to a target memory
boundary).

### Body alignment details

The options for **final** body alignment are three and they are mutually
exclusive - at most one may be used. Combining them is a hard error
with the substring `mutually exclusive`:

| Option | Semantics |
|--------|-----------|
| `--align-block N` | Pads the final body with zeros (or `--filler`) to the next multiple of N |
| `--align-to N` | Forces an exact size of N bytes (truncate if longer, pad if shorter) |
| `-s`, `--size N` | Alias for `--align-to N` (numeric value); `--size auto` = default (no modification) |

The padding byte defaults to `0x00`, can be overridden with
`--filler BYTE` (range 0..255 or 0x00..0xFF). The same `--filler`
applies to `--pad-between` as well.

When `--align-to N` (or `--size N`) is smaller than the actual
concatenated body size, the body is truncated. The tool emits a warning
in that case with the substring `truncated`.

`--pad-between N` is **not** part of the 3-way mutex - it is orthogonal
to all three final modes and can be combined with any of them.

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

The `--upper` option is applied BEFORE charset conversion. It maps
`a-z` to `A-Z` and acts the same on both `--name` and `--comment`.
Default is off.

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

## Exit codes

- `0` - success (including `--help`, `--version`, `--lib-versions`
  output).
- non-`0` - error (argument parsing, mutually exclusive options,
  missing positional input, I/O error, allocation, body size limit
  overflow).

## Examples

CP/M loader (primary use case) - join CCP + BDOS + BIOS into a single
MZF sharing one load address:

```bash
mzf-cat -n LOADER -l 0xD400 -o cpm.mzf ccp.bin bdos.bin bios.bin
```

Single input (behaves equivalently to `bin2mzf`):

```bash
mzf-cat -n PROG -l 0x1200 -o prog.mzf prog.bin
```

Pad-between two modules to a 256B block boundary:

```bash
mzf-cat -n LOADER -l 0xD400 --pad-between 256 -o loader.mzf part1.bin part2.bin
```

Forced final body size of 8192 bytes (truncate or pad):

```bash
mzf-cat -n PROG -l 0x1200 -s 8192 -o prog.mzf prog1.bin prog2.bin
```

Final alignment to a 1024B block with 0xFF filler:

```bash
mzf-cat -n PROG -l 0x1200 --align-block 1024 --filler 0xFF -o prog.mzf p1.bin p2.bin
```

JP charset with a Japanese name (UTF-8 input):

```bash
mzf-cat --charset utf8-jp -n "ゲーム" -l 0x2000 -o game.mzf part1.bin part2.bin
```

Pad-between and final align together (orthogonal combination):

```bash
mzf-cat -n PROG -l 0x1200 --pad-between 256 --align-block 1024 -o prog.mzf p1.bin p2.bin
```

Inspect the output with a sibling tool right after building it:

```bash
mzf-cat -n LOADER -l 0xD400 -o loader.mzf ccp.bin bdos.bin && mzf-info loader.mzf
```

## Related tools

- `bin2mzf` - build an MZF from a single binary (full file rebuild).
- `mzf-info` - inspect an existing MZF (header, hexdump, validation).
- `mzf-hdr` - manipulate the header without touching the body.
- `mzf-strip` - extract the body from an MZF (inverse of `bin2mzf`).
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
