UNAME_S := $(shell uname -s)

BUILD_DIR  ?= build
BUILD_TYPE ?= Release

# Prefer Ninja when available for fast incremental builds; fall back to Make.
ifeq ($(shell command -v ninja >/dev/null 2>&1 && echo yes),yes)
CMAKE_GENERATOR ?= Ninja
else
CMAKE_GENERATOR ?= Unix Makefiles
endif

CMAKE_EXTRA_FLAGS ?=

ifeq ($(UNAME_S),Darwin)
  # Auto-detect Homebrew Qt 6 when CMAKE_PREFIX_PATH is not provided.
  ifndef CMAKE_PREFIX_PATH
    ifneq ($(wildcard /opt/homebrew/opt/qt),)
      CMAKE_PREFIX_PATH := /opt/homebrew/opt/qt
    else ifneq ($(wildcard /usr/local/opt/qt),)
      CMAKE_PREFIX_PATH := /usr/local/opt/qt
    endif
  endif
  ifneq ($(CMAKE_PREFIX_PATH),)
    CMAKE_PREFIX_FLAG := -DCMAKE_PREFIX_PATH=$(CMAKE_PREFIX_PATH)
  endif

  APP_DIR ?= /Applications
  # Put the CLI launcher in Homebrew's bin on Apple Silicon/Intel if writable,
  # otherwise ~/.local/bin (no sudo required).
  ifndef PREFIX
    ifeq ($(shell [ -w /opt/homebrew/bin ] && echo yes),yes)
      PREFIX := /opt/homebrew
    else ifeq ($(shell [ -w /usr/local/bin ] && echo yes),yes)
      PREFIX := /usr/local
    else
      PREFIX := $(HOME)/.local
    endif
  endif
  BINDIR ?= $(PREFIX)/bin
  RUN_BIN := $(BUILD_DIR)/Cutefin.app/Contents/MacOS/Cutefin
  CMAKE_PLATFORM_FLAGS := -DCUTEFIN_MACOS_APP_DIR=$(APP_DIR)
else
  PREFIX ?= /usr/local
  BINDIR ?= $(PREFIX)/bin
  RUN_BIN := $(BUILD_DIR)/cutefin
  CMAKE_PLATFORM_FLAGS :=
endif

.PHONY: all configure build install uninstall test run clean help

all: build

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	cmake -S . -B $(BUILD_DIR) -G "$(CMAKE_GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) \
		$(CMAKE_PREFIX_FLAG) \
		$(CMAKE_PLATFORM_FLAGS) \
		$(CMAKE_EXTRA_FLAGS)

configure:
	cmake -S . -B $(BUILD_DIR) -G "$(CMAKE_GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) \
		$(CMAKE_PREFIX_FLAG) \
		$(CMAKE_PLATFORM_FLAGS) \
		$(CMAKE_EXTRA_FLAGS)

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR)

install: build
ifeq ($(UNAME_S),Darwin)
	cmake --install $(BUILD_DIR) $(if $(DESTDIR),--prefix $(PREFIX))
	@mkdir -p "$(DESTDIR)$(BINDIR)"
	@ln -sf "$(APP_DIR)/Cutefin.app/Contents/MacOS/Cutefin" "$(DESTDIR)$(BINDIR)/cutefin"
	@echo "Installed app bundle: $(DESTDIR)$(APP_DIR)/Cutefin.app"
	@echo "Installed CLI symlink: $(DESTDIR)$(BINDIR)/cutefin"
else
	cmake --install $(BUILD_DIR) --prefix $(PREFIX)
	@echo "Installed binary: $(DESTDIR)$(BINDIR)/cutefin"
endif

uninstall:
ifeq ($(UNAME_S),Darwin)
	rm -rf "$(DESTDIR)$(APP_DIR)/Cutefin.app"
	rm -f "$(DESTDIR)$(BINDIR)/cutefin"
	@echo "Removed $(DESTDIR)$(APP_DIR)/Cutefin.app and $(DESTDIR)$(BINDIR)/cutefin"
else
	rm -f "$(DESTDIR)$(BINDIR)/cutefin"
	rm -f "$(DESTDIR)$(PREFIX)/share/applications/cutefin.desktop"
	rm -f "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/cutefin.svg"
	@echo "Removed cutefin from $(DESTDIR)$(PREFIX)"
endif

test: build
	ctest --test-dir $(BUILD_DIR) -LE live --output-on-failure

run: build
	$(RUN_BIN)

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Cutefin build targets:"
	@echo "  make              Build Cutefin (Release, auto-detects Ninja & Qt)"
	@echo "  make install      Build and install Cutefin on this machine"
	@echo "  make uninstall    Remove installed Cutefin files"
	@echo "  make test         Run the offline unit test suite"
	@echo "  make run          Build and launch Cutefin"
	@echo "  make clean        Remove $(BUILD_DIR)/"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX=$(PREFIX)  BINDIR=$(BINDIR)  BUILD_TYPE=$(BUILD_TYPE)"
ifeq ($(UNAME_S),Darwin)
	@echo "  APP_DIR=$(APP_DIR)"
endif
