/**
 * @file    text_field.h
 * @brief   Sdílené helpery pro parsování argumentů velikosti/zarovnání
 *          a transformaci textových polí (name, comment) MZF hlavičky.
 *
 * Tyto helpery jsou sdílené napříč rodinou CLI nástrojů (bin2mzf,
 * mzf-cat, mzf-paste, mzf-hdr) - všechny producenti MZF hlavičky
 * potřebují stejnou pipeline:
 *  - parsování `--size N|auto`
 *  - parsování `--align-block N` / `--align-to N`
 *  - parsování C-style escape sekvencí (`\\`, `\n`, `\r`, `\t`, `\xNN`)
 *  - transformaci pole `name` / `comment` s charset konverzí
 *
 * Sémantika všech čtyř funkcí je identická s původní implementací
 * v `bin2mzf.c` (refactor 1:1).
 *
 * Návratová konvence:
 *  - 0  = úspěch
 *  - -1 = chyba
 *
 * @par Závislosti:
 *  - hlavička sama používá jen standardní C typy (`<stdint.h>`,
 *    `<stdbool.h>`, `<stddef.h>`).
 *  - implementace v `.c` závisí na `libs/mzf/mzf.h`,
 *    `libs/sharpmz_ascii/sharpmz_ascii.h` a `argparse_helpers.h`.
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#ifndef TEXT_FIELD_H
#define TEXT_FIELD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Druh textového pole pro pipeline `transform_text_field`.
 *
 * Určuje sémantiku validace délky po konverzi:
 *  - `NAME`: hard error při překročení limitu (16 bajtů).
 *  - `COMMENT`: warning + truncate při překročení limitu (104 bajty).
 *
 * Hláška warning pro komentář musí obsahovat slovo "truncated"
 * (kontrolováno regresním testem
 * `test_bin2mzf_basic.sh:test_comment_truncation_warning`).
 */
typedef enum en_TEXT_FIELD_KIND {
    TEXT_FIELD_NAME,        /**< Pole `fname` MZF hlavičky (max 16 bajtů). */
    TEXT_FIELD_COMMENT      /**< Pole `cmnt` MZF hlavičky (max 104 bajty). */
} en_TEXT_FIELD_KIND;

/**
 * @brief Naparsuje argument `--size N|auto`.
 *
 * Speciální řetězec `"auto"` (case-sensitive) vrátí `*has_size = false`,
 * `*out = 0`. Numerický vstup (decimal nebo 0x..) je validován v doméně
 * 1..MZF_MAX_BODY_SIZE. Hodnota 0 i hodnota nad limit jsou odmítnuty -
 * `--size 0` nemá v MZF formátu rozumnou interpretaci a nad-limit
 * překračuje fyzickou kapacitu těla.
 *
 * @param s        Vstupní řetězec.
 * @param out      Výstupní velikost (0 pokud "auto").
 * @param has_size Výstupní příznak: true pokud byla zadána numerická hodnota.
 * @return 0 při úspěchu, -1 při chybě.
 */
int parse_size_arg ( const char *s, uint16_t *out, bool *has_size );

/**
 * @brief Naparsuje hodnotu `--align-block N` / `--align-to N`.
 *
 * Doména: 1..MZF_MAX_BODY_SIZE (1..65535). Hodnota 0 je odmítnuta
 * (nemá smysluplnou interpretaci pro zarovnání). Hodnota nad
 * MZF_MAX_BODY_SIZE je odmítnuta (přetečení velikosti těla MZF).
 *
 * @param s   Vstupní řetězec.
 * @param out Výstupní hodnota (zapsána jen při úspěchu).
 * @return 0 při úspěchu, -1 při chybě (neplatný formát, 0, nad-limit).
 */
int parse_align_value ( const char *s, uint32_t *out );

