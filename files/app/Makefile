# File Explorer (W10M) POC app — uXAML-driven FileExplorer port for NTOS.
# Host build (NTOS_SH4_linux e2e): zig cc -> x86-64 PE DLLs.
# Device build: same sources through the SH4 payload flow (payload.ld + elf2ntx).
#
# Component reuse (the W10M shared-DLL model):
#   files.dll    = app + files icons
#                + edge/app/uxaml.c        (the shared uXAML loader)
#                + payload/phone/uixctl.c  (shared controls;  static)
#                + payload/phone/svf.c     (shared SVF icons; static)
#   fingerkb.dll = the shared keyboard DLL, loaded as a separate module;
#                  the host loader cross-snaps the IAT slots (both ways)
ZIG ?= /home/z/my-project/opt/zig/zig
PHONE = ../../payload/phone
EDGEAPP = ../../edge/app

# phone-framework sources compiled into files.dll need their IAT slots
# declared extern-only (files_app.c owns the definitions) and must not
# bring their own DLL entry point or factory.
PHONE_CFLAGS = -DNTOS_LINUX_BUILD -DNT_IAT_EXTERN_ONLY -DUXCTL_NO_FACTORY

all: files.dll fingerkb.dll files.uxaml

files.uxaml: $(EDGEAPP)/uxaml_gen.py markup/*.xml
	python3 $(EDGEAPP)/uxaml_gen.py build markup/ -o files.uxaml

files.dll: files_app.c files_icons.c files_icons.h \
	   $(EDGEAPP)/uxaml.c $(EDGEAPP)/uxaml.h $(EDGEAPP)/nt_app.h $(EDGEAPP)/hosted_shell.h \
	   $(PHONE)/uixctl.c $(PHONE)/svf.c $(PHONE)/phone.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	  -Wl,--image-base=0x180310000 -DNTOS_LINUX_BUILD \
	  -I. -I$(EDGEAPP) -I$(PHONE) -I../../payload -o files.dll \
	  files_app.c files_icons.c $(EDGEAPP)/uxaml.c \
	  $(PHONE)/uixctl.c $(PHONE_CFLAGS) \
	  $(PHONE)/svf.c $(PHONE_CFLAGS)

# shared keyboard DLL (host build of the phone FINGERKB.DLL source)
fingerkb.dll: $(PHONE)/fingerkb.c $(PHONE)/phone.h $(PHONE)/system_icons.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180200000 -DNTOS_LINUX_BUILD \
	  -I$(PHONE) -I../../payload -o fingerkb.dll $(PHONE)/fingerkb.c

clean:
	rm -f files.dll fingerkb.dll files.uxaml files-*.png

# e2e: run under NTOS_SH4_linux with the shared keyboard module + the Edge
# app modules (the file:// open-in-Edge test hands the foreground over to
# edge.dll in-process); markup and the sample tree go into the fake device root
test: all
	rm -rf tests/fs/fls0 && mkdir -p tests/fs/fls0
	cp files.uxaml tests/fs/fls0/files.uxaml
	python3 tests/make_tree.py tests/fs/fls0
	$(MAKE) -C $(EDGEAPP) ZIG=$(ZIG) edge.dll html.dll chakra.dll fingerkb.dll shellchrome.dll edge.uxaml
	cp $(EDGEAPP)/edge.uxaml tests/fs/fls0/edge.uxaml
	../../edge/ntos_linux/ntos_linux $(EDGEAPP)/html.dll $(EDGEAPP)/fingerkb.dll $(EDGEAPP)/shellchrome.dll \
	  $(EDGEAPP)/chakra.dll $(EDGEAPP)/edge.dll files.dll \
	  --script tests/files.script --shot files --fs tests/fs 2>&1 | tee tests/e2e.log
	python3 tests/check_files_shots.py files-*.png
	python3 tests/verify_fs.py tests/fs/fls0
	grep -q 'launch: foreground -> .*edge.dll (cmdline "file://' tests/e2e.log \
	  || (echo 'edge was NOT launched with the file:// argument'; exit 1)

.PHONY: all clean test
