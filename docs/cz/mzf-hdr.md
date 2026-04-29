# mzf-hdr - Modifikace hlavičky MZF souboru

Verze nástroje: 0.1.0. Součást projektu [bin2mzf](https://github.com/michalhucik/bin2mzf/).

> **Upozornění:** Autor projektu neposkytuje žádné záruky a neodpovídá
> za ztráty ani poškození dat způsobené používáním nástrojů projektu bin2mzf.
> Projekt je distribuován pod licencí GPLv3 bez záruky.

Modifikuje 128bajtovou hlavičku existujícího MZF souboru bez zásahu
do těla. Tělo zůstává byte-identické - nástroj jen přepisuje vybraná
pole hlavičky podle CLI voleb a výsledek zapisuje buď zpět do vstupního
souboru (in-place) nebo do nového cílového souboru přes `-o`.

Typický use case je rychlá oprava metadat (přejmenování, změna load
adresy, oprava typu souboru) bez nutnosti znovu sestavovat celý MZF
přes `bin2mzf`. Nástroj patří do rodiny CLI projektu `bin2mzf` spolu
s `bin2mzf`, `mzf-info`, `mzf-strip`, `mzf-cat` a `mzf-paste`. Slouží
jako doplněk k `bin2mzf` - tam, kde `bin2mzf` MZF tvoří, `mzf-hdr`
ho upravuje. Sémantika je read-modify-write nad hlavičkou: každé pole,
pro které uživatel zadal volbu, se přepíše; ostatní pole zůstávají
z původního MZF beze změny.

## Použití

```
mzf-hdr [options] [-o OUTPUT.mzf] <INPUT.mzf>
mzf-hdr -n NAME <INPUT.mzf>
mzf-hdr -l LOAD -e EXEC -o OUTPUT.mzf <INPUT.mzf>
mzf-hdr --cpm --cpm-name NAME [--cpm-ext EXT] [--cpm-attr LIST] <INPUT.mzf>
mzf-hdr --version
mzf-hdr --lib-versions
mzf-hdr --help
```

## Argumenty

| Argument | Popis |
|----------|-------|
| `<INPUT.mzf>` | Vstupní MZF soubor (povinný, právě jeden positional argument) |

Žádný `-i`/`--input` flag - cesta ke vstupnímu MZF se zadává výhradně
positional argumentem (konzistence s `mzf-info` a `mzf-strip`). Žádný
stdin vstup.

## Volby

### Volby - pole hlavičky

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-t`, `--type TYPE` | 0x01..0xFF nebo `obj`, `btx`, `bsd`, `brd`, `rb` | zachová z INPUT | Typ souboru |
| `-n`, `--name NAME` | text (max 16 B po konverzi) | zachová z INPUT | Jméno v hlavičce MZF |
| `-c`, `--comment TEXT` | text (max 104 B po konverzi) | zachová z INPUT | Komentář (delší se ořízne) |
| `--cmnt-bin FILE` | cesta | zachová z INPUT | Načte raw 104 B z FILE do pole `cmnt` |
| `-l`, `--load-addr ADDR` | 0..0xFFFF | zachová z INPUT | Z80 startovací adresa (decimal nebo 0x-prefix) |
| `-e`, `--exec-addr ADDR` | 0..0xFFFF | zachová z INPUT | Z80 spouštěcí adresa |
| `-s`, `--size N` | 1..65535 | auto-size | Vynutí přesnou hodnotu pole `fsize` |
| `--no-auto-size` | - | off | Zachová původní `fsize` z INPUT.mzf |

### Znaková sada

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `--charset MODE` | `eu`, `jp`, `utf8-eu`, `utf8-jp`, `none` | `eu` | Znaková sada pro `--name` a `--comment` |
| `--upper` | - | off | Před konverzí znaků převede a-z na A-Z |

### CP/M

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `--cpm` | - | off | Aktivuje CP/M preset (SOKODI CMT.COM) |
| `--cpm-name NAME` | max 8 ASCII | - | Jméno v CP/M (vyžaduje `--cpm`) |
| `--cpm-ext EXT` | max 3 ASCII | `COM` | Přípona v CP/M (vyžaduje `--cpm`) |
| `--cpm-attr LIST` | `ro`, `sys`, `arc` (libovolná kombinace) | - | CP/M atributy (vyžaduje `--cpm`) |

### Výstup a info

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `-o`, `--output FILE` | cesta | in-place | Cílový MZF soubor; bez této volby se přepisuje INPUT.mzf |
| `--version` | - | - | Vypíše verzi nástroje a skončí |
| `--lib-versions` | - | - | Vypíše verze linkovaných knihoven a skončí |
| `-h`, `--help` | - | - | Vypíše nápovědu a skončí |

Číselné hodnoty (`-l`, `-e`, `-s`) přijímají decimal i hexadecimal
s prefixem `0x` (např. `0x1200` = `4608`).

### Selektivní update polí hlavičky

Klíčový designový kontrast vůči `bin2mzf`: každé pole hlavičky je
volitelné. Volba, kterou uživatel nezadá, znamená "ponech původní
hodnotu z INPUT.mzf"; volba, kterou zadá, znamená "přepiš toto pole".

Příklad: `mzf-hdr -n NEWNAME prog.mzf` přejmenuje soubor uvnitř
hlavičky a všechna ostatní pole (`ftype`, `fstrt`, `fexec`, `fsize`,
`cmnt`) zůstávají beze změny.

Existuje jedna výjimka v selektivitě: pole `fsize` se ve výchozím
režimu **přepočítává automaticky** na skutečnou délku těla
(viz `### Podrobnosti k velikosti (-s/--size)`). Pokud chcete
zachovat původní `fsize` přesně tak, jak je v INPUT.mzf (typicky pokud
víte, že v INPUT je úmyslně jiná hodnota než délka těla), použijte
`--no-auto-size`.

### Podrobnosti ke konverzi znakové sady

Pole `name` a `comment` se v hlavičce ukládají v Sharp MZ ASCII.
Volba `--charset` určuje, jak se vstupní řetězec z `--name` a
`--comment` konvertuje:

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

> **Pozor:** V CP/M módu (`--cpm`) je `--charset` vzájemně výlučný
> s presetem - kombinace skončí tvrdou chybou. Komentář přes `-c`
> v CP/M módu se interně zpracuje jako `--charset none`.

### Podrobnosti k --cmnt-bin

Volba `--cmnt-bin FILE` načte raw bajty ze souboru a zapíše je
přímo do 104bajtového pole `cmnt` v hlavičce. Bypassuje veškerou
konverzi znakové sady i transformaci `--upper`. Hodí se pro vložení
binárního watermarku, signatury build systému nebo proprietárního
metadata bloku.

| Velikost FILE | Chování |
|---------------|---------|
| = 104 B | Přesný fit - raw kopie do pole `cmnt` |
| < 104 B | Pre-pad nulami: prvních N bajtů ze souboru, zbytek 0x00 |
| > 104 B | Truncate na 104 B + warning na stderr (substring `truncated`) |

> **Pozor:** Tato vzájemná výlučnost se týká dvojice `-c`/`--comment` ×
> `--cmnt-bin` - oba kanály zapisují do téhož pole `cmnt`, takže nelze
> je zadat současně (kombinace skončí tvrdou chybou). Volba
> `-c`/`--comment` ani `--cmnt-bin` **není** v konfliktu s `--cpm`
> (oba kanály komentáře jsou v CP/M módu povolené).

### Podrobnosti k velikosti (-s/--size)

Pole `fsize` v hlavičce udává délku těla. `mzf-hdr` ho zpracovává
ve třech režimech:

| Režim | Chování |
|-------|---------|
| Default (žádný `-s`, žádný `--no-auto-size`) | Auto-size: `fsize` = skutečná délka těla z INPUT.mzf |
| `-s N` | Vynutí `fsize = N`. Implicitně vypne auto-size (DWIM). |
| `--no-auto-size` (bez `-s`) | Zachová původní `fsize` z INPUT.mzf beze změny |

Auto-size je výchozí, protože `mzf-hdr` typicky upravuje hlavičky,
kde má `fsize` odpovídat reálné velikosti těla. Pokud by tělo bylo
nedotčené a původní hlavička měla správné `fsize`, je auto-size
no-op.

**Hard error:** pokud `-s N` zadáte s hodnotou větší než skutečná
délka těla v INPUT.mzf, nástroj skončí chybou se substringem
`exceeds body size` a výstup nezapíše. `mzf-hdr` neumí rozšířit
tělo - pro to slouží `bin2mzf` (rebuild) nebo `mzf-paste` (vložení
bajtů na offset).

Stejný hard error se uplatní i defenzivně před zápisem, pokud byl
INPUT.mzf již rozbitý (`fsize > body_size`) a uživatel použil
`--no-auto-size`. Tím se zabrání heap OOB read v knihovní vrstvě.

### Podrobnosti k režimu CP/M

Volba `--cpm` aktivuje konvenci nástroje SOKODI CMT.COM pro přenos
CP/M souborů v MZF. Po aktivaci nástroj přepíše tato pole hlavičky:

- `ftype` = `0x22` (marker CP/M konvence)
- `fstrt` = `fexec` = `0x0100` (CP/M TPA, kam .COM soubory patří)
- `fname` se sestaví podle SOKODI 8.3 layoutu z `--cpm-name`,
  `--cpm-ext` a `--cpm-attr`

Tělo INPUT.mzf zůstává byte-identické. Rozdíl proti `bin2mzf --cpm`
je tedy v tom, že `mzf-hdr --cpm` opravuje pouze hlavičku už existujícího
MZF, zatímco `bin2mzf --cpm` ho tvoří od nuly.

`--cpm` je vzájemně výlučné s `--type`, `--load-addr`, `--exec-addr`,
`--charset` a `--name`. Jakákoliv kombinace skončí tvrdou chybou.

| Volba | Kombinace s `--cpm` |
|-------|---------------------|
| `--type`, `--load-addr`, `--exec-addr`, `--charset`, `--name` | Hard error (preset by je přepsal) |
| `--comment`, `--cmnt-bin`, `--upper`, `--size`, `--no-auto-size` | OK (orthogonální k presetu) |

Sub-volby:

- `--cpm-name NAME` - max 8 ASCII znaků, nástroj automaticky převede
  na uppercase a doplní mezerami. Vyžaduje `--cpm`. Bez `--cpm-name`
  je `--cpm` hard error.
- `--cpm-ext EXT` - max 3 ASCII znaky, výchozí `COM`. Vyžaduje
  `--cpm`.
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

### Atomicita zápisu

Operace zápisu je atomická bez ohledu na režim. V in-place režimu
(bez `-o`) nástroj nikdy nepřepisuje cílový soubor přímo - místo
toho zapíše výsledek do pomocného `<INPUT>.tmp`, dokončený soubor
přejmenuje přes `rename()` na `<INPUT>` a starý soubor se atomicky
nahradí. Pokud zápis selže uprostřed (například při zaplněném
disku), původní INPUT.mzf zůstává nezměněn a `<INPUT>.tmp` se
maže/zůstává jako stopový artefakt podle stavu chyby.

V režimu s `-o OUTPUT.mzf` se zapisuje rovnou do cílového souboru -
INPUT.mzf zůstane nedotčen.

> **Caveat (Win32):** Atomic rename na Windows má krátké okno
> mezi `unlink` cílového souboru a `rename` z tmp na cíl, kdy cíl
> teoreticky neexistuje. Na POSIX (Linux/MSYS2 přes Cygwin runtime)
> je `rename()` atomický bez tohoto okna. Pro kritická data
> doporučujeme zálohu nebo použití `-o`.

## Exit kódy

- `0` - úspěch (včetně výpisů `--help`, `--version`, `--lib-versions`).
- ne-`0` - chyba (parse argumentů, vzájemně výlučné volby, I/O chyba,
  alokace, `-s N` mimo rozsah těla, rozbitý INPUT.mzf).

## Příklady

Přejmenování souboru in-place:

```bash
mzf-hdr -n NEWNAME prog.mzf
```

Změna load i exec adresy:

```bash
mzf-hdr -l 0x6000 -e 0x6000 prog.mzf
```

Změna typu souboru na BTX (BASIC textový program):

```bash
mzf-hdr -t btx prog.mzf
```

Vytvoř novou kopii s jiným jménem, ponech originál:

```bash
mzf-hdr -n NEW -o new.mzf orig.mzf
```

Nastav CP/M atributy R/O+SYS na CCP.COM:

```bash
mzf-hdr --cpm --cpm-name CCP --cpm-attr ro,sys ccp.mzf
```

Nahraď komentář raw bajty z binárky:

```bash
mzf-hdr --cmnt-bin watermark.bin prog.mzf
```

Vynucená velikost těla 8192 bajtů (musí <= skutečné velikosti těla):

```bash
mzf-hdr -s 8192 prog.mzf
```

Charset JP s japonským jménem (UTF-8 vstup):

```bash
mzf-hdr --charset utf8-jp -n "テスト" game.mzf
```

Pipeline: zobraz, uprav, znovu zobraz:

```bash
mzf-info prog.mzf && mzf-hdr -n FOO prog.mzf && mzf-info prog.mzf
```

## Související nástroje

- `bin2mzf` - vytvoření MZF z binárky (rebuild celého souboru).
- `mzf-info` - inspekce existujícího MZF (hlavička, hexdump, validace).
- `mzf-strip` - extrakce těla z MZF (inverzní operace k `bin2mzf`).
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
