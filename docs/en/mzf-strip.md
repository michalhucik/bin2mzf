# mzf-strip - MZF Body Extraction

Tool version: 0.1.0. Part of the [bin2mzf](https://github.com/michalhucik/bin2mzf/) project.

> **Disclaimer:** The author provides no warranty and accepts no
> liability for data loss or damage resulting from use of the bin2mzf
> project tools. Distributed under the GPLv3 license, without warranty.

Read-only extraction tool for MZF files (the tape format used by
Sharp MZ computers). Reads an MZF file (from a positional argument or
stdin), validates that the input size matches the `fsize` field in
the header, and writes the raw body bytes (offset 128 .. 128+fsize)
to stdout or to the file given by `-o`. The header itself is not
processed - it is simply skipped over.

`mzf-strip` is the inverse operation of `bin2mzf`: where `bin2mzf`
prepends a 128-byte MZF header to a binary, `mzf-strip` strips it back
off. The typical use case is round-trip verification of a build
pipeline (`bin2mzf` -> `mzf-strip` -> `cmp` against the original
binary). The tool is part of the `bin2mzf` CLI family alongside
`bin2mzf`, `mzf-info`, `mzf-hdr`, `mzf-cat`, and `mzf-paste`. The
input MZF is never modified - the only output is the extracted raw
body data.

## Usage

```
mzf-strip [-o OUTPUT] <INPUT.mzf>
mzf-strip [-o OUTPUT] < INPUT.mzf
mzf-strip --version
mzf-strip --lib-versions
mzf-strip --help
```

## Arguments

| Argument | Description |
|----------|-------------|
| `<INPUT.mzf>` | Input MZF file (optional; if omitted, input is read from stdin) |

There is no `-i`/`--input` flag - the input MZF path is given solely
as a positional argument (consistent with `mzf-info` and `mzf-hdr`).
Without a positional argument, input is read from stdin.

## Options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `-o`, `--output FILE` | path to a file | stdout | Write the extracted body to `FILE` instead of stdout |
| `--version` | - | - | Print tool version and exit |
| `--lib-versions` | - | - | Print versions of linked libraries and exit |
| `-h`, `--help` | - | - | Print help and exit |

`mzf-strip` deliberately has no `--charset`, `--format`, `--cpm*`, or
`--align-*` / `--filler` options - extraction produces 1:1 raw bytes
with no transformation or interpretation of the data. Use `mzf-info`
for header display or structured output.

### Strict size check details

The key semantics of the tool: the input must be exactly `128 + fsize`
bytes, where `fsize` is the 16-bit little-endian field in the header
at offset 0x12. Both truncated (shorter) and oversized (longer) inputs
are rejected with an `input size mismatch` error on stderr and exit
code 1. `mzf-strip` does not read `fsize` via the library `mzf_load`
function - it reads bytes 0x12..0x13 directly in LE order, which lets
it implement its own strict size detection without depending on the
library layer's behavior for truncated bodies.

| Input situation | Behavior |
|-----------------|----------|
| `n == 128 + fsize` | OK, `fsize` body bytes are written, exit 0 |
| `n < 128` | Error "input shorter than MZF header", exit 1 |
| `n != 128 + fsize` | Error `input size mismatch`, exit 1 |
| `n > 65663` | Error `input size mismatch` (overflow detection), exit 1 |
| `fsize == 0` | Empty output (no bytes), exit 0 |

The reason for the strictness is integrity checking of the MZF before
extraction - if the file contains trailing bytes or has been
truncated, the resulting binary would not be identical to what
`bin2mzf` originally produced. Round-trip verification via `cmp`
would fail in that case, so the strict check catches the problem at
extraction time rather than only at the comparison stage.

For lenient handling of invalid MZFs or for inspection, use
`mzf-info`, which can load inputs with mismatches and describe them
in detail (including distinguishing truncated from trailing bytes
through `--validate` or `--format json`). `mzf-strip`, by contrast,
has a binary contract: either the input is valid and extraction
proceeds, or it exits with code 1.

The special case `fsize == 0` (typically MZFs created via
`bin2mzf --header-only`, that is, a header "skeleton" with no body)
results in empty output and exit code 0. A round-trip through
`mzf-strip` for such MZFs passes and produces a file of zero size
(analogous to `cat /dev/null > out`). Implementation-wise,
`fwrite(buf, 1, 0, fp)` is explicitly skipped because some libc
implementations spuriously set `ferror` on such a call.

### Input and output details

Input is read either from the positional argument (`<INPUT.mzf>`) or
from stdin if the positional argument is missing. Output goes by
default to stdout or to the file given by `-o, --output FILE`.
Pipelining (both stdin and stdout) is the main supported workflow -
it allows chaining `mzf-strip` with other tools (`sha256sum`, `xxd`,
`od`, `cmp`, further build pipeline steps) without intermediate
files.

The input MZF is never modified - the operation is strictly
read-only. The file is opened in `"rb"` (read binary) mode and closed
immediately after loading into the in-memory buffer.

On MSYS2/Windows the tool switches stdin and stdout into binary mode
via `setmode` to avoid LF -> CRLF conversion on output and to avoid
truncation of input at byte 0x1A (DOS EOF). Without this, a stdin
pipe on Windows would truncate the binary at the first 0x1A and
stdout would double every LF into CRLF, which would destroy any
binary output.

The maximum input size is 65663 bytes (128 header + 65535 body). A
larger input is caught by an internal overflow detector (buffer
capacity 65664 = 128 + 65535 + 1) and rejected with an `input size
mismatch` error. The body is never allocated larger than
`MZF_MAX_BODY_SIZE` (0xFFFF), which is the limit of the 16-bit
`fsize` field. The buffer for the entire input is allocated on the
heap via `malloc` and freed before returning from `main`.

## Exit codes

| Code | Description |
|------|-------------|
| 0 | Success (including `--help`, `--version`, `--lib-versions`, and `fsize == 0` empty output) |
| 1 | I/O error, size mismatch, input shorter than header, buffer allocation failure, unknown option |

## Examples

The examples below assume that the `bin2mzf` and `mzf-strip` binaries
from the project's portable distribution are on your PATH, plus
standard Unix utilities (`cat`, `cmp`, `grep`, `sha256sum`).

Round-trip verification (primary use case - extract the body and
compare against the original binary):

```bash
bin2mzf -n PROG -l 0x1200 -i prog.bin -o prog.mzf
mzf-strip prog.mzf > prog.recovered.bin
cmp prog.bin prog.recovered.bin && echo OK
```

Extract to a file via `-o`:

```bash
mzf-strip prog.mzf -o prog.bin
```

Pipe stdin -> stdout:

```bash
cat prog.mzf | mzf-strip > prog.bin
```

Header-only MZF -> empty body (the `fsize == 0` edge case):

```bash
bin2mzf -n SKEL -l 0x1200 --header-only -o skel.mzf
mzf-strip skel.mzf > /dev/null   # exit 0, no output
```

Combine with `mzf-info` to inspect the header before extraction:

```bash
mzf-info prog.mzf && mzf-strip prog.mzf -o prog.bin
```

Extract and hash the body for a CI checksum:

```bash
mzf-strip prog.mzf | sha256sum
```

Detect an invalid input (size mismatch as a shell guard):

```bash
mzf-strip broken.mzf 2>&1 | grep -q "input size mismatch" && echo "INVALID"
```

## Related tools

- `bin2mzf` - create an MZF from a binary (inverse of `mzf-strip`).
- `mzf-info` - inspect the header and hexdump the body of an MZF (read-only).
- `mzf-hdr` - modify the header without touching the body.
- `mzf-cat` - concatenate multiple binaries into a single MZF (e.g. CCP+BDOS+BIOS).
- `mzf-paste` - paste data at a given offset of an existing MZF.

## MZF format quick reference

128 bytes of header + body (max 65535 bytes):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | `ftype` | File type (0x01 OBJ, 0x02 BTX, 0x03 BSD, 0x04 BRD, 0x05 RB, 0x22 CPM) |
| 0x01 | 16 | `fname.name` | Name (Sharp MZ ASCII, possibly CP/M 8.3 layout) |
| 0x11 | 1 | `terminator` | Name terminator (always 0x0D) |
| 0x12 | 2 | `fsize` | Body size in bytes (LE) |
| 0x14 | 2 | `fstrt` | Load address in Z80 memory (LE) |
| 0x16 | 2 | `fexec` | Execution address in Z80 memory (LE) |
| 0x18 | 104 | `cmnt` | Comment / reserved space |
