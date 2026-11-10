# Cortana (W10M) demo app — uXAML-driven port for NTOS.
# Host build (NTOS_SH4_linux e2e): zig cc -> x86-64 PE DLLs.
# Device build: same sources through the SH4 payload flow (payload.ld + elf2ntx).
#
# Component reuse (the W10M shared-DLL model):
#   cortana.dll = app + orb VEC assets
#               + edge/app/uxaml.c        (the shared uXAML loader)
#               + files/app/files_icons.c (shared MDL2 glyph set)
#               + payload/phone/uixctl.c  (shared controls;  static)
#               + payload/phone/svf.c     (shared SVF engine; static)
#   fingerkb.dll = the shared keyboard DLL, loaded as a separate module
ZIG ?= /home/z/my-project/opt/zig/zig
PHONE = ../../payload/phone
EDGEAPP = ../../edge/app
FILESICONS = ../../files/app

PHONE_CFLAGS = -DNTOS_LINUX_BUILD -DNT_IAT_EXTERN_ONLY -DUXCTL_NO_FACTORY

all: cortana.dll fingerkb.dll cortana.uxaml

cortana.uxaml: $(EDGEAPP)/uxaml_gen.py markup/*.xml
	python3 $(EDGEAPP)/uxaml_gen.py build markup/ -o cortana.uxaml

cortana.dll: cortana_app.c cortana_vec.h \
	     $(EDGEAPP)/uxaml.c $(EDGEAPP)/uxaml.h $(EDGEAPP)/nt_app.h $(EDGEAPP)/hosted_shell.h $(EDGEAPP)/hosted_content.h \
	     $(FILESICONS)/files_icons.c $(FILESICONS)/files_icons.h \
	     $(PHONE)/uixctl.c $(PHONE)/svf.c $(PHONE)/phone.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	  -Wl,--image-base=0x180410000 -DNTOS_LINUX_BUILD \
	  -I. -I$(EDGEAPP) -I$(FILESICONS) -I$(PHONE) -I../../payload -o cortana.dll \
	  cortana_app.c $(FILESICONS)/files_icons.c $(EDGEAPP)/uxaml.c \
	  $(PHONE)/uixctl.c $(PHONE_CFLAGS) \
	  $(PHONE)/svf.c $(PHONE_CFLAGS)

# shared keyboard DLL (host build of the phone FINGERKB.DLL source)
fingerkb.dll: $(PHONE)/fingerkb.c $(PHONE)/phone.h $(PHONE)/system_icons.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180200000 -DNTOS_LINUX_BUILD \
	  -I$(PHONE) -I../../payload -o fingerkb.dll $(PHONE)/fingerkb.c

clean:
	rm -f cortana.dll fingerkb.dll cortana.uxaml cortana-*.png

# e2e: run under NTOS_SH4_linux with the shared keyboard module
test: all
	rm -rf tests/fs/fls0 && mkdir -p tests/fs/fls0
	cp cortana.uxaml tests/fs/fls0/cortana.uxaml
	$(MAKE) -C $(EDGEAPP) ZIG=$(ZIG) shellchrome.dll
	../../edge/ntos_linux/ntos_linux fingerkb.dll $(EDGEAPP)/shellchrome.dll cortana.dll \
	  --script tests/cortana.script --shot cortana --fs tests/fs
	python3 tests/check_cortana_shots.py cortana-*.png

.PHONY: all clean test
