ifeq ($(OS),Windows_NT)
$(error Windows is not supported)
endif

LANGUAGE_NAME := tree-sitter-rst
HOMEPAGE_URL := https://github.com/brandon-arrendondo/tree-sitter-rst-next
VERSION := 0.3.0

# repository
SRC_DIR := src

TS ?= tree-sitter

# install directory layout
PREFIX ?= /usr/local
DATADIR ?= $(PREFIX)/share
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
PCLIBDIR ?= $(LIBDIR)/pkgconfig

# source/object files
PARSER := $(SRC_DIR)/parser.c
EXTRAS := $(filter-out $(PARSER),$(wildcard $(SRC_DIR)/*.c))
OBJS := $(patsubst %.c,%.o,$(PARSER) $(EXTRAS))

# flags
ARFLAGS ?= rcs
override CFLAGS += -I$(SRC_DIR) -std=c11 -fPIC

# ABI versioning
SONAME_MAJOR = $(shell sed -n 's/\#define LANGUAGE_VERSION //p' $(PARSER))
SONAME_MINOR = $(word 1,$(subst ., ,$(VERSION)))

# OS-specific bits
ifeq ($(shell uname),Darwin)
	SOEXT = dylib
	SOEXTVER_MAJOR = $(SONAME_MAJOR).$(SOEXT)
	SOEXTVER = $(SONAME_MAJOR).$(SONAME_MINOR).$(SOEXT)
	LINKSHARED = -dynamiclib -Wl,-install_name,$(LIBDIR)/lib$(LANGUAGE_NAME).$(SOEXTVER),-rpath,@executable_path/../Frameworks
else
	SOEXT = so
	SOEXTVER_MAJOR = $(SOEXT).$(SONAME_MAJOR)
	SOEXTVER = $(SOEXT).$(SONAME_MAJOR).$(SONAME_MINOR)
	LINKSHARED = -shared -Wl,-soname,lib$(LANGUAGE_NAME).$(SOEXTVER)
endif
ifneq ($(filter $(shell uname),FreeBSD NetBSD DragonFly),)
	PCLIBDIR := $(PREFIX)/libdata/pkgconfig
endif

all: lib$(LANGUAGE_NAME).a lib$(LANGUAGE_NAME).$(SOEXT) $(LANGUAGE_NAME).pc

lib$(LANGUAGE_NAME).a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

lib$(LANGUAGE_NAME).$(SOEXT): $(OBJS)
	$(CC) $(LDFLAGS) $(LINKSHARED) $^ $(LDLIBS) -o $@
ifneq ($(STRIP),)
	$(STRIP) $@
endif

$(LANGUAGE_NAME).pc: bindings/c/$(LANGUAGE_NAME).pc.in
	sed -e 's|@PROJECT_VERSION@|$(VERSION)|' \
		-e 's|@CMAKE_INSTALL_LIBDIR@|$(LIBDIR:$(PREFIX)/%=%)|' \
		-e 's|@CMAKE_INSTALL_INCLUDEDIR@|$(INCLUDEDIR:$(PREFIX)/%=%)|' \
		-e 's|@PROJECT_DESCRIPTION@|$(DESCRIPTION)|' \
		-e 's|@PROJECT_HOMEPAGE_URL@|$(HOMEPAGE_URL)|' \
		-e 's|@CMAKE_INSTALL_PREFIX@|$(PREFIX)|' $< > $@

$(PARSER): $(SRC_DIR)/grammar.json
	$(TS) generate $^

install: all
	install -d '$(DESTDIR)$(INCLUDEDIR)'/tree_sitter '$(DESTDIR)$(PCLIBDIR)' '$(DESTDIR)$(LIBDIR)'
	install -m644 bindings/c/tree_sitter/$(LANGUAGE_NAME).h '$(DESTDIR)$(INCLUDEDIR)'/tree_sitter/$(LANGUAGE_NAME).h
	install -m644 $(LANGUAGE_NAME).pc '$(DESTDIR)$(PCLIBDIR)'/$(LANGUAGE_NAME).pc
	install -m644 lib$(LANGUAGE_NAME).a '$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).a
	install -m755 lib$(LANGUAGE_NAME).$(SOEXT) '$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).$(SOEXTVER)
	ln -sf lib$(LANGUAGE_NAME).$(SOEXTVER) '$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).$(SOEXTVER_MAJOR)
	ln -sf lib$(LANGUAGE_NAME).$(SOEXTVER_MAJOR) '$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).$(SOEXT)

uninstall:
	$(RM) '$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).a \
		'$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).$(SOEXTVER) \
		'$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).$(SOEXTVER_MAJOR) \
		'$(DESTDIR)$(LIBDIR)'/lib$(LANGUAGE_NAME).$(SOEXT) \
		'$(DESTDIR)$(INCLUDEDIR)'/tree_sitter/$(LANGUAGE_NAME).h \
		'$(DESTDIR)$(PCLIBDIR)'/$(LANGUAGE_NAME).pc

clean:
	$(RM) $(OBJS) $(LANGUAGE_NAME).pc lib$(LANGUAGE_NAME).a lib$(LANGUAGE_NAME).$(SOEXT)

test:
	$(TS) test

release: all format
	npm run prestart
	# GitHub pages doesn't like symbolic links
	cp tree-sitter-rst.wasm docs/js/tree-sitter-parser.wasm

update-examples:
	# https://sourceforge.net/p/docutils/code/HEAD/tree/trunk/docutils/test/functional/input/
	rm -rf test/examples/
	curl \
	  https://sourceforge.net/code-snapshots/svn/d/do/docutils/code/docutils-code-r8531-trunk-docutils-test-functional-input.zip \
	  --create-dirs \
	  -o test/examples/examples.zip
	unzip -jq test/examples/examples.zip "*.txt" -d test/examples
	rm test/examples/examples.zip
	rename .txt .rst test/examples/*.txt

parse-examples:
	npm run parse -- -q test/examples/*.rst

	# Tables aren't supported yet
	! npm run parse -- -q test/examples/fail/*.rst

serve: all
	npm run prestart
	npm run start

format:
	moldy --in-place src/scanner.c src/tree_sitter_rst/*

# Lint targets — run both with `make lint`
# Both tools analyze src/scanner.c, which #includes the hand-written files
# in src/tree_sitter_rst/ (parser.c among them). The generated src/parser.c
# is a separate translation unit and is not analyzed. clang-tidy reports
# diagnostics in an included file only when --header-filter matches it, so
# the filter names the hand-written directory and leaves out the generated
# headers in src/tree_sitter/. A warning fails the target.
CLANG_TIDY_HEADER_FILTER := src/tree_sitter_rst/.*

# moldy formats the scanner (see .pre-commit-config.yaml).
lint: lint-tidy lint-cppcheck

lint-tidy:
	clang-tidy \
	  --header-filter='$(CLANG_TIDY_HEADER_FILTER)' \
	  --warnings-as-errors='*' \
	  src/scanner.c \
	  -- -Isrc/ -std=c11

lint-cppcheck:
	cppcheck --enable=warning,performance \
	  --error-exitcode=1 \
	  --suppress=missingIncludeSystem \
	  -Isrc/ \
	  src/scanner.c

format-python:
	ruff format .
	ruff check --fix .

gen-punctuation-chars:
	./utils/gen_punctuation_chars.py > ./src/tree_sitter_rst/punctuation_chars.h

# Regenerate parser sources from grammar.js / tree-sitter.json. This only
# rewrites src/parser.c, src/grammar.json, and src/node-types.json — it
# leaves the language bindings (Makefile, setup.py, pyproject.toml,
# bindings/**) alone. Refreshing those is a manual `tree-sitter init
# --update` operation.
generate-bindings:
	$(TS) generate

wasm:
	$(TS) build --wasm

.PHONY: all install uninstall clean test release update-examples parse-examples serve format lint lint-tidy lint-cppcheck format-python check generate-bindings wasm
