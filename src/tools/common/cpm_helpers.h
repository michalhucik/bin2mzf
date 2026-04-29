/**
 * @file    cpm_helpers.h
 * @author  Michal Hucik <hucik@ordoz.com>
 * @brief   Sdílené parsery a transformace CP/M (SOKODI CMT.COM) konvence MZF.
 *
 * Modul drží encoder/decoder CP/M layoutu pole `fname.name[]` MZF hlavičky.
 * Funkce byly vyextrahovány z původně lokálních duplikátů v `bin2mzf.c`
 * (`parse_cpm_attr`, `normalize_cpm_field`, `build_cpm_fname`) a
 * `mzf_info.c` (`decode_cpm_header`).
 *
 * Naming konvence: prefix `mzf_cpm_*` (drop tooling-specifického prefixu,
 * konzistence se sdíleným modulem). Hodnoty bitmasky atributů a layoutové
 * konstanty viz `tools/common/cpm_constants.h`.
 *
 * @par Závislosti:
 *  - hlavička sama používá jen standardní C typy (`<stdint.h>`,
 *    `<stdbool.h>`, `<stddef.h>`) a `libs/mzf/mzf.h` pro `st_MZF_HEADER`.
 *  - implementace v `.c` závisí na `tools/common/cpm_constants.h`
 *    a `tools/common/text_field.h` (rtrim_ascii).
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#ifndef CPM_HELPERS_H
#define CPM_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "libs/mzf/mzf.h"
#include "tools/common/cpm_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Naparsuje argument `--cpm-attr` na bitmasku `MZF_CPM_ATTR_*`.
 *
 * Akceptovaná syntaxe:
 *  - case-insensitive
 *  - oddělovače mezi tokeny: čárka `,`, mezera, tabulátor, nebo žádný
 *    (concatenated) - např. `ro,sys`, `ro sys`, `roSys`.
 *  - tokeny: `ro` nebo `r/o` (Read-Only), `sys` (System), `arc` (Archived).
 *  - prázdný řetězec -> `*out_mask = 0`, return 0 (no-op).
 *  - duplikáty jsou idempotentní (OR).
 *  - neznámý nebo neúplný token = chyba.
 *
 * Token `r/o` (3 znaky) se zkouší před `ro` (2 znaky) - longest-match.
 * Bez toho by vstup `r/o,sys` selhal na `r` po pokusu o `ro`.
 *
 * @param s        Vstupní řetězec (může být prázdný; NULL je ošetřen
 *                 jako prázdný).
 * @param out_mask Výstup: bitmaska kombinace `MZF_CPM_ATTR_*`.
 *
 * @return 0 při úspěchu, -1 při chybě (neznámý token).
 *
 * Při chybě píše hlášku do `stderr` ve formátu
 * `"Error: invalid --cpm-attr token at '<context>'\n"`.
 */
int mzf_cpm_parse_attr ( const char *s, uint8_t *out_mask );

/**
 * @brief Validuje a normalizuje CP/M jméno nebo příponu (uppercase + space-pad).
 *
 * Algoritmus:
 *  1. Pokud `in == NULL` -> error (povinná hodnota).
 *  2. Spočítá `len = strlen(in)`. Prázdný řetězec -> error.
 *  3. `len > max_len` -> error (overflow).
 *  4. Pre-fill `out_buf` mezerami (0x20) na celých `max_len` bajtů.
 *  5. Pro každý bajt v rozsahu 0x20..0x7E aplikuje `toupper`. Bajty mimo
 *     printable ASCII (0x00-0x1F, 0x7F-0xFF) -> error.
 *
 * Validační hlášky jsou v angličtině (locales-ready) a obsahují
 * `field_label` ("name" / "ext") pro přesnou diagnostiku.
 *
 * @param in          Vstupní null-terminated řetězec (z `argv`).
 * @param max_len     Maximální délka pole (8 pro jméno, 3 pro příponu).
 * @param out_buf     Výstupní buffer délky přesně `max_len` bajtů.
 * @param field_label Lidský label pro diagnostické hlášky ("name", "ext").
 *
 * @return 0 při úspěchu, -1 při chybě.
 */
int mzf_cpm_normalize_field ( const char *in, unsigned max_len,
                              uint8_t *out_buf, const char *field_label );

