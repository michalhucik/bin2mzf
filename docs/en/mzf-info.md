# mzf-info - MZF File Inspection

Tool version: 0.2.0. Part of the [bin2mzf](https://github.com/michalhucik/bin2mzf/) project.

> **Disclaimer:** The author provides no warranty and accepts no
> liability for data loss or damage resulting from use of the bin2mzf
> project tools. Distributed under the GPLv3 license, without warranty.

A read-only inspection tool for MZF files (Sharp MZ tape format). Prints
the contents of the 128-byte header in a human-readable form, performs
a body hexdump, validates basic structural rules (name terminator, size
match) and optionally decodes the CP/M (SOKODI CMT.COM) layout for files
with `ftype == 0x22`.

The tool belongs to the project's CLI family alongside `bin2mzf`,
`mzf-hdr`, `mzf-strip`, `mzf-cat` and `mzf-paste`. It is the counterpart
to `bin2mzf` - where `bin2mzf` builds an MZF, `mzf-info` examines it.
Structured output (`--format json|csv`) is intended for CI pipelines,
test scripts and `jq` filters. The `--validate` switch returns only an
exit code and is handy as a quick gate in shell scripts.

## Usage

```
mzf-info [INPUT.mzf]
mzf-info --header-only [INPUT.mzf]
mzf-info --hexdump [--offset N] [--length N] [INPUT.mzf]
mzf-info --validate [INPUT.mzf]
mzf-info --format json [INPUT.mzf]
mzf-info --version
mzf-info --lib-versions
mzf-info --help
```

## Arguments

| Argument | Description |
|----------|-------------|
| `[INPUT.mzf]` | Input MZF file (optional; if missing, stdin is used) |

## Options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-C`, `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Charset used to decode `name` and `comment` |
| `--header-only` | - | off | Print header text only (no body hexdump) |
| `--body-only` | - | off | Print body hexdump only (no header text) |
| `--hexdump` | - | off | Append body hexdump after the header text |
| `--offset N` | 0..0xFFFF | 0 | Hexdump start offset within the body |
| `--length N` | 0..0xFFFF | full body | Hexdump length in bytes |
| `--validate` | - | off | Quiet mode; exit 0 if MZF is valid, 1 otherwise |
| `--no-cpm-decode` | - | decoding enabled | Disable CP/M decoding when `ftype == 0x22` |
| `--format MODE` | `text`, `json`, `csv` | `text` | Output format |
| `--version`, `-V` | - | - | Print tool version and exit |
| `--lib-versions` | - | - | Print linked library versions and exit |
| `-h`, `--help` | - | - | Print help and exit |

Numeric values (`--offset`, `--length`) accept decimal or hexadecimal
with `0x` prefix (for example `0x100` = `256`).

The `--header-only`, `--body-only` and `--validate` options are mutually
exclusive. `--validate` additionally cannot be combined with `--hexdump`.
The `--format json` and `--format csv` modes cannot be combined with
`--hexdump` or `--body-only`. See below for details.

### Character set conversion details

The `name` (16 bytes) and `comment` (104 bytes) header fields are stored
in Sharp MZ ASCII. The `--charset` option controls how they are
interpreted on display:

| Value | Meaning |
|-------|---------|
| `eu` | Sharp MZ ASCII European variant (MZ-700/800), UTF-8 output |
| `utf8-eu` | Synonym of `eu` |
| `jp` | Sharp MZ ASCII Japanese variant (MZ-1500), UTF-8 output |
| `utf8-jp` | Synonym of `jp` |
| `none` | No conversion - the decoded string is replaced by `(raw bytes only)` |

The `eu` / `utf8-eu` and `jp` / `utf8-jp` synonyms are implementation
unified and behave identically. The `eu` default matches the most common
hardware (MZ-700/800).

With `--charset none` the text output shows `Name: (raw bytes only)`
and `Comment: (raw bytes only)`, while the raw hex bytes are printed
unchanged. In structured output (`--format json|csv`) the `name` and
`comment` keys come out as empty strings.

### Header inspection details

The default (text) mode prints the header contents in a fixed line
order: file type, name (UTF-8 + 16 raw bytes), optional CP/M section,
`fsize`, `fstrt` (load), `fexec` (exec), comment (UTF-8 + first 16 raw
bytes), body checksums (`sum8`, `xor`) and the validation section.

For the `ftype` field a symbolic name is printed alongside the hex
value:

| `ftype` | Symbol | Description |
|---------|--------|-------------|
| 0x01 | `OBJ` | Machine code (object file) |
| 0x02 | `BTX` | BASIC text program |
| 0x03 | `BSD` | BASIC data file |
| 0x04 | `BRD` | BASIC read-after-run data file |
| 0x05 | `RB` | Read and branch (auto-run) |
| 0x22 | `CPM` | CP/M convention (SOKODI CMT.COM) |
| other | `unknown` | Unrecognized type |

The comment is printed in full (104 bytes) on the UTF-8 line, while
the raw hex line is limited to the first 16 bytes for readability.

### Body hexdump details

The `--hexdump` option (header text plus dump) and `--body-only` (dump
only) emit the body in a layout close to `od -A x -t x1z`:

```
00000000  4d 5a 46 20 73 61 6d 70  6c 65 20 64 61 74 61 21  |MZF sample data!|
```

- 8-digit hex offset relative to the **start of the body** (not to the
  file and not to the displayed range).
- 16 hex bytes per row, with an extra space between the 8th and 9th byte.
- ASCII gutter between `|...|`; bytes outside `0x20..0x7e` render as
  `.`.

The `--offset N` and `--length N` options bound the displayed range:
`--offset` shifts the start (default 0), `--length` limits the count
(default the full body). Values outside the body produce an error
(exit 1). Using `--offset` or `--length` without `--hexdump` or
`--body-only` produces a "no effect" warning and the program continues.

### Validation details

The `--validate` option enables quiet mode. The tool loads the MZF,
verifies two conditions and returns an exit code:

1. The name terminator at offset 0x11 equals `0x0D`.
2. The actual file size matches `128 + fsize` exactly.

If both hold, exit is 0 and stdout is empty. If either fails, exit is 1
and one line on stderr describes the reason (missing terminator,
truncated body or trailing bytes).

This contract is intended for CI pipelines and shell scripts. Example:
`mzf-info --validate hello.mzf || echo INVALID`.

`--validate` is mutually exclusive with `--header-only`, `--body-only`
and `--hexdump`. Combination with `--format json|csv` is accepted
syntactically, but `--validate` takes precedence over formatting - no
JSON nor CSV is emitted, only the exit code is returned.

### CP/M decoding details

When `ftype == 0x22` (the SOKODI CMT.COM convention marker) the tool
automatically decodes the CP/M name, extension and attributes from the
`fname` field. Detection requires three conditions to hold simultaneously:

1. `ftype == 0x22`.
2. `fname.name[8] == '.'` (name/extension separator).
3. `fname.name[12] == 0x0D` or `== 0x00` (name terminator).

If any condition fails, the CP/M section is skipped silently - this
allows third-party use of the value 0x22 without false positives.

On successful detection three lines appear in the text output, parallel
to `Name:` and the raw bytes:

```
CP/M name:     "CCP"
CP/M ext:      "COM"
CP/M attrs:    R/O,SYS
```

Attributes are encoded in bit 7 of the three extension bytes - bit 7
of the first = R/O, of the second = SYS, of the third = ARC. With a
zero mask the line shows `(none)`. Bit 7 is masked off before display
so the CP/M ext is plain ASCII.

The `--no-cpm-decode` option disables decoding. Useful for MZF files
that hold `ftype == 0x22` for other reasons, or when you want to see
the raw header without interpretation.

### Structured output details

The `--format` option switches output between text (default), JSON and
CSV. The structured formats are intended for machine processing - `jq`
pipelines, parsers in tests, exports to spreadsheets.

Key map (snake_case, flat - no nesting):

| Key | Type | Always/CP/M | Description | Example |
|-----|------|-------------|-------------|---------|
| `ftype` | uint | always | Decimal `ftype` value (0..255) | `1` |
| `ftype_hex` | string | always | Redundant hex `"0xHH"` | `"0x01"` |
| `ftype_symbol` | string | always | `OBJ`, `BTX`, `BSD`, `BRD`, `RB`, `CPM`, `unknown` | `"OBJ"` |
| `name` | string | always | UTF-8 name (empty when `--charset none`) | `"HELLO"` |
| `cpm_name` | string | CP/M only | CP/M name without padding (max 8 chars) | `"CCP"` |
| `cpm_ext` | string | CP/M only | CP/M extension with bit 7 stripped (max 3 chars) | `"COM"` |
| `cpm_attr_ro` | string | CP/M only | `"true"` / `"false"` - R/O attribute | `"true"` |
| `cpm_attr_sys` | string | CP/M only | `"true"` / `"false"` - SYS attribute | `"false"` |
| `cpm_attr_arc` | string | CP/M only | `"true"` / `"false"` - ARC attribute | `"false"` |
| `cpm_attrs` | string | CP/M only | Aggregate `"R/O,SYS,ARC"` (may be `""`) | `"R/O,SYS"` |
| `fsize` | uint | always | Body size in bytes | `4096` |
| `fstrt` | string | always | Hex `"0xHHHH"` - Z80 load address | `"0x1200"` |
| `fexec` | string | always | Hex `"0xHHHH"` - Z80 execution address | `"0x1200"` |
| `comment` | string | always | Full 104-byte UTF-8 comment (empty under `none`) | `"Built 2026-04-28"` |
| `body_sum8` | uint | always | Modulo 256 byte sum of the body | `42` |
| `body_xor` | uint | always | XOR fold of the body bytes | `0` |
| `header_terminator_ok` | string | always | `"true"` / `"false"` - 0x0D terminator | `"true"` |
| `size_match` | string | always | `"ok"` / `"trailing_bytes"` / `"truncated"` | `"ok"` |
| `size_expected` | uint | always | `128 + fsize` | `4224` |
| `size_actual` | uint | always | Actual input size | `4224` |
| `valid` | string | always | `"true"` / `"false"` - aggregate `header_terminator_ok && size_match=="ok"` | `"true"` |

> **Caution:** Boolean-valued keys (`cpm_attr_ro`, `cpm_attr_sys`,
> `cpm_attr_arc`, `header_terminator_ok`, `valid`) are represented as
> STRING `"true"` / `"false"` in both JSON and CSV, not as JSON bool.
> Consumers must compare strings, not type-cast to bool.

> **Caution:** Hex-valued keys (`ftype_hex`, `fstrt`, `fexec`) are
> STRINGS formatted as `"0xHH"` resp. `"0xHHHH"`. Numeric counts
> (`ftype`, `fsize`, `body_sum8`, `body_xor`, `size_expected`,
> `size_actual`) are JSON UINTs and unquoted in CSV.

Keys in the "CP/M only" rows are emitted only if the header decodes
as a SOKODI layout (see `### CP/M decoding details`) and
`--no-cpm-decode` is not active. Otherwise they are absent from JSON
and CSV output - you do not get them with empty values.

The CSV format uses a lazy `key,value` header (auto-emitted before the
first row) and applies RFC 4180 escaping for values containing comma,
quotes or newline.

Combining `--format json|csv` with `--hexdump` or `--body-only` is a
hard error ("incompatible with --format"). The hexdump is intentionally
omitted from structured formats - the goal there is tabular metadata,
not raw content.

A single-line JSON output sample (truncated):

```
{"ftype":1,"ftype_hex":"0x01","ftype_symbol":"OBJ","name":"HELLO","fsize":4096,"fstrt":"0x1200","fexec":"0x1200","comment":"","body_sum8":42,"body_xor":0,"header_terminator_ok":"true","size_match":"ok","size_expected":4224,"size_actual":4224,"valid":"true"}
```

### Input details

When the positional `[INPUT.mzf]` argument is omitted, the tool reads
the MZF from stdin. This enables pipelines like `cat foo.mzf | mzf-info`
or `bin2mzf ... | mzf-info`. On MSYS2/Windows both stdin and stdout
are switched to binary mode (`setmode`) to avoid LF -> CRLF conversion
or input being cut off at byte 0x1A.

The maximum input size is 65663 bytes (128 header + 65535 body). Larger
inputs are an error. A truncated body (input shorter than `128 + fsize`)
can still be loaded; in text mode it is reported as
`File size match: ERROR (truncated)` and `--validate` returns exit 1.

## Exit codes

- `0` - success (including `--help`, `--version`, `--lib-versions` and
  `--validate` on a valid MZF).
- non-`0` - error (argument parsing, mutually exclusive options, I/O
  error, allocation, input outside limits, `--validate` on an invalid
  MZF, hexdump range outside the body).

## Examples

Basic header dump:

```bash
mzf-info hello.mzf
```

Reading from stdin via pipe:

```bash
cat hello.mzf | mzf-info
```

Japanese charset for an MZ-1500 game:

```bash
mzf-info --charset utf8-jp game.mzf
```

Header only, no hexdump (cleaner output):

```bash
mzf-info --header-only hello.mzf
```

Hexdump of the first 64 body bytes:

```bash
mzf-info --body-only --length 64 hello.mzf
```

Hexdump of a range starting at offset 0x100, 64 bytes:

```bash
mzf-info --body-only --offset 0x100 --length 64 hello.mzf
```

Validation in a CI script (exit code gate):

```bash
mzf-info --validate hello.mzf || echo INVALID
```

JSON output piped to a `jq` filter:

```bash
mzf-info --format json hello.mzf | jq '.fsize'
```

JSON validation in CI:

```bash
mzf-info --format json hello.mzf | jq -r '.valid'
```

CP/M file (automatic decoding when `ftype == 0x22`):

```bash
mzf-info ccp.mzf
```

## Related tools

- `bin2mzf` - build an MZF from a binary (inverse of `mzf-strip`).
- `mzf-hdr` - manipulate the header without touching the body.
- `mzf-strip` - extract the body from an MZF.
- `mzf-cat` - join several binaries into a single MZF (e.g. CCP+BDOS+BIOS).
- `mzf-paste` - insert data at a given offset of an existing MZF.

## MZF format quick reference

128-byte header + body (max 65535 bytes):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | `ftype` | File type (0x01 OBJ, 0x02 BTX, 0x03 BSD, 0x04 BRD, 0x05 RB, 0x22 CPM) |
| 0x01 | 16 | `fname.name` | Name (Sharp MZ ASCII, possibly CP/M 8.3 layout) |
| 0x11 | 1 | `terminator` | Name terminator (always 0x0D) |
| 0x12 | 2 | `fsize` | Body size in bytes (LE) |
| 0x14 | 2 | `fstrt` | Z80 load address (LE) |
| 0x16 | 2 | `fexec` | Z80 execution address (LE) |
| 0x18 | 104 | `cmnt` | Comment / reserved space |
