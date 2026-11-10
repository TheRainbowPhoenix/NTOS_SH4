# NTOS_SH4_windows — Wine-like host env for NTOS payload PEs, native edition.
#
#   edge/ntos_win/ntos_windows.exe runs the SAME x86-64 PE payload DLLs the
#   Linux host and the real calculator run, on a Windows desktop: an
#   interactive LCD window with real mouse/keyboard input plus a session
#   recorder that emits ntos_linux e2e script lines. This is the fast
#   iteration/debug loop for uXAML markup and svf icon work.
#
# Shared sources with the Linux host (one source of truth):
#   ../ntos_linux/pe_loader.c   (map_image: VirtualAlloc on Win32)
#   ../ntos_linux/host_png.c    (dependency-free PNG writer)
#   ../ntos_linux/ntos_linux.h  (host API surface)
# Win32-only layers:
#   win_core.c   builtin COREDLL twin of host_core.c
#   win_host.c   device-LCD window + input + recorder
#   win_main.c   WinMain, module loading, script engine
#
# Targets:
#   all        build ntos_windows.exe
#   package    assemble the testable ntos-windows/ bundle + zip (needs the
#              edge app DLLs built first: `make -C ../app`)
#   test       build + package + (if wine is installed) a headless smoke run
ZIG ?= /home/z/my-project/opt/zig/zig
LINUX = ../ntos_linux
APP = ../app

ZIGFLAGS = -target x86_64-windows-gnu -O2 -g
LDFLAGS = -Wl,--subsystem,windows -lshell32 -lgdi32 -luser32

all: ntos_windows.exe

ntos_windows.exe: win_main.c win_core.c win_host.c win_host.h \
	$(LINUX)/pe_loader.c $(LINUX)/host_png.c $(LINUX)/ntos_linux.h
	$(ZIG) cc $(ZIGFLAGS) -I. -I$(LINUX) -o $@ \
	win_main.c win_core.c win_host.c \
	$(LINUX)/pe_loader.c $(LINUX)/host_png.c $(LDFLAGS)

# --- testable bundle: exe + shared DLLs + fake \fls0 tree + launchers -----
PKG = ntos-windows
PKG_ZIP = $(PKG).zip

package: all
	rm -rf $(PKG) $(PKG_ZIP)
	mkdir -p $(PKG)/fs/fls0
	cp ntos_windows.exe $(PKG)/
	cp $(APP)/html.dll $(APP)/fingerkb.dll $(APP)/shellchrome.dll \
	$(APP)/chakra.dll $(APP)/edge.dll $(PKG)/
	cp $(APP)/tests/assets/article.md $(APP)/tests/assets/logo.svf \
	$(APP)/tests/assets/demo.md $(APP)/tests/assets/cool.svf $(PKG)/fs/fls0/
	cp $(APP)/edge.uxaml $(APP)/edgelight.uxaml $(PKG)/fs/fls0/
	cp $(APP)/tests/assets/swap.html $(PKG)/fs/fls0/
	sed -e 's/#0000FF/#FFFFFF/g' -e 's/#FF0000/#0000FF/g' \
	    -e 's/#00CC00/#CC0000/g' -e 's/font-size: 22px/font-size: 16px/g' \
	    -e 's/Swap Test A/Swap Test B/g' \
	    $(APP)/tests/assets/swap.html > $(PKG)/fs/fls0/swap2.html
	{ printf '@echo off\r\n'; \
	printf 'cd /d %%~dp0\r\n'; \
	printf 'ntos_windows.exe html.dll fingerkb.dll shellchrome.dll chakra.dll edge.dll --fs fs --record session.script\r\n'; \
	printf 'echo session.script + ntos-win.log + shot-*.png record what you did\r\n'; } > $(PKG)/run-edge.bat
	cp README.txt $(PKG)/
	python3 mkzip.py $(PKG_ZIP) $(PKG)
	@echo "package: $(PKG_ZIP) ready"

# test: run the SAME winboot.script through BOTH hosts ($(PKG)/fs for both)
# and require every frame to be byte-identical (tests/wpar.py). The wine
# half needs wine64 + xvfb (+ libz-mingw-w64: wine's user32 imports
# zlib1.dll, which Debian does not ship — the workflow drops it into the
# prefix; see the wine_setup recipe below). Script mode never
# opens a window, so no X display is needed (wine nods it through).
WINEPREFIX_DIR ?= $(CURDIR)/.wineprefix

wine_setup:
	@if command -v wine64 >/dev/null 2>&1; then \
	  mkdir -p "$(WINEPREFIX_DIR)/drive_c/windows/system32"; \
	  for z in /usr/x86_64-w64-mingw32/lib/zlib1.dll /usr/lib/x86_64-w64-mingw32/lib/zlib1.dll \
	           /home/z/my-project/opt/wine-dl/root/usr/x86_64-w64-mingw32/lib/zlib1.dll; do \
	    if [ -f "$$z" ]; then cp "$$z" "$(WINEPREFIX_DIR)/drive_c/windows/system32/"; break; fi; \
	  done; \
	  env WINEPREFIX="$(WINEPREFIX_DIR)" wine64 wineboot -i >/dev/null 2>&1 || true; \
	fi

test: package wine_setup
	rm -f lshot-*.png wshot-*.png
	$(LINUX)/ntos_linux $(PKG)/html.dll $(PKG)/fingerkb.dll $(PKG)/shellchrome.dll \
	  $(PKG)/chakra.dll $(PKG)/edge.dll --fs $(PKG)/fs \
	  --script tests/winboot.script --shot lshot
	@if command -v wine64 >/dev/null 2>&1; then \
	  env WINEPREFIX="$(WINEPREFIX_DIR)" wine64 $(CURDIR)/$(PKG)/ntos_windows.exe \
	    $(CURDIR)/$(PKG)/html.dll $(CURDIR)/$(PKG)/fingerkb.dll $(CURDIR)/$(PKG)/shellchrome.dll \
	    $(CURDIR)/$(PKG)/chakra.dll $(CURDIR)/$(PKG)/edge.dll --fs $(CURDIR)/$(PKG)/fs \
	    --script tests/winboot.script --shot wshot || exit 1; \
	  python3 tests/wpar.py lshot wshot; \
	else \
	  echo "wine64 not installed locally - only the Linux half ran (forgejo CI runs both)"; \
	fi

clean:
	rm -f ntos_windows.exe lshot-*.png wshot-*.png
	rm -rf $(PKG) $(PKG_ZIP) .wineprefix

.PHONY: all clean package test wine_setup
