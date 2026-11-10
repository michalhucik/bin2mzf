# Changelog

## 2026-11-10

### bin2mzf-cli v1.0.1 - aktualizace knihovny sharpmz_ascii, opravy dokumentace

Údržbové vydání. Chování nástrojů ani jejich volby se nemění.

- Knihovna `sharpmz_ascii` aktualizována z verze 2.1.1 na 2.2.1
  (převzata z kanonického repozitáře knihovny). Výpis `--lib-versions`
  nyní uvádí `sharpmz_ascii 2.2.1`.
- Dokumentace: opraveny popisy typů souboru `BTX`, `BSD`, `BRD` a `RB`
  v `mzf-info.md` a v příkladech `bin2mzf.md` a `mzf-hdr.md`. Číselné
  kódy ani symbolické názvy se nemění.
- Verze nástrojů: `bin2mzf` 0.2.1, `mzf-info` 0.2.1, `mzf-hdr` 0.1.1,
  `mzf-strip` 0.1.1, `mzf-cat` 0.1.1, `mzf-paste` 0.1.1.

## 2026-04-29

### bin2mzf-cli v1.0.0 - první veřejná verze

První veřejná verze rodiny CLI nástrojů pro tvorbu, inspekci
a manipulaci souborů ve formátu MZF počítačů Sharp MZ (MZ-700,
MZ-800, MZ-1500).

Obsahuje šest nástrojů:

- **`bin2mzf`** v0.2.0 - vytvoření MZF souboru z binárky.
  Pole hlavičky (typ, jméno, load/exec adresa, komentář), konverze
  znakové sady (EU/JP), C-style escape v jméně a komentáři, zarovnání
  těla (`--align-block`, `--align-to`, `-s/--size`), výplň
  (`--filler`), režim pouze hlavička (`--header-only`), konvence
  CP/M se SOKODI atributy (`--cpm`, `--cpm-name`, `--cpm-ext`,
  `--cpm-attr`).
- **`mzf-info`** v0.2.0 - read-only inspekce existujícího MZF.
  Textový výpis hlavičky, hexdump těla, validace, dekódování CP/M
  konvence při `ftype == 0x22`. Strukturovaný výstup pro automatizaci
  (`--format text|json|csv`).
- **`mzf-hdr`** v0.1.0 - modifikace hlavičky existujícího MZF beze
  změny těla. Selektivní update polí, in-place atomická operace
  s podporou `-o` pro nový soubor, plná podpora režimu CP/M, raw
  104bajtový komentář (`--cmnt-bin`).
- **`mzf-strip`** v0.1.0 - extrakce těla z MZF (inverzní operace
  k `bin2mzf`). Striktní bilaterální kontrola velikosti.
- **`mzf-cat`** v0.1.0 - spojení N binárek do jednoho MZF se
  společnou load adresou. Variadické pozičnímu argumenty, zarovnání
  mezi vstupy (`--pad-between`), finální zarovnání těla.
- **`mzf-paste`** v0.1.0 - vložení nebo přepis bloku binárních dat
  na zadaný offset těla existujícího MZF. Operační režimy
  (`--insert`, `--overwrite`, `--extend`), parser ofsetu
  (decimal/hex/keyword `end`), vstup z MZF souboru
  (`--from-mzf`).

Distribuce: instalace přes `make install` (výchozí prefix
`/usr/local/`, konfigurovatelné přes `PREFIX=...`). Dokumentace
v `docs/cz/` a `docs/en/`.

Licence: GPLv3.
