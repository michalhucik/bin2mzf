# mzf-cat - Spojení binárek do MZF souboru

Verze nástroje: 0.1.0. Součást projektu [bin2mzf](https://github.com/michalhucik/bin2mzf/).

> **Upozornění:** Autor projektu neposkytuje žádné záruky
> a neodpovídá za ztráty ani poškození dat způsobené používáním nástrojů
> projektu bin2mzf. Projekt je distribuován pod licencí GPLv3 bez záruky.

Spojí několik binárních souborů do jednoho MZF s jedinou load adresou.
Postupně přečte všechny pozicní vstupy, mezi non-last vstupy volitelně
vloží padding na zarovnání bloku (`--pad-between`), na konec aplikuje
volitelné finální zarovnání (`--align-block` / `--align-to` / `--size`)
a sestaví MZF s jedinou společnou hlavičkou. Tělo se uloží jako souvislý
blok v paměti Z80.

Typický use case je CP/M loader, který se skládá z modulů CCP + BDOS +
BIOS a musí jít po sobě v paměti. Jiné typické nasazení: spojení
segmentů kódu/dat do jednoho přenosného páskového souboru. Nástroj
patří do rodiny CLI projektu `bin2mzf` spolu s `bin2mzf`, `mzf-info`,
`mzf-hdr`, `mzf-strip` a `mzf-paste`. Slouží jako specializovaný
doplněk k `bin2mzf` - tam, kde `bin2mzf` zpracovává jednu binárku,
`mzf-cat` zpracovává libovolný počet binárek za sebou.

## Použití

```
mzf-cat -n NAME -l LOAD_ADDR [options] -o OUTPUT.mzf INPUT1.bin [INPUT2.bin ...]
mzf-cat --version
mzf-cat --lib-versions
mzf-cat --help
```

## Argumenty

| Argument | Popis |
|----------|-------|
| `INPUT1.bin [INPUT2.bin ...]` | Vstupní binární soubory (alespoň 1, variadic - více vstupů povoleno) |

## Volby

### Povinné volby

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-n`, `--name NAME` | text (max 16 B po konverzi) | - | Jméno zapsané do hlavičky MZF |
| `-l`, `--load-addr ADDR` | 0..0xFFFF | - | Z80 startovací adresa (decimal nebo 0x-prefix) |
| `-o`, `--output FILE` | cesta | stdout | Výstupní MZF soubor |

### Volby - hlavička

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-t`, `--type TYPE` | 0x01..0xFF nebo `obj`, `btx`, `bsd`, `brd`, `rb` | `obj` (0x01) | Typ souboru |
| `-c`, `--comment TEXT` | text (max 104 B po konverzi) | - | Komentář (delší se ořízne) |
| `-e`, `--exec-addr ADDR` | 0..0xFFFF | rovno `--load-addr` | Z80 spouštěcí adresa |

### Volby - layout těla

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `--pad-between N` | 1..65535 | - | Před každým non-last vstupem zarovná tělo na násobek N |
| `--align-block N` | 1..65535 | - | Finální tělo zarovná na nejbližší vyšší násobek N |
| `--align-to N` | 1..65535 | - | Vynutí přesnou velikost N (truncate nebo pad) |
| `-s`, `--size N\|auto` | 1..65535 nebo `auto` | `auto` | Alias k `--align-to N` (numerická hodnota) |
| `--filler BYTE` | 0..255 nebo 0x00..0xFF | 0x00 | Padding bajt pro `--pad-between` i finální align |

### Volby - znaková sada

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-C`, `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Znaková sada pro `--name` a `--comment` |
| `--upper` | - | off | Před konverzí znaků převede a-z na A-Z |

### Volby - info

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `--version` | - | - | Vypíše verzi nástroje a skončí |
| `--lib-versions` | - | - | Vypíše verze linkovaných knihoven a skončí |
| `-h`, `--help` | - | - | Vypíše nápovědu a skončí |

Číselné hodnoty přijímají decimal i hexadecimal s prefixem `0x`
(např. `0x1200` = `4608`).

### Variadické vstupy

`mzf-cat` přijímá vstupy výhradně jako pozicní argumenty. Žádný
`-i`/`--input` flag neexistuje (na rozdíl od `bin2mzf`) - vícenásobné
vstupy by takovou volbu komplikovaly bez přínosu. Nutný je alespoň
jeden vstup; horní limit je daný pouze celkovou velikostí těla
(<= 65535 B).

Optstring nástroje je POSIX-strict (`+` na začátku) - to znamená, že
všechny volby musí předcházet pozicní argumenty. Po prvním ne-volba
argumentu getopt zastaví parsování, zbytek bere jako vstupy. Příkaz
ve formě `mzf-cat input.bin -n NAME -l 0x1200 -o out.mzf` je proto
neplatný - `-n`, `-l` a `-o` v něm budou interpretovány jako další
pozicní vstupy. Pokud chybí jakýkoliv pozicní vstup, nástroj skončí
chybou s textem "at least one input file is required".

### Pořadí operací

Tělo MZF se skládá v pevně daném pořadí:

```
input1 -> [pad-between filler] -> input2 -> ... -> [pad-between filler] -> inputN -> [final align]
```

Konkrétně:

1. Načte se `input1` jako první blok těla.
2. Pokud je zadán `--pad-between N` a aktuální offset těla není násobkem
   N, doplní se `--filler` bajty do dalšího násobku N. Pokud offset už
   na hranici je, padding je nulový (`modv == 0` -> 0 bajtů).
3. Načte se `input2`, případně další pad-between a další vstupy, dokud
   nejsou všechny vstupy zpracované.
4. Po posledním vstupu se pad-between **neprovádí** (poslední vstup
   nemá následníka, mezi který by se padovalo).
5. Pokud je zadán některý z `--align-block`/`--align-to`/`--size`,
   aplikuje se finální zarovnání na celkové tělo.

Maximální velikost těla je 65535 bajtů. Pokud součet všech vstupů
i s pad-between nebo s finálním paddingem překročí limit, nástroj
skončí tvrdou chybou se substringem `exceeds maximum body size`.

`--pad-between` a finální zarovnání jsou orthogonální - lze je
kombinovat (typicky `--pad-between` zarovnává jednotlivé moduly
na blok a `--align-block` zarovnává celý výsledek na hranici cílové
paměti).

### Podrobnosti k zarovnání

Volby pro **finální** zarovnání těla jsou tři a jsou vzájemně výlučné -
lze použít maximálně jednu z nich. Kombinace skončí tvrdou chybou se
substringem `mutually exclusive`:

| Volba | Sémantika |
|-------|-----------|
| `--align-block N` | Doplní výsledné tělo nulami (nebo `--filler`) na nejbližší vyšší násobek N |
| `--align-to N` | Vynutí přesnou velikost N (truncate při delším těle, pad při kratším) |
| `-s`, `--size N` | Alias k `--align-to N` (numerická hodnota); `--size auto` = výchozí (žádná modifikace) |

Padding bajt je výchozí `0x00`, lze přepsat volbou `--filler BYTE`
(rozsah 0..255 nebo 0x00..0xFF). Stejný `--filler` se použije i pro
`--pad-between`.

Při `--align-to N` (resp. `--size N`) menším než skutečná velikost
spojeného těla se tělo ořízne (truncate). Nástroj v tom případě
emituje varovnou hlášku se substringem `truncated`.

`--pad-between N` **není** součástí 3-cestného mutexu - je orthogonální
ke všem třem finálním režimům a může se s libovolným z nich kombinovat.

### Podrobnosti ke konverzi znakové sady

Pole `name` a `comment` se v hlavičce ukládají v Sharp MZ ASCII.
Volba `--charset` určuje, jak se vstupní řetězec konvertuje:

| Hodnota | Význam |
|---------|--------|
| `eu` | Sharp MZ ASCII evropská varianta (MZ-700/800), vstup v ASCII/Latin |
| `utf8-eu` | Synonymum k `eu`, vstup v UTF-8 (přehlásky, ß, ...) |
| `jp` | Sharp MZ ASCII japonská varianta (MZ-1500) |
| `utf8-jp` | Synonymum k `jp`, vstup v UTF-8 (kanji, katakana) |
| `none` | Bez konverze, raw bajty (vhodné pro ASCII jména typu CP/M) |

Synonyma `eu` / `utf8-eu` a `jp` / `utf8-jp` jsou sémanticky
zaměnitelná - obě varianty interpretují vstup jako UTF-8.

Volba `--upper` se aplikuje PŘED konverzí znakové sady. Převede
`a-z` na `A-Z` a působí stejně na `--name` i `--comment`. Výchozí
stav je vypnuto.

#### Escape sekvence v `-n` a `-c`

V `--name` i `--comment` lze použít C-style escape sekvence:

| Sekvence | Význam |
|----------|--------|
| `\\` | Bajt `\` (0x5C) |
| `\n` | Bajt LF (0x0A) |
| `\r` | Bajt CR (0x0D) |
| `\t` | Bajt TAB (0x09) |
| `\xNN` | Libovolný bajt v hex |

Pokud pole obsahuje libovolnou escape sekvenci, `--charset` i `--upper`
se pro DANÉ pole vypnou (chovají se jako `--charset none`). Granularita
je per-pole - escape v `--name` nevypne konverzi v `--comment` a naopak.

> **Pozor:** Volba `-c` (krátký zápis `--comment`) je odlišná od `-C`
> (krátký zápis `--charset`). Pozor na case.

Limit 16 bajtů u `--name` se kontroluje **po konverzi**. UTF-8 multibyte
sekvence se mohou rozšířit nebo zúžit - délka v bajtech po konverzi
musí být <= 16.

## Exit kódy

- `0` - úspěch (včetně výpisů `--help`, `--version`, `--lib-versions`).
- ne-`0` - chyba (parse argumentů, vzájemně výlučné volby, chybějící
  pozicní vstup, I/O chyba, alokace, přetečení limitu velikosti těla).

## Příklady

CP/M loader (primární use case) - spojení CCP + BDOS + BIOS do jednoho
MZF se společnou load adresou:

```bash
mzf-cat -n LOADER -l 0xD400 -o cpm.mzf ccp.bin bdos.bin bios.bin
```

Jeden vstup (chování ekvivalentní s `bin2mzf`):

```bash
mzf-cat -n PROG -l 0x1200 -o prog.mzf prog.bin
```

Pad-between na 256B blok mezi dvěma moduly:

```bash
mzf-cat -n LOADER -l 0xD400 --pad-between 256 -o loader.mzf part1.bin part2.bin
```

Vynucená finální velikost těla 8192 bajtů (truncate nebo pad):

```bash
mzf-cat -n PROG -l 0x1200 -s 8192 -o prog.mzf prog1.bin prog2.bin
```

Finální zarovnání na 1024B blok s 0xFF výplní:

```bash
mzf-cat -n PROG -l 0x1200 --align-block 1024 --filler 0xFF -o prog.mzf p1.bin p2.bin
```

JP charset s japonským jménem (UTF-8 vstup):

```bash
mzf-cat --charset utf8-jp -n "ゲーム" -l 0x2000 -o game.mzf part1.bin part2.bin
```

Pad-between i finální align současně (orthogonální kombinace):

```bash
mzf-cat -n PROG -l 0x1200 --pad-between 256 --align-block 1024 -o prog.mzf p1.bin p2.bin
```

Inspekce výstupu sourozeneckým nástrojem hned po vytvoření:

```bash
mzf-cat -n LOADER -l 0xD400 -o loader.mzf ccp.bin bdos.bin && mzf-info loader.mzf
```

## Související nástroje

- `bin2mzf` - vytvoření MZF z jediné binárky (rebuild celého souboru).
- `mzf-info` - inspekce existujícího MZF (hlavička, hexdump, validace).
- `mzf-hdr` - manipulace hlavičky bez změny těla.
- `mzf-strip` - extrakce těla z MZF (inverzní operace k `bin2mzf`).
- `mzf-paste` - vložení dat na zadaný offset existujícího MZF.

## MZF formát stručně

128 bajtů hlavičky + tělo (max 65535 bajtů):

| Offset | Velikost | Pole | Popis |
|--------|----------|------|-------|
| 0x00 | 1 | `ftype` | Typ souboru (0x01 OBJ, 0x02 BTX, 0x03 BSD, 0x04 BRD, 0x05 RB) |
| 0x01 | 16 | `fname.name` | Jméno (Sharp MZ ASCII) |
| 0x11 | 1 | `terminator` | Terminátor jména (vždy 0x0D) |
| 0x12 | 2 | `fsize` | Velikost těla v bajtech (LE) |
| 0x14 | 2 | `fstrt` | Startovací adresa v paměti Z80 (LE) |
| 0x16 | 2 | `fexec` | Adresa spuštění v paměti Z80 (LE) |
| 0x18 | 104 | `cmnt` | Komentář / rezervovaný prostor |
