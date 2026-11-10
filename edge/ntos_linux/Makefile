# NTOS_SH4_linux — Wine-like host env for NTOS payload PEs (see WORKLOG.md)
# Host runner builds with plain gcc; payload DLLs build with zig (x86-64 PE).
ZIG ?= /home/z/my-project/opt/zig/zig
CC ?= gcc
CFLAGS = -O2 -g -Wall -Wextra -std=c11

all: ntos_linux sample.dll

ntos_linux: main.c pe_loader.c host_core.c host_png.c ntos_linux.h
	    $(CC) $(CFLAGS) -o $@ main.c pe_loader.c host_core.c host_png.c

sample.dll: sample_app.c
	    $(ZIG) cc -target x86_64-windows-gnu -shared -O2 -nostdlib \
	      -Wl,--image-base=0x180000000 -o $@ sample_app.c \
	      -DNTOS_LINUX_BUILD

clean:
	    rm -f ntos_linux sample.dll *.o lcd-*.png

test: all
	    ./ntos_linux sample.dll --script tests/sample.script --shot test --fs tests/fs
	    @python3 tests/check_shots.py test-final.png

.PHONY: all clean test
