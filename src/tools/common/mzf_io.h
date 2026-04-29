/**
 * @file   mzf_io.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @brief  Sdílené I/O helpery rodiny bin2mzf (read, align, write).
 *
 * Modul obsahuje funkce pro:
 *  - načtení binárního souboru do bufferu s rolling offsetem
 *    (`read_file_to_buffer_at`),
 *  - zarovnání těla MZF na násobek bloku nebo na přesnou velikost
 *    (`apply_alignment`),
 *  - zápis kompletní MZF struktury do souboru (`write_mzf_to_file`)
 *    nebo na stdout (`write_mzf_to_stdout`).
 *
 * Sdíleno mezi nástroji `bin2mzf`, `mzf-cat` a `mzf-paste`. Všechny
 * funkce vrací 0 při úspěchu nebo -1 při chybě a chybovou hlášku
 * vypisují na stderr (anglicky, locales-ready).
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#ifndef MZF_IO_H
#define MZF_IO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "libs/mzf/mzf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Načte celý obsah souboru do bufferu od daného offsetu (rolling).
 *
 * Otevře `path` pro binární čtení a chunked `fread` smyčkou naplní
 * `buf + buf_offset`. Detekce přetečení: pokud po naplnění dostupné
 * kapacity zbývá ještě bajt navíc, vrátí chybu a vypíše hlášku na
 * stderr (substring `"exceed"` a `"maximum body size"`).
 *
 * @param path         Cesta k souboru. Nesmí být NULL.
 * @param buf          Cílový buffer. Nesmí být NULL.
 * @param buf_offset   Offset v `buf`, kam se má začít zapisovat.
 *                     Musí platit `buf_offset <= buf_capacity`.
 * @param buf_capacity Celková kapacita bufferu v bajtech.
 * @param bytes_read   Výstup: počet skutečně načtených bajtů z tohoto
 *                     souboru (bez `buf_offset`). Nesmí být NULL.
 *
 * @return 0 při úspěchu, -1 při I/O chybě, neplatných parametrech
 *         nebo přetečení dostupné kapacity.
 *
 * @par Side effects:
 *  - Při chybě píše hlášku na stderr.
 *  - Modifikuje `buf[buf_offset .. buf_offset + *bytes_read)`.
 */
int read_file_to_buffer_at ( const char *path, uint8_t *buf,
                             size_t buf_offset, size_t buf_capacity,
                             size_t *bytes_read );

/**
 * @brief Aplikuje zarovnání nebo vynucenou velikost na tělo souboru.
 *
 * Modifikuje obsah bufferu `body_buf` a hodnotu `*body_size` podle
 * voleb `align_block`, `align_to` a `has_align_to`:
 *
 *  - `align_block != 0`: zarovná velikost těla na nejbližší vyšší
 *    násobek `align_block`. Padding bajty od původního konce po nový
 *    konec se nastaví na `filler`. Pokud výsledek překročí
 *    MZF_MAX_BODY_SIZE, vrátí chybu.
 *  - `has_align_to == true`: vynutí přesnou cílovou velikost `align_to`.
 *      - `*body_size > align_to`: warning (slovo **"truncated"**
 *        je povinný invariant pro testy) + ořez na `align_to`.
 *      - `*body_size < align_to`: padding `filler` až do `align_to`.
 *      - `*body_size == align_to`: no-op.
 *
 * Volby `align_block` a `has_align_to` jsou typicky cross-validovány
 * v `parse_args` jako vzájemně výlučné, ale funkce by zvládla obě
 * (nejprve align_block, pak align_to).
 *
 * Vstupní invariant: `*body_size <= MZF_MAX_BODY_SIZE`,
 * `buf_capacity >= MZF_MAX_BODY_SIZE + 1`.
 *
 * @param body_buf      In/out buffer s tělem.
 * @param buf_capacity  Kapacita bufferu v bajtech (defenzivní kontrola).
 * @param body_size     In/out aktuální velikost tělesa v bajtech.
 * @param align_block   Násobek pro --align-block (0 = neaplikovat).
 * @param align_to      Cílová velikost pro --align-to (validní jen
 *                      při `has_align_to == true`).
 * @param has_align_to  Rozlišení align_to=0 vs nepoužito.
 * @param filler        Padding bajt.
 *
 * @return 0 při úspěchu, -1 při chybě (overflow nad MZF_MAX_BODY_SIZE
 *         nebo nedostatečná kapacita bufferu).
 */
