/**
 * @file    argparse_helpers.h
 * @brief   Sdílené parsery argumentů pro CLI nástroje rodiny bin2mzf.
 *
 * Tato hlavička poskytuje sadu parserů, které jsou shodné napříč více
 * binárkami rodiny (bin2mzf, mzf-info, v budoucnu mzf-hdr, ...).
 * Sjednocuje sémantiku číselných formátů (decimální i 0x.. hex),
 * symbolických typů souboru (obj/btx/bsd/brd/rb) a voleb znakové sady.
 *
 * Návratová konvence všech parserů je stejná:
 *  - 0  = úspěch (výstupní hodnota je platná)
 *  - -1 = chyba (neplatný formát, hodnota mimo rozsah, NULL vstup)
 *
 * Při chybě parsery nemodifikují výstupní parametr.
 *
 * @par Závislost na knihovnách:
 *  - `libs/mzf/mzf.h` kvůli konstantám `MZF_FTYPE_*`
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#ifndef ARGPARSE_HELPERS_H
#define ARGPARSE_HELPERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Volba znakové sady pro pole `fname` a `cmnt` MZF hlavičky.
 *
 * Sdílený enum pro celou rodinu CLI nástrojů (`bin2mzf`, `mzf-info`,
 * `mzf-hdr`). Sémantika v producentu (`bin2mzf`) i konzumentu
 * (`mzf-info`) je totožná - varianty `EU` a `UTF8_EU` jsou implementačně
 * sjednocené (jediný code path), stejně jako `JP` a `UTF8_JP`.
 * Existence dvojic je explicitní intent-marker pro uživatele.
 *
 * V producentu volba určuje, jak se vstupní řetězec z argv konvertuje
 * před zápisem do hlavičky. V konzumentu (`mzf-info`) určuje, jak se
 * bajty z hlavičky dekódují zpět na UTF-8 výstup.
 *
 * Hodnota `NONE` přeskočí jakoukoli konverzi (raw bajty).
 */
typedef enum en_TOOL_CHARSET {
    TOOL_CHARSET_EU = 0,    /**< Sharp MZ EU (default) - lowercase a-z na 0x9A-0xC0, evropské speciální znaky. */
    TOOL_CHARSET_JP,        /**< Sharp MZ JP - rozsah 0x00-0x5D identita, ostatní -> 0x20 (mezera). */
    TOOL_CHARSET_UTF8_EU,   /**< Synonymum k EU s explicitním označením, že vstup/výstup je UTF-8. */
    TOOL_CHARSET_UTF8_JP,   /**< Synonymum k JP s explicitním označením, že vstup/výstup je UTF-8. */
    TOOL_CHARSET_NONE       /**< Žádná konverze - pracuje se přímo s bajty. */
} en_TOOL_CHARSET;

/**
 * @brief Naparsuje uint16_t hodnotu z řetězce (decimal nebo 0x...).
 *
 * Akceptuje formáty:
 *  - decimální: `4608`
 *  - hexadecimální: `0x1200`, `0X1200`
 *  - oktalová s prefixem `0`: `04608` (díky `strtoul` base=0)
 *
 * Hodnota mimo rozsah `0..0xFFFF` je odmítnuta. Prázdný řetězec nebo
 * řetězec s netriviálními znaky za číslem je odmítnut.
 *
 * @param s   Vstupní řetězec (NULL i prázdný řetězec jsou chyba).
 * @param out Výstupní hodnota (zapsána pouze při úspěchu, NULL = chyba).
 * @return 0 při úspěchu, -1 při chybě (neplatný formát, přetečení, NULL).
 *
 * @note Funkce nastavuje a čte `errno`. Volající nemusí `errno` resetovat
 *       předem - parser to dělá interně.
 */
int argparse_uint16 ( const char *s, uint16_t *out );

/**
 * @brief Naparsuje jeden bajt (0..0xFF) z řetězce.
 *
 * Akceptuje stejné formáty jako `argparse_uint16` (decimal, 0x..),
 * následně validuje horní limit 0xFF. Hodnota mimo rozsah je odmítnuta.
 *
 * @param s   Vstupní řetězec.
 * @param out Výstupní hodnota (zapsána pouze při úspěchu, NULL = chyba).
 * @return 0 při úspěchu, -1 při chybě (neplatný formát, přetečení nad 0xFF, NULL).
 */
int argparse_byte ( const char *s, uint8_t *out );

/**
 * @brief Naparsuje argument volby `--charset` do enumu.
 *
 * Akceptuje právě tyto řetězce (case-sensitive):
 *  - `"eu"`        -> `TOOL_CHARSET_EU`
 *  - `"jp"`        -> `TOOL_CHARSET_JP`
 *  - `"utf8-eu"`   -> `TOOL_CHARSET_UTF8_EU`
 *  - `"utf8-jp"`   -> `TOOL_CHARSET_UTF8_JP`
 *  - `"none"`      -> `TOOL_CHARSET_NONE`
 *
 * Cokoli jiného (včetně NULL) je odmítnuto.
 *
 * @param s   Vstupní řetězec z `optarg`.
 * @param out Výstupní enum hodnota (zapsána pouze při úspěchu).
 * @return 0 při úspěchu, -1 pokud řetězec neodpovídá žádné variantě.
 */
int argparse_charset ( const char *s, en_TOOL_CHARSET *out );

/**
 * @brief Naparsuje hodnotu typu souboru (ftype) z řetězce.
 *
 * Akceptuje:
 *  - symbolické názvy (case-insensitive): `obj`, `btx`, `bsd`, `brd`, `rb`
 *  - číselné hodnoty: decimal nebo 0x.. v rozsahu 0..0xFF
 *
 * Symbolické názvy se zkouší jako první. Pokud je vstupní řetězec delší
 * než nejdelší symbolické jméno (tj. evidentně nepasuje), funkce přejde
 * rovnou na číselný parser.
 *
 * @param s   Vstupní řetězec.
 * @param out Výstupní ftype (zapsán pouze při úspěchu).
 * @return 0 při úspěchu, -1 při chybě.
 */
int argparse_ftype ( const char *s, uint8_t *out );

#ifdef __cplusplus
}
#endif

#endif /* ARGPARSE_HELPERS_H */
