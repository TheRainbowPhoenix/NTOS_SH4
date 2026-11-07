SOURCEDIR = src
BUILDDIR = obj
OUTDIR = dist
DEPDIR = .deps

AS:=sh4a_nofpueb-elf-gcc
AS_FLAGS:=-gdwarf-5

SDK_DIR?=/sdk

DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
WARNINGS=-Wall -Wextra -pedantic -Werror -pedantic-errors
INCLUDES=-I$(SDK_DIR)/include #-I$(SOURCEDIR)
DEFINES=
FUNCTION_FLAGS=-flto=auto -ffat-lto-objects -fno-builtin -ffunction-sections -fdata-sections -gdwarf-5 -O2
COMMON_FLAGS=$(FUNCTION_FLAGS) $(INCLUDES) $(WARNINGS) $(DEFINES)

CC:=sh4a_nofpueb-elf-gcc
CC_FLAGS=-std=c23 $(COMMON_FLAGS)

CXX:=sh4a_nofpueb-elf-g++
CXX_FLAGS=-std=c++20 $(COMMON_FLAGS)

LD:=sh4a_nofpueb-elf-g++
LD_FLAGS:=$(FUNCTION_FLAGS) -Wl,--gc-sections
LIBS:=-L$(SDK_DIR) -lsdk

READELF:=sh4a_nofpueb-elf-readelf
OBJCOPY:=sh4a_nofpueb-elf-objcopy
STRIP:=sh4a_nofpueb-elf-strip
PYTHON := $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null)

APP_ELF := $(OUTDIR)/NTOS.elf
APP_HH3 := $(APP_ELF:.elf=.hh3)

# --- payloads (standard PE EXEs/DLLs, machine 0x01A8 SH4 BE) ----------------
#   HELLO.EXE      self-contained hello (copy-load fallback or UTLB-mapped)
#   COREDLL.DLL    stub COREDLL module, /FIXED at 0x00800000, PE exports
#   HELLOCORE.EXE  EXE importing from COREDLL (exercises UTLB + import snap)

PAYLOAD_DIR := payload
#   HELLO.EXE      self-contained hello (copy-load fallback or UTLB-mapped)
#   COREDLL.DLL    WinCE-style COREDLL: ~1900 exports (coredll.def), a few
#                  really implemented, the rest routed to coredll_stub
#   HELLOCORE.EXE  EXE importing from COREDLL (UTLB + import snap demo)
#   MEMTOOL.EXE    SH4 BE recreation of NonCASRom/WinCE/MEMTOOL.EXE
#                  (NKDbgPrintfW / SetKMode / SetProcPermissions / memory)
PAYLOAD_CFLAGS := -ffreestanding -fno-builtin -fno-stack-protector 	-fno-unwind-tables -fno-asynchronous-unwind-tables 	-O2 -Wall -Wextra $(INCLUDES)

HELLO_OBJ := $(BUILDDIR)/payload/hello.o
HELLO_ELF := $(BUILDDIR)/payload/hello.elf
HELLO_EXE := $(OUTDIR)/HELLO.EXE

COREDLL_OBJ := $(BUILDDIR)/payload/coredll.o
COREDLL_ELF := $(BUILDDIR)/payload/coredll.elf
COREDLL_DLL := $(OUTDIR)/COREDLL.DLL

HELLOCORE_OBJ := $(BUILDDIR)/payload/hello_core.o
HELLOCORE_ELF := $(BUILDDIR)/payload/hello_core.elf
HELLOCORE_EXE := $(OUTDIR)/HELLOCORE.EXE

MEMTOOL_OBJ := $(BUILDDIR)/payload/memtool.o
MEMTOOL_ELF := $(BUILDDIR)/payload/memtool.elf
MEMTOOL_EXE := $(OUTDIR)/MEMTOOL.EXE
PAINT_OBJ := $(BUILDDIR)/payload/paintdemo.o
PAINT_ELF := $(BUILDDIR)/payload/paintdemo.elf
PAINT_EXE := $(OUTDIR)/PAINT.EXE
WINLAB_OBJ := $(BUILDDIR)/payload/winlab.o
WINLAB_ELF := $(BUILDDIR)/payload/winlab.elf
WINLAB_EXE := $(OUTDIR)/WINLAB.EXE

AS_SOURCES:=$(shell find $(SOURCEDIR) -name '*.S')
CC_SOURCES:=$(shell find $(SOURCEDIR) -name '*.c')
CXX_SOURCES:=$(shell find $(SOURCEDIR) -name '*.cpp')
OBJECTS := $(addprefix $(BUILDDIR)/,$(AS_SOURCES:.S=.o)) 	$(addprefix $(BUILDDIR)/,$(CC_SOURCES:.c=.o)) 	$(addprefix $(BUILDDIR)/,$(CXX_SOURCES:.cpp=.o))

