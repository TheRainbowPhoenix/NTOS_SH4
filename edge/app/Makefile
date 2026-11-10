# Edge POC app — uXAML-driven Edge mobile chrome for NTOS.
# Host build (NTOS_SH4_linux e2e): zig cc -> x86-64 PE DLLs.
# Device build: same sources through the SH4 payload flow (payload.ld + elf2ntx).
#
# Component reuse (the W10M shared-DLL model):
#   edge.dll    = app + markup engine + icons
#               + payload/phone/uixctl.c  (shared controls;  static)
#               + payload/phone/svf.c     (shared SVF icons; static)
#   fingerkb.dll= the shared keyboard DLL, loaded as a separate module;
#                 the host loader cross-snaps the IAT slots (both ways)
ZIG ?= /home/z/my-project/opt/zig/zig
MQJS ?= /home/z/my-project/opt/src/mquickjs
PHONE = ../../payload/phone
IMPORTS = ../../payload

# phone-framework sources compiled into edge.dll need their IAT slots
# declared extern-only (edge_app.c owns the definitions) and must not
# bring their own DLL entry point or factory.
PHONE_CFLAGS = -DNTOS_LINUX_BUILD -DNT_IAT_EXTERN_ONLY -DUXCTL_NO_FACTORY

all: edge.dll html.dll chakra.dll fingerkb.dll shellchrome.dll edge.uxaml

edge.uxaml: uxaml_gen.py markup/*.xml
	python3 uxaml_gen.py build markup/ -o edge.uxaml
	python3 uxaml_gen.py build markup/ -o edgelight.uxaml --light

edge.dll: edge_app.c icons.c icons.h uxaml.c uxaml.h nt_app.h hosted_shell.h mockweb.h \
          $(PHONE)/uixctl.c $(PHONE)/svf.c $(PHONE)/phone.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	  -Wl,--image-base=0x180010000 -DNTOS_LINUX_BUILD \
	  -I. -I../../payload/phone -I../../payload -o edge.dll \
	  edge_app.c icons.c uxaml.c \
	  $(PHONE)/uixctl.c $(PHONE_CFLAGS) \
	  $(PHONE)/svf.c $(PHONE_CFLAGS)

# shared web-document engine (HTML.DLL, built from payload/phone/markdown.c):
# the real html4/css1/markdown renderer — box-model layout, floats, forms,
# find-in-page, reader mode. Like chakra.dll it is a plain loadable
# library: fill an MdHost, call md_load/md_layout/md_paint. Any app or
# test harness can host it; nothing here is Edge-specific.
html.dll: $(PHONE)/markdown.c $(PHONE)/markdown.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180400000 -DNTOS_LINUX_BUILD \
	  -I$(PHONE) -I$(IMPORTS) -o html.dll $(PHONE)/markdown.c

# shared keyboard DLL (host build of the phone FINGERKB.DLL source)
fingerkb.dll: $(PHONE)/fingerkb.c $(PHONE)/phone.h $(PHONE)/system_icons.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180200000 -DNTOS_LINUX_BUILD \
	  -I$(PHONE) -I$(IMPORTS) -o fingerkb.dll $(PHONE)/fingerkb.c

# shared shell-chrome DLL (host build of the phone SHELLCHROME.DLL source).
# Provides the system nav bar (Back/Home/Search) through UIXChromeFactory;
# edge.dll imports it like fingerkb instead of re-drawing the bar.
shellchrome.dll: $(PHONE)/shellchrome.c $(PHONE)/phone.h $(PHONE)/system_icons.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180300000 -DNTOS_LINUX_BUILD \
	  -I$(PHONE) -I$(IMPORTS) -o shellchrome.dll $(PHONE)/shellchrome.c

# chakra.dll — DEFAULT: pure API-shape stub (160 exports, no engine).
# The device has no FPU and the POC targets the UI; the real 5.8MB ChakraCore
# is out of reach. Regenerate with: python3 ../tools/gen_chakra_stub.py
chakra.dll: chakra_stub.c nt_app.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180100000 -DNTOS_LINUX_BUILD \
	  -I. -o chakra.dll chakra_stub.c

# ---- optional: behavioural engine over mquickjs (host experiments only) ----
MQJS_SRC := $(wildcard $(MQJS)/mquickjs.c)
ifneq ($(MQJS_SRC),)

mqjs_edge_stdlib.h: edge_stdlib_host.c $(MQJS)/mqjs_stdlib.c
	gcc -O1 -I$(MQJS) -o /tmp/edge_stdlib_gen $(MQJS)/mqjs_stdlib.c $(MQJS)/mquickjs_build.c
	/tmp/edge_stdlib_gen -m64 > $@

chakra-full.dll: chakra_mqjs.c shim/shim.c shim/*.h nt_app.h mqjs_edge_stdlib.h $(MQJS)/mquickjs.c
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180020000 -DNTOS_LINUX_BUILD \
	  -I. -Ishim -I$(MQJS) -o chakra-full.dll chakra_mqjs.c shim/shim.c shim/setjmp.S \
	  $(MQJS)/mquickjs.c $(MQJS)/dtoa.c $(MQJS)/cutils.c $(MQJS)/libm.c

endif

clean:
	rm -f edge.dll html.dll chakra.dll chakra-full.dll fingerkb.dll shellchrome.dll edge.uxaml edge-*.png

# e2e: run under NTOS_SH4_linux with the shared keyboard module; markup
# goes into the fake device root
test: all
	rm -rf tests/fs/fls0 && mkdir -p tests/fs/fls0
	cp edge.uxaml tests/fs/fls0/edge.uxaml
	cp edgelight.uxaml tests/fs/fls0/edgelight.uxaml
	cp tests/assets/article.md tests/assets/logo.svf tests/fs/fls0/
	cp tests/assets/demo.md tests/assets/cool.svf tests/fs/fls0/
	cp tests/assets/swap.html tests/fs/fls0/swap.html
	# swap2.html: the SAME page with colour/size values swapped — the
	# render-from-content gate (see check_edge_shots.py 'swap-b')
	sed -e 's/#0000FF/#FFFFFF/g' -e 's/#FF0000/#0000FF/g' \
	    -e 's/#00CC00/#CC0000/g' -e 's/font-size: 22px/font-size: 16px/g' \
	    -e 's/Swap Test A/Swap Test B/g' \
	    tests/assets/swap.html > tests/fs/fls0/swap2.html
	../ntos_linux/ntos_linux html.dll fingerkb.dll shellchrome.dll chakra.dll edge.dll --script tests/edge.script \
	  --shot edge --fs tests/fs
	python3 tests/check_edge_shots.py edge-*.png
	../ntos_linux/ntos_linux chakra.dll --entry ChakraSmokeTest

.PHONY: all clean test
