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

# --- other apps: the exe is app-agnostic, assemble per-app bundles -----
# Prerequisites: the payload DLLs must be built first, i.e.
#   make -C ../app ... (edge set)  +  make -C ../../<app>/app
# The launchers mirror each app's e2e module list so a native session
# behaves exactly like the CI run (files keeps the full edge set for the
# file:// hand-off).

define STAGE
rm -rf $(1) $(1).zip
mkdir -p $(1)/fs/fls0
cp ntos_windows.exe README.txt $(1)/
endef

BAT = @printf '@echo off\r\ncd /d %%~dp0\r\nntos_windows.exe $(1) --fs fs --record session.script\r\necho session.script + ntos-win.log + shot-*.png record what you did\r\n' > $(2)

package-files: all
	$(call STAGE,ntos-windows-files)
	cp $(APP)/html.dll $(APP)/fingerkb.dll $(APP)/shellchrome.dll 	   $(APP)/chakra.dll $(APP)/edge.dll ../../files/app/files.dll ntos-windows-files/
	cp $(APP)/edge.uxaml $(APP)/edgelight.uxaml ../../files/app/files.uxaml ntos-windows-files/fs/fls0/
	cp $(APP)/tests/assets/article.md $(APP)/tests/assets/logo.svf 	   $(APP)/tests/assets/demo.md $(APP)/tests/assets/cool.svf ntos-windows-files/fs/fls0/
	python3 ../../files/app/tests/make_tree.py ntos-windows-files/fs/fls0
	$(call BAT,html.dll fingerkb.dll shellchrome.dll chakra.dll edge.dll files.dll,ntos-windows-files/run-files.bat)
	python3 mkzip.py ntos-windows-files.zip ntos-windows-files
	@echo "package: ntos-windows-files.zip ready"

package-cortana: all
	$(call STAGE,ntos-windows-cortana)
	cp ../../cortana/app/fingerkb.dll $(APP)/shellchrome.dll 	   ../../cortana/app/cortana.dll ntos-windows-cortana/
	cp ../../cortana/app/cortana.uxaml ntos-windows-cortana/fs/fls0/
	$(call BAT,fingerkb.dll shellchrome.dll cortana.dll,ntos-windows-cortana/run-cortana.bat)
	python3 mkzip.py ntos-windows-cortana.zip ntos-windows-cortana
	@echo "package: ntos-windows-cortana.zip ready"

package-calc: all
	$(call STAGE,ntos-windows-calc)
	cp ../../calc/app/fingerkb.dll $(APP)/shellchrome.dll 	   ../../calc/app/calc.dll ntos-windows-calc/
	cp ../../calc/app/calc.uxaml ntos-windows-calc/fs/fls0/
	$(call BAT,fingerkb.dll shellchrome.dll calc.dll,ntos-windows-calc/run-calc.bat)
	python3 mkzip.py ntos-windows-calc.zip ntos-windows-calc
	@echo "package: ntos-windows-calc.zip ready"

package-all: package package-files package-cortana package-calc

# rename the original edge package for the composite target

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
