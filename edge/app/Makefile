# Edge POC app — uXAML-driven Edge mobile chrome for NTOS.
# Host build (NTOS_SH4_linux e2e): zig cc -> x86-64 PE DLL.
# Device build: same sources through the SH4 payload flow (payload.ld + elf2ntx).
ZIG ?= /home/z/my-project/opt/zig/zig

all: edge.dll edge.uxaml

edge.uxaml: uxaml_gen.py markup/*.xml
	python3 uxaml_gen.py build markup/ -o edge.uxaml

edge.dll: edge_app.c uxaml.c uxaml.h nt_app.h
	$(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	  -Wl,--image-base=0x180010000 -DNTOS_LINUX_BUILD \
	  -I. -I../../payload/phone -o edge.dll edge_app.c uxaml.c

clean:
	rm -f edge.dll edge.uxaml edge-*.png

# e2e: run under NTOS_SH4_linux; markup goes into the fake device root
test: all
	rm -rf tests/fs/fls0 && mkdir -p tests/fs/fls0
	cp edge.uxaml tests/fs/fls0/edge.uxaml
	../ntos_linux/ntos_linux edge.dll --script tests/edge.script --shot edge \
	  --fs tests/fs
	python3 ../ntos_linux/tests/check_shots.py edge-*.png

.PHONY: all clean test
