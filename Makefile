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
#   COREDLL.DLL    WinCE-style shared preferred base 0x10000000; relocatable
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
XAMLUI_OBJ := $(BUILDDIR)/payload/xamlui.o
XAMLUI_ELF := $(BUILDDIR)/payload/xamlui.elf
XAMLUI_DLL := $(OUTDIR)/XAMLUI.DLL

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
GUIWIN_OBJ := $(BUILDDIR)/payload/guiwin.o
GUIWIN_ELF := $(BUILDDIR)/payload/guiwin.elf
GUIWIN_EXE := $(OUTDIR)/GUIWIN.EXE
METROWIN_OBJ := $(BUILDDIR)/payload/metrowin.o
METROWIN_ELF := $(BUILDDIR)/payload/metrowin.elf
METROWIN_EXE := $(OUTDIR)/METROWIN.EXE
CALC_OBJ := $(BUILDDIR)/payload/calc.o
CALC_ELF := $(BUILDDIR)/payload/calc.elf
CALC_EXE := $(OUTDIR)/CALC.EXE
CHAIN_OBJ := $(BUILDDIR)/payload/chain.o
CHAIN_ELF := $(BUILDDIR)/payload/chain.elf
CHAIN_DLL := $(OUTDIR)/CHAIN.DLL
CHAINTEST_OBJ := $(BUILDDIR)/payload/chain_test.o
CHAINTEST_ELF := $(BUILDDIR)/payload/chain_test.elf
CHAINTEST_EXE := $(OUTDIR)/CHAINTEST.EXE
# 12 PE DLLs at identical preferred bases; ST01 fans out to four DLLs.
STRESS_NAMES := 01 02 03 04 05 06 07 08 09 10 11 12
STRESS_DLLS := $(addprefix $(OUTDIR)/ST,$(addsuffix .DLL,$(STRESS_NAMES)))
STRESS_OBJ := $(BUILDDIR)/payload/stress_exec.o
STRESS_ELF := $(BUILDDIR)/payload/stress_exec.elf
STRESS_EXE := $(OUTDIR)/STRESSEXEC.EXE
STRESS_EXTRA_01 := --extra-import-prefix stress_d02:ST02.DLL --extra-import-prefix stress_d03:ST03.DLL --extra-import-prefix stress_d04:ST04.DLL --extra-import-prefix stress_d05:ST05.DLL
STRESS_EXTRA_02 := --extra-import-prefix stress_d06:ST06.DLL --extra-import-prefix stress_d07:ST07.DLL
STRESS_EXTRA_03 := --extra-import-prefix stress_d08:ST08.DLL --extra-import-prefix stress_d09:ST09.DLL
STRESS_EXTRA_04 := --extra-import-prefix stress_d10:ST10.DLL --extra-import-prefix stress_d11:ST11.DLL
STRESS_EXTRA_05 := --extra-import-prefix stress_d12:ST12.DLL


AS_SOURCES:=$(shell find $(SOURCEDIR) -name '*.S')
CC_SOURCES:=$(shell find $(SOURCEDIR) -name '*.c')
CXX_SOURCES:=$(shell find $(SOURCEDIR) -name '*.cpp')
OBJECTS := $(addprefix $(BUILDDIR)/,$(AS_SOURCES:.S=.o)) 	$(addprefix $(BUILDDIR)/,$(CC_SOURCES:.c=.o)) 	$(addprefix $(BUILDDIR)/,$(CXX_SOURCES:.cpp=.o))

NOLTOOBJS := $(foreach obj, $(OBJECTS), $(if $(findstring /nolto/, $(obj)), $(obj)))

DEPFILES := $(OBJECTS:$(BUILDDIR)/%.o=$(DEPDIR)/%.d)

hh3: $(APP_HH3) Makefile
elf: $(APP_ELF) Makefile
ntx: $(HELLO_EXE) $(COREDLL_DLL) $(XAMLUI_DLL) $(HELLOCORE_EXE) $(MEMTOOL_EXE) $(PAINT_EXE) $(WINLAB_EXE) $(GUIWIN_EXE) $(METROWIN_EXE) $(CALC_EXE) $(CHAIN_DLL) $(CHAINTEST_EXE) $(STRESS_DLLS) $(STRESS_EXE) Makefile

