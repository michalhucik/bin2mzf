# mzf-paste - Vložení nebo přepis dat v MZF souboru

Verze nástroje: 0.1.0. Součást projektu [bin2mzf](https://github.com/michalhucik/bin2mzf/).

> **Upozornění:** Autor projektu neposkytuje žádné záruky a neodpovídá
> za ztráty ani poškození dat způsobené používáním nástrojů projektu bin2mzf.
> Projekt je distribuován pod licencí GPLv3 bez záruky.

Vloží nebo přepíše blok binárních dat na zadaný offset těla existujícího
MZF souboru. Hlavička cíle (typ, jméno, load/exec adresa, komentář)
zůstává zachována; mění se pouze tělo a pole `fsize` se přepočítá na
novou délku těla. Nástroj doplňuje `mzf-cat` (který umí pouze append
za sebou) o operace uprostřed těla a `mzf-hdr` (který se těla nedotýká)
o cílenou modifikaci binárního obsahu.

Typický use case je opravný patch, vložení runtime dat na pevný offset
nebo embedování pomocné tabulky do hotového MZF. Nástroj patří do rodiny
CLI projektu `bin2mzf` spolu s `bin2mzf`, `mzf-info`, `mzf-hdr`,
`mzf-strip` a `mzf-cat`. Sémantika je read-modify-write nad tělem:
v in-place režimu (bez `-o`) se cílový soubor přepisuje atomicky přes
pomocný `<TARGET>.tmp` a `rename()`.

## Použití

```
mzf-paste [options] --at OFFSET <INPUT.bin> <TARGET.mzf>
mzf-paste [options] --at OFFSET --from-mzf <INPUT.mzf> <TARGET.mzf>
mzf-paste [options] -o OUTPUT.mzf --at OFFSET <INPUT> <TARGET.mzf>
mzf-paste --version
mzf-paste --lib-versions
mzf-paste --help
```

## Argumenty

| Argument | Popis |
|----------|-------|
| `<INPUT>` | Vstupní binární soubor (nebo MZF s `--from-mzf`), právě jeden positional argument |
| `<TARGET.mzf>` | Cílový MZF soubor (modifikuje se in-place bez `-o`), právě jeden positional argument |

Žádný `-i`/`--input` flag - cesty se zadávají výhradně positional
argumenty (konzistence s `mzf-info`, `mzf-strip` a `mzf-hdr`). Žádný
stdin vstup.

## Volby

### Operační režim a pozice

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `--insert` | - | on | Vloží `INPUT` na `OFFSET`, zbytek těla se posune; tělo roste |
| `--overwrite` | - | off | Přepíše existující bajty od `OFFSET`; mutex s `--insert` |
| `--extend` | - | off | S `--overwrite` povolí přesah za konec těla |
| `--at OFFSET` | 0..0xFFFF nebo `end` | povinné | Offset v těle cíle (decimal, 0x-prefix nebo keyword `end`) |
| `--filler BYTE` | 0..255 nebo 0x00..0xFF | 0x00 | Výplňový bajt pro gap (offset > current body size) |

### Vstup a výstup

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `--from-mzf` | - | off | `INPUT` je MZF soubor - přeskočí 128B hlavičku, použije pouze tělo |
| `-o`, `--output FILE` | cesta | in-place | Výstupní MZF soubor; bez této volby se přepisuje `TARGET.mzf` |

### Info

| Volba | Hodnoty | Výchozí | Popis |
|-------|---------|---------|-------|
| `--version` | - | - | Vypíše verzi nástroje a skončí |
| `--lib-versions` | - | - | Vypíše verze linkovaných knihoven a skončí |
| `-h`, `--help` | - | - | Vypíše nápovědu a skončí |

Číselné hodnoty (`--at`, `--filler`) přijímají decimal i hexadecimal
s prefixem `0x` (např. `0x100` = `256`).

### Operační režimy (--insert vs --overwrite)

Tři kombinace módů určují, jak se vstupní data umístí do těla cíle:

- **`--insert`** (výchozí) - vstup se vloží na `OFFSET` a zbytek těla
  od tohoto bodu se posune doprava. Velikost těla roste o délku vstupu.
- **`--overwrite`** - vstup přepíše existující bajty od `OFFSET`. Velikost
  těla zůstává zachována, pokud vstup nepřesahuje konec těla.
- **`--overwrite --extend`** - varianta `--overwrite`, která povolí, aby
  vstup přesáhl konec těla. Velikost těla pak vzroste přesně o tu část,
  která se nevejde dovnitř.

Volby `--insert` a `--overwrite` jsou vzájemně výlučné. Kombinace skončí
tvrdou chybou se substringem `mutually exclusive`. Bez explicitní volby
módu platí výchozí `--insert`. Modifikátor `--extend` bez `--overwrite`
nemá efekt (silent ignore - bez chyby).

Chování podle vztahu `OFFSET` k aktuální velikosti těla:

| Mód | `OFFSET < current_size` | `OFFSET == current_size` | `OFFSET > current_size` |
|-----|-------------------------|--------------------------|-------------------------|
| `--insert` | vloží, posune zbytek | append na konec | gap-fill `--filler`, pak vloží |
| `--overwrite` | přepíše uvnitř těla | hard error (chybí `--extend`) | hard error (chybí `--extend`) |
| `--overwrite --extend` | přepíše uvnitř těla | append na konec | gap-fill `--filler`, pak zapíše |

Hard error `--overwrite` bez `--extend` při přesahu konce obsahuje
substring `use --extend` v hlášce.

Chování při gap-fill (`OFFSET > current_size`) je v obou případech
totožné: tělo se nejprve doplní `--filler` bajty od `current_size` do
`OFFSET` a teprve pak se zapíší vstupní data. Default `--filler` je
`0x00`, lze přepsat libovolnou hodnotou v rozsahu 0..0xFF. Velikost
mezery se započítává do výsledné `fsize` a podléhá horní hranici 65535
bajtů.

### Podrobnosti k --at OFFSET

Volba `--at` je povinná. Akceptuje tři formy:

| Forma | Příklad | Význam |
|-------|---------|--------|
| Decimal | `--at 1024` | Offset v decimální notaci, rozsah 0..65535 |
| Hexadecimal | `--at 0x400` | Offset s prefixem `0x`, stejný rozsah |
| Keyword `end` | `--at end` | Resolved na aktuální velikost těla cíle (= append) |

Forma `end` je užitečná zejména pro `--insert` (append vstupu za stávající
tělo) a pro `--overwrite --extend` (rozšíření těla přesně o vstupní data).
Hodnota `end` se vyhodnocuje až po načtení cíle, takže reflektuje skutečnou
aktuální `fsize` z hlavičky. Pro běžnou append operaci je výsledek shodný
s režimem `--insert --at end` (doporučená kombinace, mód `--insert` je
ostatně výchozí, takže lze psát i jen `--at end`).

Pokud `OFFSET` přesahuje aktuální velikost těla, chování se řídí maticí
v sekci výše. Výsledná velikost těla nesmí přesáhnout 65535 bajtů (limit
MZF formátu); jinak nástroj skončí tvrdou chybou se substringem `exceed`.

### Atomicita zápisu

Operace zápisu je atomická bez ohledu na režim. V in-place režimu
(bez `-o`) nástroj nikdy nepřepisuje cílový soubor přímo - místo
toho zapíše výsledek do pomocného `<TARGET>.tmp`, dokončený soubor
přejmenuje přes `rename()` na `<TARGET>` a starý soubor se atomicky
nahradí. Pokud zápis selže uprostřed (například při zaplněném
disku), původní `TARGET.mzf` zůstává nezměněn a `<TARGET>.tmp` se
maže/zůstává jako stopový artefakt podle stavu chyby.

V režimu s `-o OUTPUT.mzf` se zapisuje rovnou do cílového souboru -
`TARGET.mzf` zůstane nedotčen.

> **Caveat (Win32):** Atomic rename na Windows má krátké okno
> mezi `unlink` cílového souboru a `rename` z tmp na cíl, kdy cíl
> teoreticky neexistuje. Na POSIX (Linux/MSYS2 přes Cygwin runtime)
> je `rename()` atomický bez tohoto okna. Pro kritická data
> doporučujeme zálohu nebo použití `-o`.

### Vstup --from-mzf

Bez volby `--from-mzf` se `INPUT` čte jako raw binární soubor - libovolné
bajty bez interpretace.

S `--from-mzf` se `INPUT` načte jako MZF soubor: nástroj zvaliduje
128bajtovou hlavičku přes knihovnu `mzf` a pro paste operaci použije
pouze pole `body` (= prvních `header.fsize` bajtů těla). 128B hlavička
vstupního MZF se zahodí.

Typický use case: embedování pomocné MZF tabulky (například fontové
mapy, level dat nebo druhého programu) přímo do těla hostitelského MZF,
aniž by bylo nutné MZF nejdřív rozbalit přes `mzf-strip` do dočasné
binárky. Hlavička cílového MZF se nemění - vkládá/přepisuje se pouze
tělo.

Pokud má vstupní MZF nekonzistentní hlavičku (například chybný
terminátor jména nebo `fsize` mimo rozsah), `mzf_load` vrátí chybu
a `mzf-paste` skončí ne-nulovým exit kódem ještě před zápisem cíle.
Vstup se tedy chová jako read-only validovaný zdroj dat.

## Exit kódy

- `0` - úspěch (včetně výpisů `--help`, `--version`, `--lib-versions`).
- ne-`0` - chyba (parse argumentů, vzájemně výlučné volby, chybějící
  povinné `--at`, neplatný offset, I/O chyba, alokace, `--overwrite`
  past-end bez `--extend`, výsledné tělo přesahuje 65535 bajtů).

## Příklady

Vložení patche na pevný offset (insert je výchozí):

```bash
mzf-paste --at 0x100 patch.bin target.mzf
```

Přepis bloku metadat na offset 0x40:

```bash
mzf-paste --overwrite --at 0x40 metadata.bin target.mzf
```

Append na konec těla (insert s keyword `end`):

```bash
mzf-paste --at end footer.bin target.mzf
```

Overwrite s povoleným prodloužením těla:

```bash
mzf-paste --overwrite --extend --at 0x800 newdata.bin target.mzf
```

Insert na offset za koncem těla - mezera se vyplní `--filler`:

```bash
mzf-paste --at 0x500 --filler 0xFF newdata.bin target.mzf
```

Embedování těla jiného MZF (např. fontové tabulky) na konec:

```bash
mzf-paste --from-mzf --at end font.mzf main.mzf
```

Zápis do nového souboru místo in-place modifikace:

```bash
mzf-paste --at 0x100 patch.bin target.mzf -o patched.mzf
```

Pipeline: zobraz, modifikuj, znovu zobraz:

```bash
mzf-info target.mzf && mzf-paste --at 0x100 patch.bin target.mzf && mzf-info target.mzf
```

## Související nástroje

- `bin2mzf` - vytvoření MZF z binárky (rebuild celého souboru).
- `mzf-info` - inspekce existujícího MZF (hlavička, hexdump, validace).
- `mzf-hdr` - manipulace hlavičky bez změny těla.
- `mzf-strip` - extrakce těla z MZF (inverzní operace k `bin2mzf`).
- `mzf-cat` - spojení více binárek do jednoho MZF (např. CCP+BDOS+BIOS).

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
