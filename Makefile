# ===========================================================================
#  bin2mzf build systém
#
#  make libs          -> build-libs/   (sdílené knihovny .a)
#  make cli           -> build-cli/    (CLI nástroje, závisí na libs)
#  make test          -> build-tests/  (C unit testy + CLI shell testy)
#  make test-libs     -> jen C unit testy knihoven
#  make test-cli      -> jen integrační shell testy CLI nástrojů
#  make all           -> libs + cli (default)
#  make install       -> instalace binárek a docs do $(PREFIX) (default /usr/local)
#  make uninstall     -> odebere instalované soubory z $(PREFIX)
#  make help          -> vypíše tento header
#  make clean         -> vyčistí vše (libs + cli + testy)
# ===========================================================================

# Detekce platformy: MSYS2/MinGW vs Linux/POSIX
ifneq ($(MSYSTEM),)
    export TEMP := $(shell cygpath -w /tmp)
    export TMP  := $(TEMP)
    EXE_SUFFIX := .exe
    CMAKE_GENERATOR := MSYS Makefiles
else
    CMAKE_GENERATOR := Unix Makefiles
endif

CMAKE_CONFIGURE_LIBS  := cmake -S src/libs -B build-libs -G "$(CMAKE_GENERATOR)"
CMAKE_CONFIGURE_CLI   := cmake -S src/tools -B build-cli -G "$(CMAKE_GENERATOR)"
CMAKE_CONFIGURE_TESTS := cmake -S tests -B build-tests -G "$(CMAKE_GENERATOR)"

BUILD_LIBS_DIR    := build-libs
BUILD_CLI_DIR     := build-cli
BUILD_TESTS_DIR   := build-tests

# Release verze rodiny CLI nástrojů. Načítá se přímo z hlavičkového
# souboru, aby byl zdrojem pravdy C makro (nedochází k divergenci
# mezi Makefile a kódem).
BIN2MZF_CLI_VERSION := $(shell awk '/define[[:space:]]+BIN2MZF_CLI_RELEASE_VERSION[[:space:]]+"/ { gsub(/"/,"",$$3); print $$3 }' src/tools/common/bin2mzf_cli_version.h)

# Instalační prefix - lze přepsat z příkazové řádky:
#   make install PREFIX=~/.local
# Default odpovídá GNU coding standards.
PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin
DOCDIR  := $(PREFIX)/share/doc/bin2mzf

JOBS              := $(shell nproc)

.PHONY: all libs cli clean libs-clean cli-clean libs-rebuild cli-rebuild rebuild \
        test test-libs test-cli tests-clean \
        install uninstall help

# --- Implicitní target: knihovny + CLI ---

all: cli

# --- Knihovny (build-libs/) ---

libs: $(BUILD_LIBS_DIR)/Makefile
	cmake --build $(BUILD_LIBS_DIR) -j$(JOBS)

$(BUILD_LIBS_DIR)/Makefile:
	$(CMAKE_CONFIGURE_LIBS)

libs-clean:
	@if [ -d $(BUILD_LIBS_DIR) ]; then rm -rf $(BUILD_LIBS_DIR); fi

libs-rebuild: libs-clean libs

# --- CLI nástroje (build-cli/, závisí na libs) ---

cli: libs $(BUILD_CLI_DIR)/Makefile
	cmake --build $(BUILD_CLI_DIR) -j$(JOBS)

$(BUILD_CLI_DIR)/Makefile:
	$(CMAKE_CONFIGURE_CLI)

cli-clean:
	@if [ -d $(BUILD_CLI_DIR) ]; then rm -rf $(BUILD_CLI_DIR); fi

cli-rebuild: cli-clean cli

# --- Testy (build-tests/, závisí na libs) ---

test: test-libs test-cli

test-libs: libs $(BUILD_TESTS_DIR)/Makefile
	cmake --build $(BUILD_TESTS_DIR) -j$(JOBS)
	@echo ""
	@echo "=== Running C unit tests ==="
	@failed=0; \
	for t in $(BUILD_TESTS_DIR)/test_*$(EXE_SUFFIX); do \
		if [ -x "$$t" ]; then \
			echo ""; \
			$$t || failed=1; \
		fi; \
	done; \
	echo ""; \
	if [ $$failed -ne 0 ]; then \
		echo "*** SOME TESTS FAILED ***"; \
		exit 1; \
	else \
		echo "=== All tests passed ==="; \
	fi