# Basic builds must work with the older HH3 v2.2.1 toolchain too.
# The BFD/COFF parity suite is a diagnostic target (and a CI gate), not
# a prerequisite for deploying NTOS.
all: elf hh3 ntx

# GNU binutils is the authoritative SH4BE object/code linker. The bundled
# cross toolchain does NOT support sh-pe or pei-shl output: do not replace
# the compatibility PE packer with a format the toolchain cannot emit.
check-pe: ntx
	@sed 's/\r$//' tools/test_binutils_parity.sh | sh

probe-pe: ntx
	@sed 's/\r$//' tools/probe_binutils_pe.sh | sh

# Optional device regression: both DLLs already request preferred 0x10000000.
# The fixture instead requests 0x01000000 (the first fallback slot).
# Install dist_collision/XAMLUI.DLL in place of dist/XAMLUI.DLL only when
# testing the mapped loader; default dist/XAMLUI.DLL remains untouched.
collision-test: ntx
	$(PYTHON) tools/make_collision_dll.py $(XAMLUI_DLL) -o dist_collision/XAMLUI.DLL --base 0x01000000
.DEFAULT_GOAL := all
.SECONDARY: # Prevents intermediate files from being deleted

.NOTPARALLEL: clean
clean:
	rm -rf $(BUILDDIR) $(OUTDIR) $(DEPDIR) dist_collision

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
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(HELLO_OBJ)

