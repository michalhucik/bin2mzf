# Changelog

## 2026-11-10

### bin2mzf-cli v1.0.1 - sharpmz_ascii library update, documentation fixes

Maintenance release. The behaviour of the tools and their options is
unchanged.

- The `sharpmz_ascii` library was updated from version 2.1.1 to 2.2.1
  (taken from the canonical repository of the library). The
  `--lib-versions` output now reports `sharpmz_ascii 2.2.1`.
- Documentation: fixed the descriptions of the `BTX`, `BSD`, `BRD` and
  `RB` file types in `mzf-info.md` and in the examples of `bin2mzf.md`
  and `mzf-hdr.md`. Numeric codes and symbolic names are unchanged.
- Tool versions: `bin2mzf` 0.2.1, `mzf-info` 0.2.1, `mzf-hdr` 0.1.1,
  `mzf-strip` 0.1.1, `mzf-cat` 0.1.1, `mzf-paste` 0.1.1.

## 2026-04-29

### bin2mzf-cli v1.0.0 - first public release

The first public release of the family of CLI tools for creating,
inspecting and manipulating files in the MZF format of Sharp MZ
computers (MZ-700, MZ-800, MZ-1500).

Includes six tools:

- **`bin2mzf`** v0.2.0 - create an MZF file from a binary. Header
  fields (type, name, load/exec address, comment), character set
  conversion (EU/JP), C-style escapes in name and comment, body
  alignment (`--align-block`, `--align-to`, `-s/--size`), filler
  (`--filler`), header-only mode (`--header-only`), CP/M convention
  with SOKODI attributes (`--cpm`, `--cpm-name`, `--cpm-ext`,
  `--cpm-attr`).
- **`mzf-info`** v0.2.0 - read-only inspection of an existing MZF.
  Textual header dump, body hexdump, validation, CP/M decoding when
  `ftype == 0x22`. Structured output for automation
  (`--format text|json|csv`).
- **`mzf-hdr`** v0.1.0 - modify the header of an existing MZF
  without touching the body. Selective field update, in-place atomic
  operation with `-o` support for a new file, full CP/M mode
  support, raw 104-byte comment (`--cmnt-bin`).
- **`mzf-strip`** v0.1.0 - extract the body from an MZF (inverse of
  `bin2mzf`). Strict bilateral size check.
- **`mzf-cat`** v0.1.0 - join several binaries into a single MZF
  with a shared load address. Variadic positional arguments,
  inter-input padding (`--pad-between`), final body alignment.
- **`mzf-paste`** v0.1.0 - insert or overwrite a block of binary
  data at a given offset of the body of an existing MZF. Operation
  modes (`--insert`, `--overwrite`, `--extend`), offset parser
  (decimal/hex/keyword `end`), MZF input
  (`--from-mzf`).

Distribution: installation via `make install` (default prefix
`/usr/local/`, configurable via `PREFIX=...`). Documentation in
`docs/cz/` and `docs/en/`.

License: GPLv3.
