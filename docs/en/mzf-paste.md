# mzf-paste - Insert or Overwrite Data in MZF

Tool version: 0.1.0. Part of the [bin2mzf](https://github.com/michalhucik/bin2mzf/) project.

> **Disclaimer:** The author provides no warranty and accepts no
> liability for data loss or damage resulting from use of the bin2mzf
> project tools. Distributed under the GPLv3 license, without warranty.

Inserts or overwrites a block of binary data at a given offset of the
body of an existing MZF file. The target header (type, name, load/exec
address, comment) is preserved; only the body changes and the `fsize`
field is recomputed to match the new body length. The tool complements
`mzf-cat` (which only appends) with operations in the middle of the body
and `mzf-hdr` (which never touches the body) with targeted binary content
modification.

The typical use case is a hotfix patch, inserting runtime data at a
fixed offset, or embedding an auxiliary table into an existing MZF.
The tool belongs to the project's CLI family alongside `bin2mzf`,
`mzf-info`, `mzf-hdr`, `mzf-strip` and `mzf-cat`. The semantics are
read-modify-write over the body: in in-place mode (no `-o`) the target
file is rewritten atomically via a helper `<TARGET>.tmp` and `rename()`.

## Usage

```
mzf-paste [options] --at OFFSET <INPUT.bin> <TARGET.mzf>
mzf-paste [options] --at OFFSET --from-mzf <INPUT.mzf> <TARGET.mzf>
mzf-paste [options] -o OUTPUT.mzf --at OFFSET <INPUT> <TARGET.mzf>
mzf-paste --version
mzf-paste --lib-versions
mzf-paste --help
```

## Arguments

| Argument | Description |
|----------|-------------|
| `<INPUT>` | Input binary file (or MZF with `--from-mzf`), exactly one positional argument |
| `<TARGET.mzf>` | Target MZF file (modified in place without `-o`), exactly one positional argument |

No `-i`/`--input` flag - paths are passed exclusively as positional
arguments (consistent with `mzf-info`, `mzf-strip` and `mzf-hdr`). No
stdin input.

## Options

### Operation mode and position

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `--insert` | - | on | Insert `INPUT` at `OFFSET`, shift the rest of the body; body grows |
| `--overwrite` | - | off | Overwrite existing bytes from `OFFSET`; mutually exclusive with `--insert` |
| `--extend` | - | off | With `--overwrite`, allow input to extend past the end of the body |
| `--at OFFSET` | 0..0xFFFF or `end` | required | Offset within the target body (decimal, 0x-prefix or keyword `end`) |
| `--filler BYTE` | 0..255 or 0x00..0xFF | 0x00 | Filler byte for the gap (offset > current body size) |

### Input and output

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `--from-mzf` | - | off | `INPUT` is an MZF file - skip the 128-byte header, use only its body |
| `-o`, `--output FILE` | path | in-place | Output MZF file; without this flag `TARGET.mzf` is overwritten |

### Info

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `--version` | - | - | Print the tool version and exit |
| `--lib-versions` | - | - | Print linked library versions and exit |
| `-h`, `--help` | - | - | Print help and exit |

Numeric values (`--at`, `--filler`) accept both decimal and hex with
`0x` prefix (e.g. `0x100` = `256`).

### Operation modes (--insert vs --overwrite)

Three mode combinations determine how the input data is placed into the
target body:

- **`--insert`** (default) - the input is inserted at `OFFSET` and the
  rest of the body from that point is shifted right. The body size grows
  by the input length.
- **`--overwrite`** - the input overwrites existing bytes from `OFFSET`.
  The body size stays the same unless the input would extend past the
  end of the body.
- **`--overwrite --extend`** - a variant of `--overwrite` that allows the
  input to extend past the end of the body. The body grows exactly by
  the part that does not fit inside.

Options `--insert` and `--overwrite` are mutually exclusive. Combining
them ends with a hard error containing the substring `mutually exclusive`.
Without an explicit mode option the default `--insert` applies. The
modifier `--extend` without `--overwrite` has no effect (silent ignore -
no error).

Behaviour by the relation of `OFFSET` to the current body size:

| Mode | `OFFSET < current_size` | `OFFSET == current_size` | `OFFSET > current_size` |
|------|-------------------------|--------------------------|-------------------------|
| `--insert` | inserts, shifts the rest | append at the end | gap-fill `--filler`, then insert |
| `--overwrite` | overwrites inside body | hard error (missing `--extend`) | hard error (missing `--extend`) |
| `--overwrite --extend` | overwrites inside body | append at the end | gap-fill `--filler`, then write |

The hard error for `--overwrite` without `--extend` past the end contains
the substring `use --extend` in the message.

The gap-fill behaviour (`OFFSET > current_size`) is identical in both
cases: the body is first padded with `--filler` bytes from `current_size`
up to `OFFSET`, and only then the input data is written. Default
`--filler` is `0x00`, can be overridden with any value in 0..0xFF. The
gap size counts towards the resulting `fsize` and is subject to the
65535-byte upper limit.

### --at OFFSET details

The `--at` option is required. It accepts three forms:

| Form | Example | Meaning |
|------|---------|---------|
| Decimal | `--at 1024` | Offset in decimal notation, range 0..65535 |
| Hexadecimal | `--at 0x400` | Offset with `0x` prefix, same range |
| Keyword `end` | `--at end` | Resolved to the current body size of the target (= append) |

The `end` form is useful especially for `--insert` (append the input
after the current body) and for `--overwrite --extend` (extend the body
by exactly the input size). The `end` value is resolved after the target
is loaded, so it reflects the actual current `fsize` from the header.
For a plain append operation the result is the same as `--insert --at
end` (recommended combination, since `--insert` is the default mode and
you can write just `--at end`).

If `OFFSET` exceeds the current body size, behaviour follows the matrix
in the section above. The resulting body size must not exceed 65535
bytes (MZF format limit); otherwise the tool ends with a hard error
containing the substring `exceed`.

### Atomic write details

The write operation is atomic regardless of mode. In in-place mode
(no `-o`) the tool never overwrites the target file directly -
instead, it writes the result to a helper file `<TARGET>.tmp`,
renames the finished file via `rename()` to `<TARGET>` and the old
file is atomically replaced. If the write fails midway (for example
when the disk is full), the original `TARGET.mzf` stays unchanged and
`<TARGET>.tmp` is removed/left as a leftover artifact depending on
the error state.

In `-o OUTPUT.mzf` mode the output goes straight into the target
file - `TARGET.mzf` is never touched.

> **Caveat (Win32):** Atomic rename on Windows has a small race
> window between `unlink` of the target and `rename` from tmp to
> target where the target briefly does not exist. On POSIX
> (Linux/MSYS2 via Cygwin runtime) `rename()` is atomic without
> this window. Keep a backup or use `-o` for critical data.

### --from-mzf input details

Without the `--from-mzf` option, `INPUT` is read as a raw binary file -
arbitrary bytes with no interpretation.

With `--from-mzf`, `INPUT` is loaded as an MZF file: the tool validates
the 128-byte header via the `mzf` library and uses only the `body` field
(= the first `header.fsize` bytes of the body) for the paste operation.
The 128-byte header of the input MZF is discarded.

Typical use case: embedding an auxiliary MZF table (for example a font
map, level data or a second program) directly into the body of the host
MZF without first having to extract the MZF via `mzf-strip` into a
temporary binary. The target MZF header is unchanged - only the body is
inserted/overwritten.

If the input MZF has an inconsistent header (for example a wrong name
terminator or `fsize` out of range), `mzf_load` returns an error and
`mzf-paste` exits with a non-zero status before writing the target.
The input therefore behaves as a read-only validated data source.

## Exit codes

- `0` - success (including `--help`, `--version`, `--lib-versions`
  output).
- non-`0` - error (argument parsing, mutually exclusive options, missing
  required `--at`, invalid offset, I/O error, allocation, `--overwrite`
  past-end without `--extend`, resulting body size would exceed 65535
  bytes).

## Examples

Insert a patch at a fixed offset (insert is the default):

```bash
mzf-paste --at 0x100 patch.bin target.mzf
```

Overwrite a metadata block at offset 0x40:

```bash
mzf-paste --overwrite --at 0x40 metadata.bin target.mzf
```

Append at the end of the body (insert with the keyword `end`):

```bash
mzf-paste --at end footer.bin target.mzf
```

Overwrite with permitted body extension:

```bash
mzf-paste --overwrite --extend --at 0x800 newdata.bin target.mzf
```

Insert at an offset past the current body end - the gap is filled with
`--filler`:

```bash
mzf-paste --at 0x500 --filler 0xFF newdata.bin target.mzf
```

Embed the body of another MZF (e.g. a font table) at the end:

```bash
mzf-paste --from-mzf --at end font.mzf main.mzf
```

Write to a new file instead of in-place modification:

```bash
mzf-paste --at 0x100 patch.bin target.mzf -o patched.mzf
```

Pipeline: inspect, modify, inspect again:

```bash
mzf-info target.mzf && mzf-paste --at 0x100 patch.bin target.mzf && mzf-info target.mzf
```

## Related tools

- `bin2mzf` - create an MZF from a binary (rebuild of the whole file).
- `mzf-info` - inspect an existing MZF (header, hexdump, validation).
- `mzf-hdr` - manipulate the header without changing the body.
- `mzf-strip` - extract the body from an MZF (inverse of `bin2mzf`).
- `mzf-cat` - concatenate multiple binaries into one MZF (e.g. CCP+BDOS+BIOS).

## MZF format quick reference

128-byte header + body (max 65535 bytes):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | `ftype` | File type (0x01 OBJ, 0x02 BTX, 0x03 BSD, 0x04 BRD, 0x05 RB) |
| 0x01 | 16 | `fname.name` | Name (Sharp MZ ASCII) |
| 0x11 | 1 | `terminator` | Name terminator (always 0x0D) |
| 0x12 | 2 | `fsize` | Body size in bytes (LE) |
| 0x14 | 2 | `fstrt` | Z80 memory load address (LE) |
| 0x16 | 2 | `fexec` | Z80 memory exec address (LE) |
| 0x18 | 104 | `cmnt` | Comment / reserved space |
