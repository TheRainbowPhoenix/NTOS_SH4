# SystemSettings (W10M) POC app — uXAML-driven port for NTOS.
# Host build (NTOS_SH4_linux e2e): zig cc -> x86-64 PE DLLs.
# Device build: same sources through the SH4 payload flow (payload.ld + elf2ntx).
#
# Component reuse (the W10M shared-DLL model):
#   settings.dll = app + settings glyph set
#                + files/app/files_icons.c   (shared MDL2 glyph set)
#                + edge/app/uxaml.c          (the shared uXAML loader)
#                + payload/phone/uixctl.c    (shared controls;  static)
#                + payload/phone/svf.c       (shared SVF icons; static)
#   fingerkb.dll = the shared keyboard DLL, loaded as a separate module
ZIG ?= /home/z/my-project/opt/zig/zig
PHONE = ../../payload/phone
EDGEAPP = ../../edge/app
FILESICONS = ../../files/app

PHONE_CFLAGS = -DNTOS_LINUX_BUILD -DNT_IAT_EXTERN_ONLY -DUXCTL_NO_FACTORY

all: settings.dll fingerkb.dll settings.uxaml

settings.uxaml: $(EDGEAPP)/uxaml_gen.py markup/*.xml
	python3 $(EDGEAPP)/uxaml_gen.py build markup/ -o settings.uxaml

settings.dll: settings_app.c settings_icons.c settings_icons.h \
	      $(EDGEAPP)/uxaml.c $(EDGEAPP)/uxaml.h $(EDGEAPP)/nt_app.h \
	      $(FILESICONS)/files_icons.c $(FILESICONS)/files_icons.h \
	      $(PHONE)/uixctl.c $(PHONE)/svf.c $(PHONE)/phone.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	  -Wl,--image-base=0x180510000 -DNTOS_LINUX_BUILD \
	  -I. -I$(EDGEAPP) -I$(FILESICONS) -I$(PHONE) -I../../payload -o settings.dll \
	  settings_app.c settings_icons.c $(FILESICONS)/files_icons.c \
	  $(EDGEAPP)/uxaml.c \
	  $(PHONE)/uixctl.c $(PHONE_CFLAGS) \
	  $(PHONE)/svf.c $(PHONE_CFLAGS)

# shared keyboard DLL (host build of the phone FINGERKB.DLL source)
fingerkb.dll: $(PHONE)/fingerkb.c $(PHONE)/phone.h $(PHONE)/system_icons.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180200000 -DNTOS_LINUX_BUILD \
	  -I$(PHONE) -I../../payload -o fingerkb.dll $(PHONE)/fingerkb.c

clean:
	rm -f settings.dll fingerkb.dll settings.uxaml settings-*.png

# e2e: run under NTOS_SH4_linux with the shared keyboard + navbar modules
test: all
	rm -rf tests/fs/fls0 && mkdir -p tests/fs/fls0
	cp settings.uxaml tests/fs/fls0/settings.uxaml
	$(MAKE) -C $(EDGEAPP) ZIG=$(ZIG) fingerkb.dll shellchrome.dll
	cp $(EDGEAPP)/fingerkb.dll $(EDGEAPP)/shellchrome.dll . 2>/dev/null || true
	../../edge/ntos_linux/ntos_linux fingerkb.dll shellchrome.dll settings.dll \
	  --script tests/settings.script --shot settings --fs tests/fs
	python3 tests/check_settings_shots.py settings-*.png

.PHONY: all clean test
