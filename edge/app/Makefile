# Edge POC app — uXAML-driven Edge mobile chrome for NTOS.
# Host build (NTOS_SH4_linux e2e): zig cc -> x86-64 PE DLL.
# Device build: same sources through the SH4 payload flow (payload.ld + elf2ntx).
ZIG ?= /home/z/my-project/opt/zig/zig
MQJS ?= /home/z/my-project/opt/src/mquickjs

all: edge.dll chakra.dll edge.uxaml

edge.uxaml: uxaml_gen.py markup/*.xml
	python3 uxaml_gen.py build markup/ -o edge.uxaml

edge.dll: edge_app.c icons.c uxaml.c uxaml.h nt_app.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	  -Wl,--image-base=0x180010000 -DNTOS_LINUX_BUILD \
	  -I. -I../../payload/phone -o edge.dll edge_app.c icons.c uxaml.c

# chakra.dll — DEFAULT: pure API-shape stub (160 exports, no engine).
# The device has no FPU and the POC targets the UI; the real 5.8MB ChakraCore
# is out of reach. Regenerate with: python3 ../tools/gen_chakra_stub.py
chakra.dll: chakra_stub.c nt_app.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -Os -nostdlib \
	  -Wl,--image-base=0x180020000 -DNTOS_LINUX_BUILD \
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
	rm -f edge.dll chakra.dll chakra-full.dll edge.uxaml edge-*.png

# e2e: run under NTOS_SH4_linux; markup goes into the fake device root
test: all
	rm -rf tests/fs/fls0 && mkdir -p tests/fs/fls0
	cp edge.uxaml tests/fs/fls0/edge.uxaml
	../ntos_linux/ntos_linux edge.dll --script tests/edge.script --shot edge \
	  --fs tests/fs
	python3 ../ntos_linux/tests/check_shots.py edge-*.png
	../ntos_linux/ntos_linux chakra.dll --entry ChakraSmokeTest

.PHONY: all clean test
