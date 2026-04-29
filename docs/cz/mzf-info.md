# mzf-info - Inspekce MZF souboru

Verze nástroje: 0.2.0. Součást projektu [bin2mzf](https://github.com/michalhucik/bin2mzf/).

> **Upozornění:** Autor projektu neposkytuje žádné záruky a neodpovídá
> za ztráty ani poškození dat způsobené používáním nástrojů projektu bin2mzf.
> Projekt je distribuován pod licencí GPLv3 bez záruky.

Read-only inspekční nástroj pro MZF soubory (páskový formát počítačů
Sharp MZ). Vypíše obsah 128bajtové hlavičky v lidsky čitelné podobě,
provede hexdump těla, zkontroluje validitu (terminátor jména, shoda
velikosti) a volitelně dekóduje CP/M (SOKODI CMT.COM) layout pro
soubory s `ftype == 0x22`.

Nástroj patří do rodiny CLI projektu `bin2mzf` spolu s `bin2mzf`,
`mzf-hdr`, `mzf-strip`, `mzf-cat` a `mzf-paste`. Slouží jako protějšek
k `bin2mzf` - tam, kde `bin2mzf` MZF tvoří, `mzf-info` ho zkoumá.
Strukturovaný výstup (`--format json|csv`) je určený pro CI pipeline,
testovací skripty a `jq` filtry. Volba `--validate` vrací jen exit kód
a hodí se jako rychlá brána v shellových skriptech.

## Použití

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

## Argumenty

| Argument | Popis |
|----------|-------|
| `[INPUT.mzf]` | Vstupní MZF soubor (volitelný; pokud chybí, čte se ze stdin) |

## Volby

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-C`, `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Znaková sada pro dekódování `name` a `comment` |
| `--header-only` | - | off | Vypíše jen text hlavičky (bez hexdumpu těla) |
| `--body-only` | - | off | Vypíše jen hexdump těla (bez textu hlavičky) |
| `--hexdump` | - | off | Připojí hexdump těla za text hlavičky |
| `--offset N` | 0..0xFFFF | 0 | Počáteční offset hexdumpu v rámci těla |
| `--length N` | 0..0xFFFF | délka těla | Počet bajtů hexdumpu |
| `--validate` | - | off | Tichý režim; exit 0 pokud je MZF validní, jinak 1 |
| `--no-cpm-decode` | - | dekódování zapnuto | Vypne CP/M dekódování při `ftype == 0x22` |
| `--format MODE` | `text`, `json`, `csv` | `text` | Výstupní formát |
| `--version`, `-V` | - | - | Vypíše verzi nástroje a skončí |
| `--lib-versions` | - | - | Vypíše verze linkovaných knihoven a skončí |
| `-h`, `--help` | - | - | Vypíše nápovědu a skončí |

Číselné hodnoty (`--offset`, `--length`) přijímají decimal i
hexadecimal s prefixem `0x` (např. `0x100` = `256`).

Volby `--header-only`, `--body-only` a `--validate` jsou vzájemně
výlučné. Volba `--validate` se navíc nesnáší s `--hexdump`. Volby
`--format json` a `--format csv` se nesnáší s `--hexdump` ani
`--body-only`. Detaily viz dále.

### Podrobnosti ke konverzi znakové sady

Pole `name` (16 bajtů) a `comment` (104 bajtů) jsou v hlavičce uloženy
v Sharp MZ ASCII. Volba `--charset` určuje, jak se interpretují při
zobrazení:

| Hodnota | Význam |
|---------|--------|
| `eu` | Sharp MZ ASCII evropská varianta (MZ-700/800), výstup v UTF-8 |
| `utf8-eu` | Synonymum k `eu` |
| `jp` | Sharp MZ ASCII japonská varianta (MZ-1500), výstup v UTF-8 |
| `utf8-jp` | Synonymum k `jp` |
| `none` | Bez konverze - místo dekódovaného řetězce se vypíše `(raw bytes only)` |

Synonyma `eu` / `utf8-eu` a `jp` / `utf8-jp` jsou implementačně
sjednocená a chovají se identicky. Default `eu` odpovídá nejčastějšímu
hardware (MZ-700/800).

Při `--charset none` se v textovém výstupu objeví `Name: (raw bytes only)`
a `Comment: (raw bytes only)`, raw hex bajty se zobrazí beze změny.
Ve strukturovaném výstupu (`--format json|csv`) jdou klíče `name`
a `comment` jako prázdný string.

### Podrobnosti k inspekci hlavičky

Výchozí (textový) režim vypíše obsah hlavičky v pevném pořadí řádků:
typ souboru, jméno (UTF-8 + raw 16 bajtů), volitelná CP/M sekce,
`fsize`, `fstrt` (load), `fexec` (exec), komentář (UTF-8 + raw prvních
16 bajtů), kontrolní součty těla (`sum8`, `xor`) a sekce validace.

U pole `ftype` se vedle hexa hodnoty vypisuje symbolický název:

| `ftype` | Symbol | Popis |
|---------|--------|-------|
| 0x01 | `OBJ` | Strojový kód (object file) |
| 0x02 | `BTX` | BASIC textový program |
| 0x03 | `BSD` | BASIC datový soubor |
| 0x04 | `BRD` | BASIC read-after-run datový soubor |
| 0x05 | `RB` | Read and branch (auto-run) |
| 0x22 | `CPM` | CP/M konvence (SOKODI CMT.COM) |
| jiné | `unknown` | Nerozpoznaný typ |

Komentář se v UTF-8 řádku vypisuje celý (104 bajtů), raw hex se omezuje
na prvních 16 bajtů kvůli šířce řádku.

### Podrobnosti k hexdumpu těla

Volby `--hexdump` (s textovým výstupem hlavičky) a `--body-only` (jen
hexdump) zapnou výpis těla ve formátu blízkém `od -A x -t x1z`:

```
00000000  4d 5a 46 20 73 61 6d 70  6c 65 20 64 61 74 61 21  |MZF sample data!|
```

- 8místný hex offset relativní k **začátku těla** (ne k souboru a ne
  k zobrazenému rozsahu).
- 16 bajtů hex hodnot, mezera mezi 8. a 9. bajtem.
- ASCII gutter mezi `|...|`, znaky mimo rozsah `0x20..0x7e` se zobrazí
  jako `.`.

Volby `--offset N` a `--length N` ohraničují zobrazený rozsah:
`--offset` posune počátek (default 0), `--length` omezí délku (default
celé tělo). Hodnoty mimo rozsah těla skončí chybou (exit 1). Použití
`--offset` nebo `--length` bez `--hexdump` ani `--body-only` vypíše
varování "no effect" a program pokračuje.

### Podrobnosti k validaci

Volba `--validate` zapíná tichý režim. Nástroj načte MZF, ověří dvě
podmínky a vrátí exit kód:

1. Terminátor jména na offsetu 0x11 je `0x0D`.
2. Skutečná velikost souboru přesně odpovídá `128 + fsize`.

Pokud obě platí, exit je 0 a stdout je prázdný. Pokud kterákoliv
selže, exit je 1 a na stderr se vypíše jedna řádka popisující důvod
(neprůchozí terminátor, truncated body nebo trailing bytes).

Tento kontrakt je určený pro CI pipeline a shell skripty. Příklad:
`mzf-info --validate hello.mzf || echo INVALID`.

Volba `--validate` je vzájemně výlučná s `--header-only`, `--body-only`
a `--hexdump`. Kombinace s `--format json|csv` projde syntakticky, ale
`--validate` má precedenci nad formátováním - nevypíše se žádný JSON
ani CSV, jen se vrátí exit kód.

### Podrobnosti k CP/M dekódování

Při `ftype == 0x22` (marker konvence SOKODI CMT.COM) nástroj automaticky
dekóduje CP/M jméno, příponu a atributy z pole `fname`. Detekce
vyžaduje současné splnění tří podmínek:

1. `ftype == 0x22`.
2. `fname.name[8] == '.'` (oddělovač jména a přípony).
3. `fname.name[12] == 0x0D` nebo `== 0x00` (terminátor jména).

Pokud kterákoliv podmínka selže, CP/M sekce se nevypíše (silent skip)
- to umožňuje cizí použití hodnoty 0x22 bez falešných pozitiv.

Při úspěšné detekci se v textovém výstupu zobrazí tři řádky paralelně
s `Name:` a raw bajty:

```
CP/M name:     "CCP"
CP/M ext:      "COM"
CP/M attrs:    R/O,SYS
```

Atributy se kódují v bitu 7 trojce bajtů přípony - bit 7 prvního =
R/O, druhého = SYS, třetího = ARC. Při nulové masce se zobrazí
`(none)`. Před zobrazením se bit 7 maskuje, takže CP/M ext je čistě
ASCII.

Volba `--no-cpm-decode` dekódování vypne. Hodí se pro MZF soubory,
které mají `ftype == 0x22` z jiných důvodů, nebo když chcete vidět
surovou hlavičku bez interpretace.

### Podrobnosti k strukturovanému výstupu

Volba `--format` přepíná výstup mezi text (default), JSON a CSV.
Strukturované formáty (json/csv) jsou určené pro strojové zpracování
- pipelines `jq`, parsery v testech, exporty do tabulek.

Mapa klíčů (snake_case, plochá - bez nestingu):

| Klíč | Typ | Vždy/CP/M | Popis | Příklad |
|------|-----|-----------|-------|---------|
| `ftype` | uint | vždy | Decimální hodnota `ftype` (0..255) | `1` |
| `ftype_hex` | string | vždy | Redundantní hex `"0xHH"` | `"0x01"` |
| `ftype_symbol` | string | vždy | `OBJ`, `BTX`, `BSD`, `BRD`, `RB`, `CPM`, `unknown` | `"OBJ"` |
| `name` | string | vždy | UTF-8 jméno (prázdné při `--charset none`) | `"HELLO"` |
| `cpm_name` | string | jen CP/M | CP/M jméno bez paddingu (max 8 znaků) | `"CCP"` |
| `cpm_ext` | string | jen CP/M | CP/M přípona bez bitu 7 (max 3 znaky) | `"COM"` |
| `cpm_attr_ro` | string | jen CP/M | `"true"` / `"false"` - atribut R/O | `"true"` |
| `cpm_attr_sys` | string | jen CP/M | `"true"` / `"false"` - atribut SYS | `"false"` |
| `cpm_attr_arc` | string | jen CP/M | `"true"` / `"false"` - atribut ARC | `"false"` |
| `cpm_attrs` | string | jen CP/M | Agregát `"R/O,SYS,ARC"` (může být `""`) | `"R/O,SYS"` |
| `fsize` | uint | vždy | Velikost těla v bajtech | `4096` |
| `fstrt` | string | vždy | Hex `"0xHHHH"` - load adresa Z80 | `"0x1200"` |
| `fexec` | string | vždy | Hex `"0xHHHH"` - exec adresa Z80 | `"0x1200"` |
| `comment` | string | vždy | UTF-8 plný 104B komentář (prázdný při `none`) | `"Built 2026-04-28"` |
| `body_sum8` | uint | vždy | Modulo 256 součet bajtů těla | `42` |
| `body_xor` | uint | vždy | XOR fold bajtů těla | `0` |
| `header_terminator_ok` | string | vždy | `"true"` / `"false"` - terminátor 0x0D | `"true"` |
| `size_match` | string | vždy | `"ok"` / `"trailing_bytes"` / `"truncated"` | `"ok"` |
| `size_expected` | uint | vždy | `128 + fsize` | `4224` |
| `size_actual` | uint | vždy | Skutečná velikost vstupu | `4224` |
| `valid` | string | vždy | `"true"` / `"false"` - agregát `header_terminator_ok && size_match=="ok"` | `"true"` |

> **Pozor:** Hodnoty typu boolean (`cpm_attr_ro`, `cpm_attr_sys`,
> `cpm_attr_arc`, `header_terminator_ok`, `valid`) jsou v JSON i CSV
> reprezentované jako STRING `"true"` / `"false"`, nikoli jako JSON
> bool. Konzument musí porovnávat řetězec, ne typovat na bool.

> **Pozor:** Hex hodnoty (`ftype_hex`, `fstrt`, `fexec`) jsou STRINGY
> ve formátu `"0xHH"` resp. `"0xHHHH"`. Číselné hodnoty (`ftype`,
> `fsize`, `body_sum8`, `body_xor`, `size_expected`, `size_actual`)
> jsou JSON UINT a v CSV bez uvozovek.

Klíče v sekci "jen CP/M" se vypisují pouze pokud se hlavička dekóduje
jako SOKODI layout (viz `### Podrobnosti k CP/M dekódování`) a
`--no-cpm-decode` není aktivní. Jinak v JSON/CSV výstupu úplně chybí
- nedostáváte je s prázdnou hodnotou.

CSV formát používá lazy hlavičku `key,value` (vypíše se automaticky
před prvním řádkem) a aplikuje RFC 4180 escape pro hodnoty s čárkou,
uvozovkami nebo newline.

Kombinace `--format json|csv` s `--hexdump` nebo `--body-only` skončí
tvrdou chybou ("incompatible with --format"). Hexdump se ze
strukturovaného formátu vědomě vynechává - cílem je tabelární metadata,
ne raw obsah.

Ukázka jednořádkového JSON výstupu (zkráceno):

```
{"ftype":1,"ftype_hex":"0x01","ftype_symbol":"OBJ","name":"HELLO","fsize":4096,"fstrt":"0x1200","fexec":"0x1200","comment":"","body_sum8":42,"body_xor":0,"header_terminator_ok":"true","size_match":"ok","size_expected":4224,"size_actual":4224,"valid":"true"}
```

### Podrobnosti k vstupu

Pokud není zadán positional argument `[INPUT.mzf]`, nástroj čte MZF ze
stdin. To umožňuje pipelines typu `cat foo.mzf | mzf-info` nebo
`bin2mzf ... | mzf-info`. Na MSYS2/Windows je stdin i stdout přepnut
do binary módu (`setmode`), aby nedocházelo k LF -> CRLF konverzi nebo
k přerušení vstupu na bajtu 0x1A.

Maximální velikost vstupu je 65663 bajtů (128 hlavička + 65535 tělo).
Větší vstup skončí chybou. Truncated body (vstup kratší než `128 + fsize`)
nástroj umí načíst a v textovém režimu ho ohlásí jako
`File size match: ERROR (truncated)`. Volba `--validate` v takovém
případě vrátí exit 1.

## Exit kódy

- `0` - úspěch (včetně výpisů `--help`, `--version`, `--lib-versions`
  a `--validate` na validním MZF).
- ne-`0` - chyba (parse argumentů, vzájemně výlučné volby, I/O chyba,
  alokace, vstup mimo limity, `--validate` na nevalidním MZF, hexdump
  rozsah mimo tělo).

## Příklady

Základní výpis hlavičky:

```bash
mzf-info hello.mzf
```

Čtení ze stdin přes pipe:

```bash
cat hello.mzf | mzf-info
```

Japonský charset pro MZ-1500 hru:

```bash
mzf-info --charset utf8-jp game.mzf
```

Jen hlavička, bez hexdumpu (čitelnější výpis):

```bash
mzf-info --header-only hello.mzf
```

Hexdump prvních 64 bajtů těla:

```bash
mzf-info --body-only --length 64 hello.mzf
```

Hexdump rozsahu od offsetu 0x100, 64 bajtů:

```bash
mzf-info --body-only --offset 0x100 --length 64 hello.mzf
```

Validace v CI skriptu (exit code brána):

```bash
mzf-info --validate hello.mzf || echo INVALID
```

JSON výstup pro `jq` filtr:

```bash
mzf-info --format json hello.mzf | jq '.fsize'
```

JSON validace v CI:

```bash
mzf-info --format json hello.mzf | jq -r '.valid'
```

CP/M soubor (automatické dekódování při `ftype == 0x22`):

```bash
mzf-info ccp.mzf
```

## Související nástroje

- `bin2mzf` - vytvoření MZF z binárky (inverzní operace k `mzf-strip`).
- `mzf-hdr` - manipulace hlavičky bez změny těla.
- `mzf-strip` - extrakce těla z MZF.
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
