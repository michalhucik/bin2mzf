# bin2mzf - Vytvoření MZF souboru z binárky

Verze nástroje: 0.2.0. Součást projektu [bin2mzf](https://github.com/michalhucik/bin2mzf/).

> **Upozornění:** Autor projektu neposkytuje žádné záruky
> a neodpovídá za ztráty ani poškození dat způsobené používáním nástrojů
> projektu bin2mzf. Projekt je distribuován pod licencí GPLv3 bez záruky.

Vytvoří MZF soubor (páskový formát počítačů Sharp MZ) z binárního
vstupu. Sestaví 128 bajtů hlavičky podle zadaných voleb (typ souboru,
jméno, load/exec adresa, komentář) a připojí tělo z binárky.

Nástroj je primárně určen jako finální krok build-pipeline pro vývojáře
aplikací na platformě Sharp MZ (MZ-700, MZ-800, MZ-1500).
Patří do rodiny CLI nástrojů projektu
spolu s `mzf-info`, `mzf-hdr`, `mzf-strip`, `mzf-cat` a `mzf-paste`.

## Použití

```
bin2mzf -n NAME -l LOAD_ADDR [options] [INPUT]
bin2mzf -n NAME -l LOAD_ADDR -i INPUT -o OUTPUT.mzf
bin2mzf -n NAME -l LOAD_ADDR --header-only -o OUTPUT.mzf
bin2mzf --cpm --cpm-name NAME [--cpm-ext EXT] [--cpm-attr LIST] -i INPUT -o OUTPUT.mzf
bin2mzf --version
bin2mzf --lib-versions
bin2mzf --help
```

## Argumenty

| Argument | Popis |
|----------|-------|
| `[INPUT]` | Vstupní binární soubor (volitelný; alternativa k `-i` nebo stdin) |

## Volby

### Povinné volby

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-n`, `--name NAME` | text (max 16 B po konverzi) | - | Jméno zapsané do hlavičky MZF |
| `-l`, `--load-addr ADDR` | 0..0xFFFF | - | Z80 startovací adresa (decimal nebo 0x-prefix) |

### Volitelné volby

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-t`, `--type TYPE` | 0x01..0xFF nebo `obj`, `btx`, `bsd`, `brd`, `rb` | `obj` (0x01) | Typ souboru |
| `-e`, `--exec-addr ADDR` | 0..0xFFFF | rovno `--load-addr` | Z80 spouštěcí adresa |
| `-c`, `--comment TEXT` | text (max 104 B po konverzi) | - | Komentář (delší se ořízne) |
| `-i`, `--input FILE` | cesta | - | Vstupní binární soubor (alternativa k positional argumentu) |
| `-o`, `--output FILE` | cesta | stdout | Výstupní MZF soubor |
| `-C`, `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Znaková sada pro `--name` a `--comment` |
| `--upper` | - | off | Před konverzí znaků převede a-z na A-Z |
| `-s`, `--size N\|auto` | 1..65535 nebo `auto` | `auto` | Vynutí přesnou velikost těla (alias k `--align-to N`) |
| `--align-block N` | 1..65535 | - | Zarovná tělo na nejbližší vyšší násobek N |
| `--align-to N` | 1..65535 | - | Vynutí přesnou velikost N (truncate nebo pad) |
| `--filler BYTE` | 0..255 nebo 0x00..0xFF | 0x00 | Padding bajt pro `--align-*` / `--size` |
| `--header-only` | - | off | Vyrobí pouze 128B hlavičku, vstup ignoruje |
| `--auto-size` | - | on | Pole `fsize` v hlavičce = skutečná délka těla |
| `--no-auto-size` | - | off | Pole `fsize` se převezme z `--size N` |
| `--cpm` | - | off | Aktivuje CP/M preset (SOKODI CMT.COM) |
| `--cpm-name NAME` | max 8 ASCII | - | Jméno v CP/M (vyžaduje `--cpm`) |
| `--cpm-ext EXT` | max 3 ASCII | `COM` | Přípona v CP/M (vyžaduje `--cpm`) |
| `--cpm-attr LIST` | `ro`, `sys`, `arc` (libovolná kombinace) | - | CP/M atributy (vyžaduje `--cpm`) |
| `--version` | - | - | Vypíše verzi nástroje a skončí |
| `--lib-versions` | - | - | Vypíše verze linkovaných knihoven a skončí |
| `-h`, `--help` | - | - | Vypíše nápovědu a skončí |

Číselné hodnoty přijímají decimal i hexadecimal s prefixem `0x`
(např. `0x1200` = `4608`).

### Podrobnosti ke konverzi znakové sady

Pole `name` a `comment` se zapisují v Sharp MZ ASCII. Volba `--charset`
určuje, jak se vstupní řetězec konvertuje:

| Hodnota | Význam |
|---------|--------|
| `eu` | Sharp MZ ASCII evropská varianta (MZ-700/800), vstup v ASCII/Latin |
| `utf8-eu` | Synonymum k `eu`, vstup v UTF-8 (přehlásky, ß, ...) |
| `jp` | Sharp MZ ASCII japonská varianta (MZ-1500) |
| `utf8-jp` | Synonymum k `jp`, vstup v UTF-8 (kanji, katakana) |
| `none` | Bez konverze, raw bajty (vhodné pro ASCII jména typu CP/M) |

Synonyma `eu` / `utf8-eu` a `jp` / `utf8-jp` jsou sémanticky
zaměnitelná - obě varianty interpretují vstup jako UTF-8.

Výchozí `--charset` je nastaven na `eu`.

#### Volba `--upper`

Aplikuje se PŘED konverzí znakové sady. Převede `a-z` na `A-Z` a působí
stejně na `--name` i `--comment`. Výchozí stav je vypnuto.

Volby `--charset` a `--upper` jsou **globální** - není možné je zadat
samostatně pro `--name` a samostatně pro `--comment`. Jediný způsob,
jak dosáhnout odlišného chování per-pole, je escape sekvence (viz
následující sekce): pole obsahující `\` automaticky vypne konverzi
i `--upper` jen pro sebe.

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

### Podrobnosti k zarovnání těla

Tři volby řídí zarovnání/velikost těla. Jsou vzájemně výlučné - lze
použít maximálně jednu z nich:

| Volba | Sémantika |
|-------|-----------|
| `--align-block N` | Doplní tělo nulami (nebo `--filler`) na nejbližší vyšší násobek N |
| `--align-to N` | Vynutí přesnou velikost N (truncate při delším těle, pad při kratším) |
| `-s`, `--size N` | Alias k `--align-to N` (numerická hodnota); `--size auto` = výchozí (žádná modifikace) |

Padding bajt je výchozí `0x00`, lze přepsat volbou `--filler BYTE`
(rozsah 0..255). Kombinace `--filler` bez `--align-*` nebo `--size`
vede k warningu "no effect".

Při `--align-to N` (resp. `--size N`) nižším než skutečná velikost
vstupu se tělo ořízne (truncate). Nástroj v tom případě emituje
varovnou hlášku.

### Podrobnosti k hlavičkovému módu

`--header-only` přeskočí čtení vstupu a zapíše pouze 128 B hlavičky.
Velikost výstupu je tedy přesně 128 bajtů.

`--auto-size` (výchozí) nastaví `fsize` v hlavičce na skutečnou délku
těla po případném zarovnání. `--no-auto-size` nutí použít hodnotu
zadanou v `--size N`. Kombinace `--no-auto-size` bez `--size` je no-op
a vede k warningu (auto-size se vrátí jako default).

Při `--header-only` je `fsize` nastaveno na 0, případně na hodnotu
ze `--size N` v kombinaci s `--no-auto-size`.

### Podrobnosti k režimu CP/M

Volba `--cpm` aktivuje konvenci nástroje SOKODI CMT.COM pro přenos
CP/M souborů v MZF. Po aktivaci nástroj nastaví preset:

- `ftype` = `0x22` (marker CP/M konvence)
- `fstrt` = `fexec` = `0x0100` (CP/M TPA, kam .COM soubory patří)
- `--charset` = `none` (CP/M filesystem pracuje s ASCII jmény)

`--cpm` je vzájemně výlučné s `--type`, `--load-addr`, `--exec-addr`,
`--charset` a `--name`. Jakákoliv kombinace skončí tvrdou chybou.
Volba `-c`/`--comment` zůstává povolena.

Sub-volby:

- `--cpm-name NAME` - max 8 ASCII znaků, nástroj automaticky převede
  na uppercase a doplní mezerami. Vyžaduje `--cpm`.
- `--cpm-ext EXT` - max 3 ASCII znaky, výchozí `COM`. Vyžaduje `--cpm`.
- `--cpm-attr LIST` - libovolná kombinace `ro`, `sys`, `arc`. Položky
  lze oddělit čárkou, mezerou nebo zřetězit (`ro,sys`, `ro sys`,
  `roSys`). Case-insensitive. Vyžaduje `--cpm`.

Layout pole `fname` (16 znaků + 0x0D terminátor) v CP/M režimu:

| Offset | Význam |
|--------|--------|
| 0..7 | CP/M jméno (8 znaků, doplněno mezerami, ASCII) |
| 8 | `.` (0x2E) - oddělovač |
| 9 | Přípona[0] - bit 7 = R/O (Read-Only) |
| 10 | Přípona[1] - bit 7 = SYS (System) |
| 11 | Přípona[2] - bit 7 = ARC (Archived) |
| 12 | 0x0D (terminátor) |

Žádný atribut nevynucuje velikost těla - `--cpm` lze kombinovat
s `--align-block`, `--align-to`, `--size` i `--filler`.

### Podrobnosti k vstupu a výstupu

Priorita zdroje vstupu:

1. `--input FILE` (pokud zadán)
2. Positional argument `[INPUT]` (pokud zadán)
3. `stdin` (pokud nic z výše uvedeného není)

Kombinace `--input` + positional argument skončí tvrdou chybou.

Výstup jde do souboru zadaného `--output FILE`, nebo na stdout pokud
není zadán. Na MSYS2/Windows nástroj přepíná stdin a stdout do binary
módu (`setmode`), aby nedocházelo k LF -> CRLF konverzi nebo k přerušení
vstupu na bajtu 0x1A.

Maximální velikost těla je 65535 bajtů (limit 16-bit pole `fsize`).
Větší vstup skončí chybou.

## Exit kódy

- `0` - úspěch (včetně výpisů `--help`, `--version`, `--lib-versions`).
- ne-`0` - chyba (parse argumentů, vzájemně výlučné volby, I/O chyba,
  alokace, přetečení limitu velikosti).

## Příklady

Základní vytvoření OBJ MZF z binárky:

```bash
bin2mzf -n HELLO -l 0x1200 -o hello.mzf hello.bin
```

Pojmenovaný MZF s komentářem a vlastní spouštěcí adresou:

```bash
bin2mzf -n LOADER -l 0x2000 -e 0x2010 -c "Built 2026-04-28" -o loader.mzf loader.bin
```

Symbolický typ (BTX = BASIC textový program):

```bash
bin2mzf -n GAME -t btx -l 0x1200 -o game.mzf game.bin
```

Charset JP s japonským jménem (UTF-8 vstup):

```bash
bin2mzf --charset utf8-jp -n "テスト" -l 0xD000 -o test.mzf test.bin
```

CP/M .COM soubor s atributy R/O+SYS:

```bash
bin2mzf --cpm --cpm-name NEWCCP --cpm-ext COM --cpm-attr ro,sys -o newccp.mzf newccp.bin
```

Zarovnání na 256B blok s 0xFF výplní:

```bash
bin2mzf -n MYBOOT -l 0x2000 --align-block 256 --filler 0xFF -o myboot.mzf myboot.bin
```

Vynucená velikost těla 8192 bajtů (truncate nebo pad):

```bash
bin2mzf -n PROG -l 0x1200 --size 8192 -o prog.mzf prog.bin
```

Header-only skeleton (pouze 128B hlavička):

```bash
bin2mzf -n EMPTY -l 0x1200 --header-only -o skeleton.mzf
```

Pipe stdin -> stdout a následná inspekce sourozeneckým nástrojem:

```bash
cat foo.bin | bin2mzf -n FOO -l 0x1200 > foo.mzf
mzf-info foo.mzf
```

## Související nástroje

- `mzf-info` - inspekce existujícího MZF (hlavička, hexdump, validace).
- `mzf-hdr` - manipulace hlavičky bez změny těla.
- `mzf-strip` - extrakce těla z MZF (inverzní operace k `bin2mzf`).
- `mzf-cat` - spojení více binárek do jednoho MZF (např. CCP+BDOS+BIOS).
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
