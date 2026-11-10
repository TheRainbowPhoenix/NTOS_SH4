# WindowsCalculator (W10M) POC app — uXAML-driven port for NTOS.
# Host build (NTOS_SH4_linux e2e): zig cc -> x86-64 PE DLLs.
# Device build: same sources through the SH4 payload flow.
#
# Component reuse (the W10M shared-DLL model):
#   calc.dll = app + calc operator glyphs (calc_icons.c)
#            + files/app/files_icons.c   (shared MDL2 glyph set)
#            + edge/app/uxaml.c          (the shared uXAML loader)
#            + payload/phone/uixctl.c    (shared controls; static)
#            + payload/phone/svf.c       (shared SVF icons; static)
#   fingerkb.dll = shared keyboard DLL (kept for parity with the other apps)
#   shellchrome.dll = the bottom nav strip (linked from edge/app)
ZIG ?= /home/z/my-project/opt/zig/zig
PHONE = ../../payload/phone
EDGEAPP = ../../edge/app
FILESICONS = ../../files/app

PHONE_CFLAGS = -DNTOS_LINUX_BUILD -DNT_IAT_EXTERN_ONLY -DUXCTL_NO_FACTORY

all: calc.dll fingerkb.dll calc.uxaml

calc.uxaml: $(EDGEAPP)/uxaml_gen.py markup/*.xml
	python3 $(EDGEAPP)/uxaml_gen.py build markup/ -o calc.uxaml

# (uixctl/svf not linked: every calculator surface is painted by the app
#  + calc_icons/files_icons vector glyphs; uxaml.c is a pure parser)
calc.dll: calc_app.c calc_icons.c calc_icons.h \
	  $(EDGEAPP)/uxaml.c $(EDGEAPP)/uxaml.h $(EDGEAPP)/nt_app.h \
	  $(FILESICONS)/files_icons.c $(FILESICONS)/files_icons.h \
	  $(PHONE)/font_bitmap.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	  -Wl,--image-base=0x180510000 -DNTOS_LINUX_BUILD \
	  -I. -I$(EDGEAPP) -I$(FILESICONS) -I$(PHONE) -I../../payload -o calc.dll \
	  calc_app.c calc_icons.c $(FILESICONS)/files_icons.c \
	  $(EDGEAPP)/uxaml.c

# shared keyboard DLL (host build of the phone FINGERKB.DLL source)
fingerkb.dll: $(PHONE)/fingerkb.c $(PHONE)/phone.h $(PHONE)/system_icons.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180200000 -DNTOS_LINUX_BUILD \
	  -I$(PHONE) -I../../payload -o fingerkb.dll $(PHONE)/fingerkb.c

clean:
	rm -f calc.dll fingerkb.dll calc.uxaml calc-*.png
	rm -rf tests/build tests/fs

# e2e: run under NTOS_SH4_linux with the shared keyboard + navbar modules
test: all
	rm -rf tests/fs/fls0 && mkdir -p tests/fs/fls0
	cp calc.uxaml tests/fs/fls0/calc.uxaml
	$(MAKE) -C $(EDGEAPP) ZIG=$(ZIG) fingerkb.dll shellchrome.dll
	mkdir -p tests/build
	cp $(EDGEAPP)/fingerkb.dll $(EDGEAPP)/shellchrome.dll tests/build/
	cp fingerkb.dll calc.dll tests/build/
	../../edge/ntos_linux/ntos_linux tests/build/fingerkb.dll tests/build/shellchrome.dll tests/build/calc.dll \
	  --script tests/calc.script --shot calc --fs tests/fs
	python3 tests/check_calc_shots.py calc-*.png

.PHONY: all clean test
