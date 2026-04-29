# bin2mzf

*Read this in [English](README.md).*

Rodina CLI nástrojů pro tvorbu, inspekci a manipulaci souborů
v MZF formátu počítačů Sharp MZ (MZ-700, MZ-800, MZ-1500, ...).

> **Stav projektu:** první veřejná verze `bin2mzf-cli v1.0.0`
> uvolněna 2026-04-29 (viz [`docs/cz/Changelog.md`](docs/cz/Changelog.md)).

## Co projekt řeší

V různých projektech zaměřených na vývoj SW pro platformu Sharp MZ
je často potřeba po kompilaci a sestavení programu zajistit jeho "dopravu"
do paměti počítače. Jednou z nejběžnějších cest jak toho dosáhnout je
prostřednictvím kazetového magnetofonu (CMT), což vyžaduje převod binárních
dat do formátu MZF, kterým je popsán běžný Sharp MZ CMT soubor.
K tomuto účelu mi v rámci různých projektů vznikala řada minimalistických
`bin2mzf.c` nástrojů (typicky < 100 řádek). Každý byl jinak nekompletní:
žádná validace, žádná konverze znakové sady, žádná inspekce, fixní bufry,
nekonzistentní CLI.

Projekt `bin2mzf` všechny tyto bastly nahrazuje plnohodnotnou rodinou nástrojů
postavenou nad ověřenou knihovnou `mzf` z projektu [mz800emu](https://sourceforge.net/projects/mz800emu/).

## Nástroje

- **`bin2mzf`** - vytvoří MZF z binárky (load/exec adresa, jméno,
  komentář, typ, konverze UTF-8 -> Sharp MZ ASCII, header-only mód,
  zarovnání těla, CP/M konvence s atributy).
- **`mzf-info`** - inspekce hlavičky existujícího MZF, validace,
  hexdump těla, dekódování CP/M atributů, JSON/CSV výstup.
- **`mzf-hdr`** - manipulace hlavičky beze změny těla, auto-size.
- **`mzf-strip`** - extrakce těla z MZF (inverzní operace k bin2mzf).
- **`mzf-cat`** - spojení více binárek do jednoho MZF (např.
  CCP+BDOS+BIOS pro CP/M loader).
- **`mzf-paste`** - vložení nebo přepis dat na zadaný offset těla
  existujícího MZF.

Uživatelská dokumentace per nástroj: [`docs/cz/`](docs/cz/) (česky),
[`docs/en/`](docs/en/) (anglicky).

## Build a instalace

```bash
make                    # libs + CLI nástroje (build-libs/, build-cli/)
make test               # unit testy knihoven + CLI integrační testy
sudo make install       # instalace do /usr/local/ (default)
make install PREFIX=~/.local   # instalace do uživatelského prefixu
make clean              # vyčistí vše
```

Vyžaduje CMake 3.16+, GCC 10+ (nebo Clang 12+) s podporou C11.

`make install` umístí binárky do `$(PREFIX)/bin/` a uživatelskou
dokumentaci do `$(PREFIX)/share/doc/bin2mzf/`. Výchozí `PREFIX` je
`/usr/local`.

## Licence

GPLv3 - viz [`LICENSE`](LICENSE).

## Autor

**Michal Hučík** - [github.com/michalhucik](https://github.com/michalhucik)
