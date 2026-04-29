# bin2mzf

*Tento dokument v [češtině](README_cz.md).*

A family of CLI tools for creating, inspecting and manipulating files
in the MZF format of Sharp MZ computers (MZ-700, MZ-800, MZ-1500, ...).

> **Project status:** the first public release `bin2mzf-cli v1.0.0`
> was published on 2026-04-29
> (see [`docs/en/Changelog.md`](docs/en/Changelog.md)).

## What this project solves

When developing software for the Sharp MZ platform you often need
to "deliver" a compiled and assembled program into the computer's
memory. The most common path is via a cassette tape recorder (CMT),
which requires converting raw binary data into the MZF format used
by every Sharp MZ CMT file. Over the years I kept writing minimal
single-purpose `bin2mzf.c` tools (typically under 100 lines) for
each project I worked on. Each was incomplete in its own way: no
validation, no character set conversion, no inspection, fixed
buffers, an inconsistent CLI.

The `bin2mzf` project replaces all those one-off hacks with a proper
family of tools built on top of the proven `mzf` library from the
[mz800emu](https://sourceforge.net/projects/mz800emu/) project.

## Tools

- **`bin2mzf`** - create an MZF from a binary (load/exec address,
  name, comment, type, UTF-8 -> Sharp MZ ASCII conversion,
  header-only mode, body alignment, CP/M convention with attributes).
- **`mzf-info`** - inspect the header of an existing MZF, validate,
  body hexdump, decode CP/M attributes, JSON/CSV output.
- **`mzf-hdr`** - modify the header without changing the body,
  auto-size.
- **`mzf-strip`** - extract the body from an MZF (inverse of
  bin2mzf).
- **`mzf-cat`** - join several binaries into a single MZF (e.g.
  CCP+BDOS+BIOS for a CP/M loader).
- **`mzf-paste`** - insert or overwrite data at a given offset of
  the body of an existing MZF.

Per-tool user documentation:
[`docs/en/`](docs/en/) (English),
[`docs/cz/`](docs/cz/) (Czech).

## Build and installation

```bash
make                    # libs + CLI tools (build-libs/, build-cli/)
make test               # library unit tests + CLI integration tests
sudo make install       # install into /usr/local/ (default)
make install PREFIX=~/.local   # install into a user prefix
make clean              # clean everything
```

Requires CMake 3.16+, GCC 10+ (or Clang 12+) with C11 support.

`make install` puts the binaries into `$(PREFIX)/bin/` and the user
documentation into `$(PREFIX)/share/doc/bin2mzf/`. The default
`PREFIX` is `/usr/local`.

## License

GPLv3 - see [`LICENSE`](LICENSE).

## Author

**Michal Hučík** - [github.com/michalhucik](https://github.com/michalhucik)
