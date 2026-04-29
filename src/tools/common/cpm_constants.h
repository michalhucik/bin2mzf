/**
 * @file    cpm_constants.h
 * @author  Michal Hucik <hucik@ordoz.com>
 * @brief   Sjednocené konstanty CP/M konvence (SOKODI CMT.COM) pro MZF.
 *
 * Modul drží konstanty popisující CP/M variantu MZF souboru. Knihovna
 * `mzf` v `src/libs/` (1:1 kopie z mzdisk) tyto symboly nemá - CP/M
 * konvence patří do `mzdsk_cpm`, ne do generického `mzf`. Pravidlo
 * "1:1 kopie z mzdisk" zakazuje lokální úpravy knihoven, proto definujeme
 * tyto konstanty zde, ve sdíleném common modulu rodiny `bin2mzf`.
 *
 * Konstanty byly sjednoceny z původně duplicitních lokálních definic
 * v `bin2mzf.c` (`BIN2MZF_CPM_*`) a `mzf_info.c` (`MZF_INFO_CPM_*`).
 * Drop tooling-specifického prefixu (`MZF_CPM_*`) odpovídá pravidlu
 * common modulu = neutrální naming.
 *
 * Hodnoty atributů jsou shodné s `MZDSK_CPM_ATTR_*` v mzdisku - kvůli
 * budoucí harmonizaci se sdíleným enum (až bude `mzdsk_cpm` třeba
 * importován do `src/libs/`).
 *
 * @par Layout pole `fname.name[]` v CP/M (SOKODI CMT.COM) konvenci:
 *  - `[0..7]`   = jméno (uppercase, padding mezerami)
 *  - `[8]`      = '.' (0x2E) - oddělovač
 *  - `[9..11]`  = přípona (uppercase, padding mezerami) + bit 7 = atribut
 *  - `[12]`     = 0x0D - vnitřní CR za příponou (8.3+CR)
 *  - `[13..15]` = vyplněno z úvodního `memset` (0x0D nebo 0x00 podle encoderu)
 *
 * @par Licence:
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2026 Michal Hucik <hucik@ordoz.com>
 */

#ifndef CPM_CONSTANTS_H
#define CPM_CONSTANTS_H

/** @brief Hodnota pole `ftype` v MZF hlavičce pro CP/M konvenci (SOKODI CMT.COM). */
#define MZF_CPM_FTYPE          0x22u

/** @brief Defaultní `fstrt` i `fexec` pro CP/M (TPA - .COM startuje na 0x0100). */
#define MZF_CPM_DEFAULT_ADDR   0x0100u

/** @brief Defaultní přípona CP/M souboru, pokud `--cpm-ext` chybí. */
#define MZF_CPM_DEFAULT_EXT    "COM"

/** @brief Maximální délka jména v CP/M 8.3 layoutu (8 znaků). */
#define MZF_CPM_NAME_LEN       8u

/** @brief Maximální délka přípony v CP/M 8.3 layoutu (3 znaky). */
#define MZF_CPM_EXT_LEN        3u

/**
 * @brief Délka kompletního CP/M layoutu uvnitř pole `fname.name[]`.
 *
 * 8 (jméno) + 1 (tečka) + 3 (přípona) + 1 (CR `0x0D` na pozici 12)
 * = 13 bajtů. Zbylé bajty `name[13..15]` doplní `mzf_tools_create_mzfhdr`
 * z úvodního `memset(0x0D)`. Sémanticky shodné s mzdisk decoderem,
 * který pozici 13..15 ignoruje.
 */
#define MZF_CPM_FNAME_LEN      13u

/** @brief Pozice oddělovače `.` v 8.3 layoutu (offset 8 v poli `fname.name`). */
#define MZF_CPM_DOT_POS        8u

/** @brief Pozice prvního bajtu přípony v 8.3 layoutu (offset 9 v poli `fname.name`). */
#define MZF_CPM_EXT_POS        9u

/**
 * @brief Pozice terminátoru CP/M jména (offset 12 v poli `fname.name`).
 *
 * Knihovní `mzf_tools_create_mzfhdr` vyplní tento offset hodnotou 0x0D
 * (`MZF_FNAME_TERMINATOR`); mzdisk encoder zapisuje 0x00. Detekce SOKODI
 * layoutu akceptuje obě hodnoty pro maximální kompatibilitu.
 */
#define MZF_CPM_TERM_POS       12u

/** @brief Maska bitu 7 - příznak nastaveného CP/M atributu v bajtu přípony. */
#define MZF_CPM_ATTR_BIT       0x80u

/** @brief Bit v masce atributů: Read-Only (kóduje se do bitu 7 `name[9]`). */
#define MZF_CPM_ATTR_RO        0x01u

/** @brief Bit v masce atributů: System (kóduje se do bitu 7 `name[10]`). */
#define MZF_CPM_ATTR_SYS       0x02u

/** @brief Bit v masce atributů: Archived (kóduje se do bitu 7 `name[11]`). */
#define MZF_CPM_ATTR_ARC       0x04u

/** @brief Maska všech tří podporovaných atributů. */
#define MZF_CPM_ATTR_ALL       (MZF_CPM_ATTR_RO | MZF_CPM_ATTR_SYS | MZF_CPM_ATTR_ARC)

#endif /* CPM_CONSTANTS_H */