int apply_alignment ( uint8_t *body_buf, size_t buf_capacity,
                      size_t *body_size,
                      uint32_t align_block,
                      uint32_t align_to, bool has_align_to,
                      uint8_t filler );

/**
 * @brief Zapíše MZF strukturu do souboru přes generic_driver.
 *
 * Otevře cestu pro zápis (FILE_DRIVER_OPMODE_W), zavolá `mzf_save`
 * a stream uzavře. Při chybě vypíše hlášku na stderr.
 *
 * @param path Cílová cesta. Nesmí být NULL.
 * @param mzf  MZF data k zápisu. Nesmí být NULL.
 *
 * @return 0 při úspěchu, -1 při chybě.
 */
int write_mzf_to_file ( const char *path, const st_MZF *mzf );

/**
 * @brief Zapíše MZF strukturu na stdout přes paměťový driver.
 *
 * Otevře paměťový handler s přesnou velikostí (128 + body_size),
 * nechá `mzf_save` zapsat data do paměti a pak `fwrite` přesype obsah
 * na stdout. Volajícímu nepřísluší řešit binární režim stdout - to
 * udělá `main()` přes `setmode(_O_BINARY)` na Win32.
 *
 * @param mzf MZF data k zápisu. Nesmí být NULL.
 *
 * @return 0 při úspěchu, -1 při chybě (alokace, encode error, short write).
 */
int write_mzf_to_stdout ( const st_MZF *mzf );

/**
 * @brief Načte MZF soubor z disku do nově alokované struktury `st_MZF`.
 *
 * Postup:
 *  1. Načte celý obsah souboru do dočasného bufferu (limit
 *     `MZF_HEADER_SIZE + MZF_MAX_BODY_SIZE + 1`).
 *  2. Validuje minimální velikost (alespoň 128 B - hlavička).
 *  3. Vytvoří paměťový handler, naplní ho daty.
 *  4. Zavolá `mzf_load`, který provede konverzi endianity a alokaci
 *     `st_MZF` včetně těla.
 *
 * Při chybě (I/O, nevalidní MZF, truncated body) vypíše hlášku na
 * stderr a vrátí -1. Volajícímu náleží `mzf_free(*out_mzf)` po použití.
 *
 * @param path     Cesta k MZF souboru (NULL = chyba).
 * @param out_mzf  Výstup: alokovaná struktura `st_MZF` (volající free).
 *                 Při chybě se nemodifikuje vůli `*out_mzf = NULL` na začátku.
 *
 * @return 0 OK, -1 chyba.
 *
 * @par Side effects:
 *  - alokuje a uvolňuje dočasný buffer (peak ~64 KiB).
 *  - alokuje `st_MZF` včetně těla (volající uvolní `mzf_free`).
 *  - při chybě píše hlášku na stderr.
 */
int load_mzf_from_file ( const char *path, st_MZF **out_mzf );

/**
 * @brief Zapíše MZF in-place s atomic tmp + rename.
 *
 * Postup:
 *  1. Sestaví dočasné jméno (`<path>.tmp`).
 *  2. Zapíše MZF do tmp přes `write_mzf_to_file`. Při chybě smaže tmp.
 *  3. Na Win32 nejdřív `unlink(path)` (rename přes existující selže
 *     na MSYS2/MinGW). ENOENT se ignoruje.
 *  4. `rename(tmp, path)`. Při chybě smaže tmp a hlásí chybu.
 *
 * Není ACID-atomic na Windows kvůli mezeře mezi `unlink` a `rename`,
 * ale pro CLI nástroj single-process je to dostatečné.
 *
 * @param path Cílová cesta (modifikuje se in-place).
 * @param mzf  MZF data k zápisu.
 *
 * @return 0 OK, -1 chyba.
 */
int write_mzf_in_place ( const char *path, const st_MZF *mzf );

#ifdef __cplusplus
}
#endif

#endif /* MZF_IO_H */