NOLTOOBJS := $(foreach obj, $(OBJECTS), $(if $(findstring /nolto/, $(obj)), $(obj)))

DEPFILES := $(OBJECTS:$(BUILDDIR)/%.o=$(DEPDIR)/%.d)

hh3: $(APP_HH3) Makefile
elf: $(APP_ELF) Makefile
ntx: $(HELLO_EXE) $(COREDLL_DLL) $(HELLOCORE_EXE) $(MEMTOOL_EXE) $(PAINT_EXE) $(WINLAB_EXE) Makefile

all: elf hh3 ntx
.DEFAULT_GOAL := all
.SECONDARY: # Prevents intermediate files from being deleted

.NOTPARALLEL: clean
clean:
	rm -rf $(BUILDDIR) $(OUTDIR) $(DEPDIR)

%.hh3: %.elf
	$(STRIP) -o $@ $^

# Payloads: freestanding compile, static link at their preferred base
# (+0x200 for the PE header page -- link VA == runtime VA, see elf2ntx.py),
# then the ELF->PE packer emits the standard image.
$(HELLO_OBJ): $(PAYLOAD_DIR)/hello.c payload/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/hello.c -o $@ $(PAYLOAD_CFLAGS)

$(HELLO_ELF): $(HELLO_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(HELLO_OBJ)

$(HELLO_EXE): $(HELLO_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(HELLO_ELF) -o $@

$(COREDLL_OBJ): $(PAYLOAD_DIR)/coredll.c
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/coredll.c -o $@ $(PAYLOAD_CFLAGS)

$(COREDLL_ELF): $(COREDLL_OBJ) $(PAYLOAD_DIR)/coredll.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_DIR)/coredll.ld -o $@ $(COREDLL_OBJ)

$(COREDLL_DLL): $(COREDLL_ELF) tools/elf2ntx.py tools/coredll_exports.txt
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(COREDLL_ELF) -o $@ --base 0x00800000 --dll 		--exports coredll_puts,coredll_getkey,coredll_refresh,coredll_malloc,coredll_free 		--exports-file tools/coredll_exports.txt --stub-symbol coredll_stub 		--dll-name COREDLL.DLL

$(HELLOCORE_OBJ): $(PAYLOAD_DIR)/hello_core.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/hello_core.c -o $@ $(PAYLOAD_CFLAGS)

$(HELLOCORE_ELF): $(HELLOCORE_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(HELLOCORE_OBJ)

$(HELLOCORE_EXE): $(HELLOCORE_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(HELLOCORE_ELF) -o $@ --import-module COREDLL.DLL

$(MEMTOOL_OBJ): $(PAYLOAD_DIR)/memtool.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/memtool.c -o $@ $(PAYLOAD_CFLAGS)

$(MEMTOOL_ELF): $(MEMTOOL_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(MEMTOOL_OBJ)

$(MEMTOOL_EXE): $(MEMTOOL_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(MEMTOOL_ELF) -o $@ --import-module COREDLL.DLL

$(PAINT_OBJ): $(PAYLOAD_DIR)/paintdemo.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(PAINT_ELF): $(PAINT_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(PAINT_OBJ)

$(PAINT_EXE): $(PAINT_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(PAINT_ELF) -o $@ --import-module COREDLL.DLL

$(WINLAB_OBJ): $(PAYLOAD_DIR)/winlab.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(WINLAB_ELF): $(WINLAB_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(WINLAB_OBJ)

$(WINLAB_EXE): $(WINLAB_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(WINLAB_ELF) -o $@ --import-module COREDLL.DLL

$(APP_ELF): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) -Wl,-Map $@.map -o $@ $(LD_FLAGS) $^ $(LIBS)

$(NOLTOOBJS): FUNCTION_FLAGS+=-fno-lto

$(BUILDDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) -c $< -o $@ $(AS_FLAGS)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPDIR)/$<)
	+$(CC) -c $< -o $@ $(CC_FLAGS) $(DEPFLAGS)

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPDIR)/$<)
	+$(CXX) -c $< -o $@ $(CXX_FLAGS) $(DEPFLAGS)

compile_commands.json:
	$(MAKE) $(MAKEFLAGS) clean
	bear -- sh -c "$(MAKE) $(MAKEFLAGS) --keep-going all || exit 0"

.PHONY: elf hh3 ntx all clean compile_commands.json

-include $(DEPFILES)