/**
 * @brief Naparsuje C-style escape sekvence v textovém vstupu na binární bajty.
 *
 * Akceptuje:
 *  - `\\` -> 0x5C (zpětné lomítko)
 *  - `\n` -> 0x0A
 *  - `\r` -> 0x0D
 *  - `\t` -> 0x09
 *  - `\xNN` -> hex bajt (právě 2 hex číslice 0-9, a-f, A-F)
 *
 * Ostatní znaky se kopírují beze změny. Jakákoli neznámá escape
 * sekvence (např. `\q`, `\xZZ`, `\x` na konci stringu, `\` na konci
 * stringu) je chyba.
 *
 * @param src      Vstupní null-terminated řetězec (nesmí být NULL).
 * @param dst      Cílový buffer pro binární výstup.
 * @param dst_max  Kapacita `dst` v bajtech.
 * @param out_len  Výstup: počet zapsaných bajtů.
 *
 * @return 0 při úspěchu, -1 při chybě (neznámá escape, neúplná `\xNN`,
 *         přetečení `dst`).
 *
 * @note Funkce nezapisuje terminující null - výstup je čistá binární
 *       sekvence (escape `\x00` vyrobí nulový bajt uprostřed dat).
 */
int parse_c_escapes ( const char *src, uint8_t *dst, size_t dst_max,
                      size_t *out_len );

/**
 * @brief Provede transformační pipeline pro textové pole MZF (name/comment).
 *
 * Pipeline:
 *  1. Pokud `in == NULL` -> `*out_len = 0`, return 0.
 *  2. Detekce escape sekvencí: `strchr(in, '\\') != NULL`.
 *  3. Pokud escape přítomný: `effective_charset = NONE`,
 *     `effective_upper = false` (per-pole granularita).
 *  4. Naplnění tmp bufferu (raw kopie nebo `parse_c_escapes`).
 *  5. In-place uppercase 0x61-0x7A -> 0x41-0x5A (pokud aktivní).
 *  6. Konverze znakové sady přes `sharpmz_str_from_utf8` (EU/JP),
 *     nebo memcpy pro NONE.
 *  7. Validace výstupní délky:
 *     - `<= max_out`: úspěch.
 *     - `> max_out` && NAME: error.
 *     - `> max_out` && COMMENT: warning + truncate na `max_out` (success).
 *
 * Detekce escape přes `strchr` je deterministická a parser nepotřebuje
 * vracet `had_escape` flag - rozhoduje se před voláním.
 *
 * @param in          Vstupní null-terminated řetězec z argv (může být NULL).
 * @param charset     Volba znakové sady (`en_TOOL_CHARSET` jako int).
 * @param upper       True = aplikovat uppercase a-z -> A-Z před konverzí.
 * @param field_kind  `NAME` (hard error při overflow) nebo `COMMENT`
 *                    (warning + truncate při overflow).
 * @param max_out     Maximální cílová délka v bajtech (16 pro name,
 *                    104 pro cmnt).
 * @param out         Cílový buffer (musí mít kapacitu alespoň `max_out`).
 * @param out_len     Výstup: počet skutečně zapsaných bajtů (0..max_out).
 *
 * @return 0 při úspěchu (i při truncate komentáře), -1 při chybě
 *         (malformed escape, name overflow, neplatný charset).
 *
 * @note Pro UTF-8 vstup volá `sharpmz_str_from_utf8` s 4x kapacitou,
 *       aby zachytila skutečnou výstupní délku po konverzi (knihovna
 *       jinak mlčky ořezává).
 *
 * @note Hláška warning pro komentář **musí** obsahovat slovo "truncated"
 *       (kritický invariant - regresní test).
 */
int transform_text_field ( const char *in,
                           int charset,
                           bool upper,
                           en_TEXT_FIELD_KIND field_kind,
                           size_t max_out,
                           uint8_t *out,
                           size_t *out_len );

/**
 * @brief Spočítá délku bufferu po odstranění trailing ASCII mezer (0x20).
 *
 * Slouží k oříznutí pravostranného space-paddingu CP/M jména a přípony
 * podle 8.3 konvence (`bin2mzf --cpm` doplňuje krátká jména/přípony
 * mezerami do plné délky 8 / 3) a k podobným textovým úkolům.
 *
 * Funkce nemodifikuje vstupní buffer - vrací jen efektivní délku.
 *
 * @param buf  Vstupní buffer (nesmí být NULL pokud `len > 0`).
 * @param len  Aktuální délka v bajtech.
 * @return Délka po odříznutí všech trailing 0x20 bajtů (0 pokud je celý buffer mezerový).
 */
size_t rtrim_ascii ( const uint8_t *buf, size_t len );

#ifdef __cplusplus
}
#endif

#endif /* TEXT_FIELD_H */
