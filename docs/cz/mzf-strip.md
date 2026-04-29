# mzf-strip - Extrakce těla z MZF souboru

Verze nástroje: 0.1.0. Součást projektu [bin2mzf](https://github.com/michalhucik/bin2mzf/).

> **Upozornění:** Autor projektu neposkytuje žádné záruky a neodpovídá
> za ztráty ani poškození dat způsobené používáním nástrojů projektu bin2mzf.
> Projekt je distribuován pod licencí GPLv3 bez záruky.

Read-only extrakční nástroj pro MZF soubory (páskový formát počítačů
Sharp MZ). Načte MZF soubor (z pozičního argumentu nebo stdinu), ověří,
že velikost vstupu odpovídá poli `fsize` v hlavičce, a zapíše surové
bajty těla (offset 128 až 128+fsize) na stdout nebo do souboru zadaného
přes `-o`. Hlavičku samotnou nezpracovává - jen ji přeskočí.

`mzf-strip` je inverzní operace k `bin2mzf`: tam, kde `bin2mzf` k binárce
připojí 128bajtovou MZF hlavičku, `mzf-strip` ji zase odřízne. Typický
use case je round-trip ověření build pipeline (`bin2mzf` -> `mzf-strip`
-> `cmp` proti původní binárce). Nástroj patří do rodiny CLI projektu
`bin2mzf` spolu s `bin2mzf`, `mzf-info`, `mzf-hdr`, `mzf-cat` a
`mzf-paste`. Vstupní MZF se nikdy nemodifikuje - jediný výstup jsou
extrahovaná raw data těla.

## Použití

```
mzf-strip [-o OUTPUT] <INPUT.mzf>
mzf-strip [-o OUTPUT] < INPUT.mzf
mzf-strip --version
mzf-strip --lib-versions
mzf-strip --help
```

## Argumenty

| Argument | Popis |
|----------|-------|
| `<INPUT.mzf>` | Vstupní MZF soubor (volitelný; pokud chybí, čte se ze stdinu) |

Žádný `-i`/`--input` flag - cesta ke vstupnímu MZF se zadává výhradně
positional argumentem (konzistence s `mzf-info` a `mzf-hdr`). Bez
positional argumentu se vstup čte ze stdinu.

## Volby

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-o`, `--output FILE` | cesta k souboru | stdout | Zapíše extrahované tělo do `FILE` namísto stdoutu |
| `--version` | - | - | Vypíše verzi nástroje a skončí |
| `--lib-versions` | - | - | Vypíše verze linkovaných knihoven a skončí |
| `-h`, `--help` | - | - | Vypíše nápovědu a skončí |

`mzf-strip` záměrně nemá `--charset`, `--format`, `--cpm*` ani
`--align-*` / `--filler` volby - extrakce produkuje 1:1 raw bajty bez
jakékoliv transformace nebo interpretace dat. Pro zobrazení hlavičky
nebo strukturovaný výpis použijte `mzf-info`.

### Podrobnosti k strict size checku

Klíčová sémantika nástroje: vstup musí mít přesně `128 + fsize` bajtů,
kde `fsize` je 16bitové little-endian pole v hlavičce na offsetu 0x12.
Jak truncated (kratší), tak oversized (delší) vstup je odmítnut s chybou
`input size mismatch` na stderr a exit kódem 1. `mzf-strip` přitom
nečte `fsize` přes knihovní `mzf_load`, ale přímo z bajtů 0x12..0x13
v LE pořadí - to umožňuje vlastní strict size detekci bez závislosti
na chování knihovní vrstvy u truncated body.

| Vstupní situace | Chování |
|-----------------|---------|
| `n == 128 + fsize` | OK, zapíše se `fsize` bajtů těla, exit 0 |
| `n < 128` | Error "input shorter than MZF header", exit 1 |
| `n != 128 + fsize` | Error `input size mismatch`, exit 1 |
| `n > 65663` | Error `input size mismatch` (overflow detekce), exit 1 |
| `fsize == 0` | Prázdný výstup (žádné bajty), exit 0 |

Důvodem přísnosti je kontrola integrity MZF před extrakcí - pokud
soubor obsahuje trailing bytes nebo je useknutý, výsledná binárka by
nebyla shodná s tím, co `bin2mzf` původně vytvořil. Round-trip
ověření přes `cmp` by v takovém případě selhalo, takže přísný check
chytne problém už při extrakci a ne až ve fázi srovnání.

Pro tolerantní zpracování nevalidních MZF nebo pro inspekci použijte
`mzf-info`, který umí vstupy s nesrovnalostmi načíst a podrobně popsat
(včetně rozlišení truncated vs trailing bytes přes `--validate` nebo
`--format json`). `mzf-strip` má naopak binární kontrakt: buď je
vstup validní a extrakce proběhne, nebo skončí s exit kódem 1.

Speciální případ `fsize == 0` (typicky MZF vytvořené přes
`bin2mzf --header-only`, tedy "kostra" hlavičky bez těla) skončí
s prázdným výstupem a exit kódem 0. Round-trip přes `mzf-strip` u
takových MZF projde a produkuje soubor s nulovou velikostí (analogie
k `cat /dev/null > out`). Implementačně je `fwrite(buf, 1, 0, fp)`
explicitně přeskočen, protože některé libc implementace v takovém
volání nastavují `ferror` falešně pozitivně.

### Podrobnosti k vstupu a výstupu

Vstup se čte buď z pozičního argumentu (`<INPUT.mzf>`), nebo ze
stdinu, pokud poziční argument chybí. Výstup jde defaultně na stdout
nebo do souboru zadaného přes `-o, --output FILE`. Pipelining (stdin
i stdout) je hlavní podporovaný workflow - umožňuje řetězit
`mzf-strip` s ostatními nástroji (`sha256sum`, `xxd`, `od`, `cmp`,
další build pipeline kroky) bez mezisouborů.

Vstupní MZF se nikdy nemodifikuje - operace je čistě read-only.
Otevírá se v režimu `"rb"` (read binary) a zavírá hned po načtení
do paměťového bufferu.

Na MSYS2/Windows nástroj přepne stdin i stdout do binárního režimu
přes `setmode`, aby nedocházelo ke konverzi LF -> CRLF na výstupu
nebo k přerušení vstupu na bajtu 0x1A (DOS EOF). Bez tohoto by stdin
pipe na Windows zkrátil binárku na prvním 0x1A a stdout by každý LF
zdvojil na CRLF, což by zničilo jakýkoliv binární výstup.

Maximální velikost vstupu je 65663 bajtů (128 hlavička + 65535 tělo).
Větší vstup je zachycen interním overflow detektorem (buffer kapacity
65664 = 128 + 65535 + 1) a odmítnut chybou `input size mismatch`. Tělo
se nikdy nealokuje větší než `MZF_MAX_BODY_SIZE` (0xFFFF), což je
limit 16bitového pole `fsize`. Buffer pro celý vstup se alokuje na
heap přes `malloc` a uvolňuje před návratem z `main`.

## Exit kódy

| Kód | Popis |
|-----|-------|
| 0 | Úspěch (včetně `--help`, `--version`, `--lib-versions` a `fsize == 0` empty output) |
| 1 | I/O chyba, size mismatch, vstup kratší než hlavička, alokace bufferu, neznámá volba |

## Příklady

Příklady níže předpokládají, že máte v PATH binárky `bin2mzf`
a `mzf-strip` z portable distribuce projektu, plus standardní Unix
utility (`cat`, `cmp`, `grep`, `sha256sum`).

Round-trip ověření (primary use case - extrakce těla a porovnání s
původní binárkou):

```bash
bin2mzf -n PROG -l 0x1200 -i prog.bin -o prog.mzf
mzf-strip prog.mzf > prog.recovered.bin
cmp prog.bin prog.recovered.bin && echo OK
```

Extrakce do souboru přes `-o`:

```bash
mzf-strip prog.mzf -o prog.bin
```

Pipe stdin -> stdout:

```bash
cat prog.mzf | mzf-strip > prog.bin
```

Header-only MZF -> empty body (edge case `fsize == 0`):

```bash
bin2mzf -n SKEL -l 0x1200 --header-only -o skel.mzf
mzf-strip skel.mzf > /dev/null   # exit 0, žádný výstup
```

Kombinace s `mzf-info` pro inspekci hlavičky před extrakcí:

```bash
mzf-info prog.mzf && mzf-strip prog.mzf -o prog.bin
```

Extrakce a hash těla pro CI checksum:

```bash
mzf-strip prog.mzf | sha256sum
```

Detekce nevalidního vstupu (size mismatch jako brána v shellu):

```bash
mzf-strip broken.mzf 2>&1 | grep -q "input size mismatch" && echo "INVALID"
```

## Související nástroje

- `bin2mzf` - vytvoření MZF z binárky (inverzní operace k `mzf-strip`).
- `mzf-info` - inspekce hlavičky a hexdump těla MZF (read-only).
- `mzf-hdr` - manipulace hlavičky bez změny těla.
- `mzf-cat` - spojení více binárek do jednoho MZF (např. CCP+BDOS+BIOS).
- `mzf-paste` - vložení dat na zadaný offset existujícího MZF.

## MZF formát stručně

128 bajtů hlavičky + tělo (max 65535 bajtů):

| Offset | Velikost | Pole | Popis |
|--------|----------|------|-------|
| 0x00 | 1 | `ftype` | Typ souboru (0x01 OBJ, 0x02 BTX, 0x03 BSD, 0x04 BRD, 0x05 RB, 0x22 CPM) |
| 0x01 | 16 | `fname.name` | Jméno (Sharp MZ ASCII, případně CP/M 8.3 layout) |
| 0x11 | 1 | `terminator` | Terminátor jména (vždy 0x0D) |
| 0x12 | 2 | `fsize` | Velikost těla v bajtech (LE) |
| 0x14 | 2 | `fstrt` | Startovací adresa v paměti Z80 (LE) |
| 0x16 | 2 | `fexec` | Adresa spuštění v paměti Z80 (LE) |
| 0x18 | 104 | `cmnt` | Komentář / rezervovaný prostor |