$(HELLO_EXE): $(HELLO_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(HELLO_ELF) -o $@

$(COREDLL_OBJ): $(PAYLOAD_DIR)/coredll.c $(PAYLOAD_DIR)/ugui_font.h
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/coredll.c -o $@ $(PAYLOAD_CFLAGS)

$(COREDLL_ELF): $(COREDLL_OBJ) $(PAYLOAD_DIR)/coredll.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/coredll.ld -o $@ $(COREDLL_OBJ)

$(COREDLL_DLL): $(COREDLL_ELF) tools/elf2ntx.py tools/coredll_exports.txt
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(COREDLL_ELF) -o $@ --base 0x10000000 --dll 		--exports coredll_puts,coredll_getkey,coredll_refresh,coredll_malloc,coredll_free 		--exports-file tools/coredll_exports.txt --stub-symbol coredll_stub 		--dll-name COREDLL.DLL

# XAMLUI is a second PE DLL with the SAME preferred 0x10000000 base.
$(XAMLUI_OBJ): $(PAYLOAD_DIR)/xamlui.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(XAMLUI_ELF): $(XAMLUI_OBJ) $(PAYLOAD_DIR)/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/xamlui.ld -o $@ $(XAMLUI_OBJ)

$(XAMLUI_DLL): $(XAMLUI_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(XAMLUI_ELF) -o $@ --base 0x10000000 --dll --dll-name XAMLUI.DLL --import-module COREDLL.DLL --exports xamlui_tile,xamlui_label,xamlui_switch,xamlui_text,xamlui_shell,xamlui_pivot,xamlui_row,xamlui_checkbox,xamlui_progress,xamlui_actionbar,xamlui_dialog,xamlui_menu,xamlui_polygon,xamlui_folder,xamlui_start

$(HELLOCORE_OBJ): $(PAYLOAD_DIR)/hello_core.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/hello_core.c -o $@ $(PAYLOAD_CFLAGS)

$(HELLOCORE_ELF): $(HELLOCORE_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(HELLOCORE_OBJ)

$(HELLOCORE_EXE): $(HELLOCORE_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(HELLOCORE_ELF) -o $@ --import-module COREDLL.DLL

$(MEMTOOL_OBJ): $(PAYLOAD_DIR)/memtool.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/memtool.c -o $@ $(PAYLOAD_CFLAGS)

$(MEMTOOL_ELF): $(MEMTOOL_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(MEMTOOL_OBJ)

$(MEMTOOL_EXE): $(MEMTOOL_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(MEMTOOL_ELF) -o $@ --import-module COREDLL.DLL

$(PAINT_OBJ): $(PAYLOAD_DIR)/paintdemo.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(PAINT_ELF): $(PAINT_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(PAINT_OBJ)

$(PAINT_EXE): $(PAINT_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(PAINT_ELF) -o $@ --import-module COREDLL.DLL

$(WINLAB_OBJ): $(PAYLOAD_DIR)/winlab.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(WINLAB_ELF): $(WINLAB_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(WINLAB_OBJ)

$(WINLAB_EXE): $(WINLAB_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(WINLAB_ELF) -o $@ --import-module COREDLL.DLL

$(GUIWIN_OBJ): $(PAYLOAD_DIR)/guiwin.c $(PAYLOAD_DIR)/wince_ui.h $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(GUIWIN_ELF): $(GUIWIN_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(GUIWIN_OBJ)

$(GUIWIN_EXE): $(GUIWIN_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(GUIWIN_ELF) -o $@ --import-module COREDLL.DLL


$(METROWIN_OBJ): $(PAYLOAD_DIR)/metrowin.c $(PAYLOAD_DIR)/wince_ui.h $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(METROWIN_ELF): $(METROWIN_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(METROWIN_OBJ)

$(METROWIN_EXE): $(METROWIN_ELF) $(XAMLUI_DLL) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(METROWIN_ELF) -o $@ --import-module COREDLL.DLL --extra-import-prefix xamlui_:XAMLUI.DLL

$(CALC_OBJ): $(PAYLOAD_DIR)/calc.c $(PAYLOAD_DIR)/wince_ui.h $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CALC_ELF): $(CALC_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(CALC_OBJ)

$(CALC_EXE): $(CALC_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(CALC_ELF) -o $@ --import-module COREDLL.DLL

# A third named DLL with its own recursive import: CHAIN.DLL -> COREDLL.
$(CHAIN_OBJ): $(PAYLOAD_DIR)/chain.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CHAIN_ELF): $(CHAIN_OBJ) $(PAYLOAD_DIR)/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/xamlui.ld -o $@ $(CHAIN_OBJ)

$(CHAIN_DLL): $(CHAIN_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(CHAIN_ELF) -o $@ --base 0x10000000 --dll --dll-name CHAIN.DLL --import-module COREDLL.DLL --exports chain_echo

$(CHAINTEST_OBJ): $(PAYLOAD_DIR)/chain_test.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CHAINTEST_ELF): $(CHAINTEST_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(CHAINTEST_OBJ)

$(CHAINTEST_EXE): $(CHAINTEST_ELF) $(CHAIN_DLL) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(CHAINTEST_ELF) -o $@ --import-module CHAIN.DLL

# Stress PE DLL graph: recursive imports across 12 DLLs (+ COREDLL).
# Generated rules still compile and link with our normal GNU SH4BE tools.
define STRESS_LIBRARY_RULE
$(BUILDDIR)/payload/stress_$(1).o: $(PAYLOAD_DIR)/stress_$(1).c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $$(dir $$@)
	$$(CC) -c $$< -o $$@ $$(PAYLOAD_CFLAGS)
$(BUILDDIR)/payload/stress_$(1).elf: $(BUILDDIR)/payload/stress_$(1).o $(PAYLOAD_DIR)/xamlui.ld
	@mkdir -p $$(dir $$@)
	$$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/xamlui.ld -o $$@ $$<
$(OUTDIR)/ST$(1).DLL: $(BUILDDIR)/payload/stress_$(1).elf tools/elf2ntx.py
	@mkdir -p $$(dir $$@)
	$$(PYTHON) tools/elf2ntx.py $$< -o $$@ --base 0x10000000 --dll --dll-name ST$(1).DLL --import-module COREDLL.DLL --exports stress_d$(1) $$(STRESS_EXTRA_$(1))
endef
$(foreach node,$(STRESS_NAMES),$(eval $(call STRESS_LIBRARY_RULE,$(node))))

$(STRESS_OBJ): $(PAYLOAD_DIR)/stress_exec.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(STRESS_ELF): $(STRESS_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $<

$(STRESS_EXE): $(STRESS_ELF) $(STRESS_DLLS) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(STRESS_ELF) -o $@ --import-module ST01.DLL

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

.PHONY: elf hh3 ntx all clean compile_commands.json check-pe probe-pe collision-test

-include $(DEPFILES)