/**
 * @brief Sestaví 13bajtový CP/M layout pole `fname.name[]` (SOKODI CMT.COM).
 *
 * Layout výstupu:
 *  - `out_buf[0..7]`  = jméno (8 znaků, doplněno mezerami 0x20, uppercase)
 *  - `out_buf[8]`     = '.' (0x2E) - oddělovač
 *  - `out_buf[9..11]` = přípona (3 znaky, padding mezerami, uppercase),
 *    bit 7 každého bajtu kóduje atribut:
 *      - `out_buf[9]`  bit 7 = R/O
 *      - `out_buf[10]` bit 7 = SYS
 *      - `out_buf[11]` bit 7 = ARC
 *  - `out_buf[12]`    = 0x0D - vnitřní CR za příponou (8.3+CR)
 *
 * Bajty `name[13..15]` (mimo tento buffer) doplní `mzf_tools_create_mzfhdr`
 * z úvodního `memset(0x0D)`. Sémanticky shodné s mzdisk decoderem
 * (decoder pozice 13..15 ignoruje), byte-non-identičnost s mzdisk
 * encoderem (ten tam dává 0x00) je akceptovaný kompromis.
 *
 * @param cpm_name  Jméno z `--cpm-name` (1..8 ASCII znaků).
 * @param cpm_ext   Přípona z `--cpm-ext` (1..3 ASCII znaky); volající
 *                  zajistí, že není NULL (defaultuje na "COM").
 * @param attr_mask Bitmaska atributů `MZF_CPM_ATTR_*` (0 = žádné).
 * @param out_buf   Výstupní buffer délky `MZF_CPM_FNAME_LEN` (13).
 *
 * @return 0 při úspěchu, -1 při chybě (validace `mzf_cpm_normalize_field`).
 */
int mzf_cpm_build_fname ( const char *cpm_name, const char *cpm_ext,
                          uint8_t attr_mask,
                          uint8_t out_buf[MZF_CPM_FNAME_LEN] );

/**
 * @brief Detekuje a dekóduje CP/M (SOKODI CMT.COM) layout pole `fname.name`.
 *
 * Pracuje výhradně nad raw bajty `hdr->fname.name[0..15]` - **nezávisle**
 * na charset dekódování přes `sharpmz_ascii`. To je nezbytné, protože
 * `sharpmz_cnv_from` převede bajty s nastaveným bitem 7 (CP/M atributy
 * 0xC3/0xCF/0xCD) na mezeru, čímž by se CP/M přípona ztratila.
 *
 * Postup:
 *  1. Validace `hdr->ftype == MZF_CPM_FTYPE` (0x22) - jinak false.
 *  2. Validace `name[8] == '.'` - jinak false (cizí MZF s 0x22 mimo SOKODI).
 *  3. Validace `name[12] == 0x0D` (knihovní default) **nebo** `0x00` (mzdisk
 *     encoder) - jinak false.
 *  4. Kopie `name[0..7]` do `out_name` a rtrim trailing mezer.
 *  5. Pro každý bajt přípony (`name[9..11]`):
 *     - bit 7 nastaven -> nastav příslušný atribut v `out_attr_mask`
 *       (pořadí 9->R/O, 10->SYS, 11->ARC),
 *     - kopíruj bajt s vymazaným bitem 7 do `out_ext`.
 *  6. Rtrim trailing mezer na `out_ext` (až po vymazání bitu 7, jinak by
 *     se 0xC3/0xCF/0xCD nezkrátilo).
 *
 * Pre/postconditions:
 *  - `hdr` nesmí být NULL.
 *  - `out_name` musí mít kapacitu alespoň `MZF_CPM_NAME_LEN` (8).
 *  - `out_ext`  musí mít kapacitu alespoň `MZF_CPM_EXT_LEN` (3).
 *  - Při návratu false jsou výstupní parametry v nedefinovaném stavu.
 *
 * @param hdr            Vstupní MZF hlavička (nesmí být NULL).
 * @param out_name       Výstup: bajty CP/M jména (bez paddingu).
 * @param out_name_len   Výstup: efektivní délka jména (0..8).
 * @param out_ext        Výstup: bajty CP/M přípony (bity 7 vymazané).
 * @param out_ext_len    Výstup: efektivní délka přípony (0..3).
 * @param out_attr_mask  Výstup: bitmaska atributů (`MZF_CPM_ATTR_RO|_SYS|_ARC`).
 * @return true pokud `hdr` odpovídá SOKODI CMT.COM layoutu, jinak false.
 */
bool mzf_cpm_decode_header ( const st_MZF_HEADER *hdr,
                             uint8_t *out_name, size_t *out_name_len,
                             uint8_t *out_ext,  size_t *out_ext_len,
                             uint8_t *out_attr_mask );

#ifdef __cplusplus
}
#endif

#endif /* CPM_HELPERS_H */
