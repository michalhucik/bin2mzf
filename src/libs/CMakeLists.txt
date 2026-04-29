# ===========================================================================
#  Sdílené knihovny projektu bin2mzf
#
#  Kompilují se jednou do build-libs/ a používá je src/tools/ i tests/.
#  Samostatný CMake projekt - volá se přes Makefile target `make libs`.
# ===========================================================================

cmake_minimum_required(VERSION 3.16)
project(bin2mzf-libs C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -pipe)
add_link_options(-pipe)

# Standalone build - bez baseui z mz800emu
add_compile_definitions(GENERIC_DRIVER_NO_BASEUI)
add_compile_definitions(GENERIC_DRIVER_FILE_CB)
add_compile_definitions(GENERIC_DRIVER_MEMORY_CB)

# Include path - styl "libs/..." vyžaduje src/ jako kořen
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# .a soubory přímo do kořene build adresáře (build-libs/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- Knihovny ---
# Sada zkopírovaná z ~/projects/mzdisk/src/libs/ (snapshot 2026-04-27).
# Zdrojový vzor: mzdisk/src/libs/CMakeLists.txt:27-45.
# Pravidlo: 1:1 kopie, lokální úpravy zakázány (synchronizace s mzdisk).
# Závislosti mezi statickými knihovnami se neřeší přes target_link_libraries -
# linkování zařizuje až konzument (tools/tests) přes -Wl,--start-group.

add_library(generic_driver STATIC
    generic_driver/generic_driver.c
    generic_driver/memory_driver.c
)

add_library(endianity STATIC
    endianity/endianity.c
)

add_library(sharpmz_ascii STATIC
    sharpmz_ascii/sharpmz_ascii.c
)

add_library(mzf STATIC
    mzf/mzf.c
    mzf/mzf_tools.c
)

add_library(output_format STATIC
    output_format/output_format.c
)