test-cli: cli
	@echo ""
	@echo "=== Running CLI integration tests ==="
	@failed=0; \
	for t in tests/cli/test_*.sh; do \
		if [ -x "$$t" ] || [ -f "$$t" ]; then \
			echo ""; \
			bash "$$t" </dev/null || failed=1; \
		fi; \
	done; \
	echo ""; \
	if [ $$failed -ne 0 ]; then \
		echo "*** SOME CLI TESTS FAILED ***"; \
		exit 1; \
	else \
		echo "=== All CLI tests passed ==="; \
	fi

$(BUILD_TESTS_DIR)/Makefile:
	$(CMAKE_CONFIGURE_TESTS)

tests-clean:
	@if [ -d $(BUILD_TESTS_DIR) ]; then rm -rf $(BUILD_TESTS_DIR); fi

# --- Instalace (binárky + dokumentace) ---
#
# Umístění:
#   $(PREFIX)/bin/                   - šest CLI binárek rodiny
#   $(PREFIX)/share/doc/bin2mzf/cz/  - česká dokumentace (.md)
#   $(PREFIX)/share/doc/bin2mzf/en/  - anglická dokumentace (.md)
#   $(PREFIX)/share/doc/bin2mzf/     - LICENSE, README.md
#
# Default $(PREFIX) je /usr/local, lze přepsat: `make install PREFIX=~/.local`.

install: cli
	@echo ""
	@echo "=== Installing bin2mzf-cli $(BIN2MZF_CLI_VERSION) into $(PREFIX) ==="
	@install -d $(BINDIR)
	@install -d $(DOCDIR)/cz
	@install -d $(DOCDIR)/en
	@install -m 0755 $(BUILD_CLI_DIR)/bin2mzf$(EXE_SUFFIX) $(BINDIR)/
	@install -m 0755 $(BUILD_CLI_DIR)/mzf-info$(EXE_SUFFIX) $(BINDIR)/
	@install -m 0755 $(BUILD_CLI_DIR)/mzf-hdr$(EXE_SUFFIX) $(BINDIR)/
	@install -m 0755 $(BUILD_CLI_DIR)/mzf-strip$(EXE_SUFFIX) $(BINDIR)/
	@install -m 0755 $(BUILD_CLI_DIR)/mzf-cat$(EXE_SUFFIX) $(BINDIR)/
	@install -m 0755 $(BUILD_CLI_DIR)/mzf-paste$(EXE_SUFFIX) $(BINDIR)/
	@install -m 0644 docs/cz/*.md $(DOCDIR)/cz/
	@install -m 0644 docs/en/*.md $(DOCDIR)/en/
	@install -m 0644 README.md $(DOCDIR)/
	@install -m 0644 LICENSE $(DOCDIR)/
	@echo ""
	@echo "=== Done: bin2mzf-cli $(BIN2MZF_CLI_VERSION) -> $(PREFIX) ==="

uninstall:
	@echo ""
	@echo "=== Uninstalling bin2mzf-cli from $(PREFIX) ==="
	@rm -f $(BINDIR)/bin2mzf$(EXE_SUFFIX)
	@rm -f $(BINDIR)/mzf-info$(EXE_SUFFIX)
	@rm -f $(BINDIR)/mzf-hdr$(EXE_SUFFIX)
	@rm -f $(BINDIR)/mzf-strip$(EXE_SUFFIX)
	@rm -f $(BINDIR)/mzf-cat$(EXE_SUFFIX)
	@rm -f $(BINDIR)/mzf-paste$(EXE_SUFFIX)
	@rm -rf $(DOCDIR)
	@echo "=== Done ==="

# --- Help ---

help:
	@sed -n '/^# =\+/,/^# =\+$$/p' Makefile | sed 's/^# \?//'

# --- Čištění ---

clean: libs-clean cli-clean tests-clean

rebuild: clean libs cli
