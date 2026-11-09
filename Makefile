SOURCEDIR = src
BUILDDIR = obj
OUTDIR = dist
DEPDIR = .deps

AS:=sh4a_nofpueb-elf-gcc
AS_FLAGS=-gdwarf-5 $(DEFINES)

SDK_DIR?=/sdk

DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
WARNINGS=-Wall -Wextra -pedantic -Werror -pedantic-errors
INCLUDES=-I$(SDK_DIR)/include #-I$(SOURCEDIR)
# Pager validated in emulator: the default loader now handles graphs beyond
# the 55-slot eager translation budget. Use DEFINES= for the conservative build.
DEFINES=-DNTOS_ENABLE_TLB_MISS_PAGER=1 -DNTOS_ENABLE_IRQ_OWNERSHIP=1
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
CONTROL_OBJ := $(BUILDDIR)/payload/control.o
CONTROL_ELF := $(BUILDDIR)/payload/control.elf
CONTROL_EXE := $(OUTDIR)/CONTROL.EXE
CONTROL_MEM_OBJ := $(BUILDDIR)/payload/control_memory.o
CPL_OBJ := $(BUILDDIR)/payload/demo_cpl.o
CPL_ELF := $(BUILDDIR)/payload/demo_cpl.elf
CPL_DLL := $(OUTDIR)/DEMO.CPL
CPL_INSTALL := $(OUTDIR)/Windows/demo.cpl
CHAIN_OBJ := $(BUILDDIR)/payload/chain.o
CHAIN_ELF := $(BUILDDIR)/payload/chain.elf
CHAIN_DLL := $(OUTDIR)/CHAIN.DLL
CHAINTEST_OBJ := $(BUILDDIR)/payload/chain_test.o
CHAINTEST_ELF := $(BUILDDIR)/payload/chain_test.elf
CHAINTEST_EXE := $(OUTDIR)/CHAINTEST.EXE
ABI_TEST_OBJ := $(BUILDDIR)/payload/abi_test.o
ABI_TEST_ELF := $(BUILDDIR)/payload/abi_test.elf
ABI_TEST_EXE := $(OUTDIR)/ABITEST.EXE
# 12 PE DLLs at identical preferred bases; ST01 fans out to four DLLs.
STRESS_NAMES := 01 02 03 04 05 06 07 08 09 10 11 12
STRESS_DLLS := $(addprefix $(OUTDIR)/ST,$(addsuffix .DLL,$(STRESS_NAMES)))
STRESS_OBJ := $(BUILDDIR)/payload/stress_exec.o
STRESS_ELF := $(BUILDDIR)/payload/stress_exec.elf
STRESS_EXE := $(OUTDIR)/STRESSEXEC.EXE
PAGERSTRESS_OBJ := $(BUILDDIR)/payload/pagerstress.o
PAGERSTRESS_ELF := $(BUILDDIR)/payload/pagerstress.elf
PAGERSTRESS_DLL := $(OUTDIR)/PAGERSTRESS.DLL
PAGERSTRESS_TEST_OBJ := $(BUILDDIR)/payload/pagerstress_test.o
PAGERSTRESS_TEST_ELF := $(BUILDDIR)/payload/pagerstress_test.elf
PAGERSTRESS_TEST_EXE := $(OUTDIR)/PAGERTEST.EXE
# Control image: identical 80-page DLL with NO explicit prefetch hints.
# Run FAULTTEST only after PAGERTEST confirms UTLB installs work.
FAULTSTRESS_OBJ := $(BUILDDIR)/payload/faultstress.o
FAULTSTRESS_ELF := $(BUILDDIR)/payload/faultstress.elf
FAULTSTRESS_DLL := $(OUTDIR)/FAULTSTRESS.DLL
FAULTTEST_EXE := $(OUTDIR)/FAULTTEST.EXE
STRESS_EXTRA_01 := --extra-import-prefix stress_d02:ST02.DLL --extra-import-prefix stress_d03:ST03.DLL --extra-import-prefix stress_d04:ST04.DLL --extra-import-prefix stress_d05:ST05.DLL
STRESS_EXTRA_02 := --extra-import-prefix stress_d06:ST06.DLL --extra-import-prefix stress_d07:ST07.DLL
STRESS_EXTRA_03 := --extra-import-prefix stress_d08:ST08.DLL --extra-import-prefix stress_d09:ST09.DLL
STRESS_EXTRA_04 := --extra-import-prefix stress_d10:ST10.DLL --extra-import-prefix stress_d11:ST11.DLL
STRESS_EXTRA_05 := --extra-import-prefix stress_d12:ST12.DLL


AS_SOURCES:=$(shell find $(SOURCEDIR) -name '*.S')
CC_SOURCES:=$(shell find $(SOURCEDIR) -name '*.c')
CXX_SOURCES:=$(shell find $(SOURCEDIR) -name '*.cpp')
OBJECTS := $(addprefix $(BUILDDIR)/,$(AS_SOURCES:.S=.o)) 	$(addprefix $(BUILDDIR)/,$(CC_SOURCES:.c=.o)) 	$(addprefix $(BUILDDIR)/,$(CXX_SOURCES:.cpp=.o))

# Flag-stamp forces a safe object rebuild when an existing checkout switches
# between the former eager loader, pager default and diagnostic variants.
.PHONY: ntos-config-force
$(BUILDDIR)/.ntos-build-flags: ntos-config-force
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(DEFINES)' > $@.tmp
	@if test -e $@ && cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(OBJECTS): $(BUILDDIR)/.ntos-build-flags

NOLTOOBJS := $(foreach obj, $(OBJECTS), $(if $(findstring /nolto/, $(obj)), $(obj)))

DEPFILES := $(OBJECTS:$(BUILDDIR)/%.o=$(DEPDIR)/%.d)

hh3: $(APP_HH3) Makefile
elf: $(APP_ELF) Makefile
ntx: $(HELLO_EXE) $(COREDLL_DLL) $(XAMLUI_DLL) $(HELLOCORE_EXE) $(MEMTOOL_EXE) $(PAINT_EXE) $(WINLAB_EXE) $(GUIWIN_EXE) $(METROWIN_EXE) $(CALC_EXE) $(CONTROL_EXE) $(CPL_INSTALL) $(CHAIN_DLL) $(CHAINTEST_EXE) $(STRESS_DLLS) $(STRESS_EXE) $(PAGERSTRESS_DLL) $(PAGERSTRESS_TEST_EXE) $(FAULTSTRESS_DLL) $(FAULTTEST_EXE) Makefile

# Basic builds must work with the older HH3 v2.2.1 toolchain too.
# The BFD/COFF parity suite is a diagnostic target (and a CI gate), not
# a prerequisite for deploying NTOS.
all: elf hh3 ntx phone

# GNU binutils is the authoritative SH4BE object/code linker. The bundled
# cross toolchain does NOT support sh-pe or pei-shl output: do not replace
# the compatibility PE packer with a format the toolchain cannot emit.
check-pe: ntx
	@tr -d '\r' < tools/test_binutils_parity.sh | sh

probe-pe: ntx
	@tr -d '\r' < tools/probe_binutils_pe.sh | sh

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

$(COREDLL_OBJ): $(PAYLOAD_DIR)/coredll.c $(PAYLOAD_DIR)/ugui_font.h $(PAYLOAD_DIR)/ntapi_caps.h
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/coredll.c -o $@ $(PAYLOAD_CFLAGS)

$(COREDLL_ELF): $(COREDLL_OBJ) $(PAYLOAD_DIR)/coredll.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/coredll.ld -o $@ $(COREDLL_OBJ)

$(COREDLL_DLL): $(COREDLL_ELF) tools/elf2ntx.py tools/coredll_exports.txt
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(COREDLL_ELF) -o $@ --base 0x10000000 --dll 		--exports coredll_launch,coredll_puts,coredll_getkey,coredll_refresh,coredll_malloc,coredll_free 		--exports-file tools/coredll_exports.txt --stub-symbol coredll_stub 		--dll-name COREDLL.DLL

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

# NTOS-native Control Panel uses the same COREDLL as calculator and phone.
$(CONTROL_OBJ): $(PAYLOAD_DIR)/control.c $(PAYLOAD_DIR)/wince_ui.h $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CPL_OBJ): payload/wince/demo/demo_cpl.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS) -DDemoCPlApplet=CPlApplet

$(CPL_ELF): $(CPL_OBJ) $(PAYLOAD_DIR)/coredll.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/coredll.ld -o $@ $(CPL_OBJ)

$(CPL_DLL): $(CPL_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $< -o $@ --base 0x10000000 --dll --dll-name DEMO.CPL --exports CPlApplet

$(CPL_INSTALL): $(CPL_DLL)
	@mkdir -p $(dir $@)
	cp $< $@

$(CONTROL_MEM_OBJ): payload/phone/tiny_memory.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CONTROL_ELF): $(CONTROL_OBJ) $(CONTROL_MEM_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(CONTROL_OBJ) $(CONTROL_MEM_OBJ)

$(CONTROL_EXE): $(CONTROL_ELF) $(CPL_DLL) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(CONTROL_ELF) -o $@ --import-module COREDLL.DLL --extra-import-prefix CPlApplet:DEMO.CPL

# A third named DLL with its own recursive import: CHAIN.DLL -> COREDLL.
$(CHAIN_OBJ): $(PAYLOAD_DIR)/chain.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CHAIN_ELF): $(CHAIN_OBJ) $(PAYLOAD_DIR)/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/xamlui.ld -o $@ $(CHAIN_OBJ)

$(CHAIN_DLL): $(CHAIN_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(CHAIN_ELF) -o $@ --base 0x10000000 --dll --dll-name CHAIN.DLL --import-module COREDLL.DLL --exports chain_echo,chain_abi5

$(CHAINTEST_OBJ): $(PAYLOAD_DIR)/chain_test.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CHAINTEST_ELF): $(CHAINTEST_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $(CHAINTEST_OBJ)

$(CHAINTEST_EXE): $(CHAINTEST_ELF) $(CHAIN_DLL) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(CHAINTEST_ELF) -o $@ --import-module CHAIN.DLL

# Explicit cross-module function-pointer ABI test, five integer arguments.
# Build standalone in dist_ce_abi with `make ce-abi-probe`.
$(ABI_TEST_OBJ): $(PAYLOAD_DIR)/abi_test.c $(PAYLOAD_DIR)/ntimport.h $(PAYLOAD_DIR)/ntabi.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(ABI_TEST_ELF): $(ABI_TEST_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $<

$(ABI_TEST_EXE): $(ABI_TEST_ELF) $(CHAIN_DLL) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(ABI_TEST_ELF) -o $@ --import-module CHAIN.DLL

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
	$$(PYTHON) tools/elf2ntx.py $$< -o $$@ --base 0x10000000 --dll --dll-name ST$(1).DLL --exports stress_d$(1) $(if $(filter-out ,$(STRESS_EXTRA_$(1))),--import-module COREDLL.DLL) $(STRESS_EXTRA_$(1))
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

# 80-BSS-page DLL: rejected by default eager loader, but RAM-backed
# experimental SH4A VBR path should fault pages transparently and persist
# changes through UTLB eviction. Both share WinCE-style DLL ImageBase.
$(PAGERSTRESS_OBJ): $(PAYLOAD_DIR)/pagerstress.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(PAGERSTRESS_ELF): $(PAGERSTRESS_OBJ) $(PAYLOAD_DIR)/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/xamlui.ld -o $@ $<

$(PAGERSTRESS_DLL): $(PAGERSTRESS_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $< -o $@ --base 0x10000000 --dll --dll-name PAGERSTRESS.DLL --exports pager_touch

$(PAGERSTRESS_TEST_OBJ): $(PAYLOAD_DIR)/pagerstress_test.c $(PAYLOAD_DIR)/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(PAGERSTRESS_TEST_ELF): $(PAGERSTRESS_TEST_OBJ) $(PAYLOAD_DIR)/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/payload.ld -o $@ $<

$(PAGERSTRESS_TEST_EXE): $(PAGERSTRESS_TEST_ELF) $(PAGERSTRESS_DLL) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $< -o $@ --import-module PAGERSTRESS.DLL

# Same DLL image and function, but no per-page prefault. If this hangs
# while the instrumented PAGERTEST passes, VBR exception delivery is broken.
$(FAULTSTRESS_OBJ): $(PAYLOAD_DIR)/pagerstress.c
	@mkdir -p $(dir $@)
	$(CC) -DPAGER_USE_HINTS=0 -c $< -o $@ $(PAYLOAD_CFLAGS)

$(FAULTSTRESS_ELF): $(FAULTSTRESS_OBJ) $(PAYLOAD_DIR)/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T $(PAYLOAD_DIR)/xamlui.ld -o $@ $<

$(FAULTSTRESS_DLL): $(FAULTSTRESS_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $< -o $@ --base 0x10000000 --dll --dll-name FAULTSTRESS.DLL --exports pager_touch

$(FAULTTEST_EXE): $(PAGERSTRESS_TEST_ELF) $(FAULTSTRESS_DLL) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(PAGERSTRESS_TEST_ELF) -o $@ --import-module FAULTSTRESS.DLL

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

# Isolated *experimental* firmware. Never reuses the default output or
# object directories: cross-build flag changes cannot silently mix objects.
# Deploy dist_pager/NTOS.hh3 ONLY after emulator verification.
pager-experiment:
	$(MAKE) -j4 BUILDDIR=obj_pager OUTDIR=dist_pager DEPDIR=.deps_pager DEFINES="-DNTOS_ENABLE_TLB_MISS_PAGER=1 -DNTOS_ENABLE_IRQ_OWNERSHIP=1" hh3
	@echo "EXPERIMENTAL PAGER: dist_pager/NTOS.hh3 (VBR fault handler unverified on hardware)"

# Optional emulator diagnostic. Reads SH7305 UTLB P4 address/data arrays
# after each LDTLB. Some emulators do not support these P4 readbacks:
# NEVER replace the regular experimental image with this diagnostic.
pager-verify:
	$(MAKE) -j4 BUILDDIR=obj_pager_verify OUTDIR=dist_pager_verify DEPDIR=.deps_pager_verify DEFINES="-DNTOS_ENABLE_TLB_MISS_PAGER=1 -DNTOS_ENABLE_IRQ_OWNERSHIP=1 -DNTOS_VERIFY_UTLB_SLOTS=1" hh3
	@echo "UTLB READBACK DIAGNOSTIC: dist_pager_verify/NTOS.hh3 (emulator only)"

# This is an isolated Renesas-ABI comparison build, NOT an MSVC
# compatibility release. The default dist and pager builds do not change.
ce-abi-probe:
	$(MAKE) -j4 BUILDDIR=obj_ce_abi OUTDIR=dist_ce_abi DEPDIR=.deps_ce_abi PAYLOAD_CFLAGS="$(PAYLOAD_CFLAGS) -DNTOS_SH_RENESAS_ABI_PROBE=1" dist_ce_abi/COREDLL.DLL dist_ce_abi/CHAIN.DLL dist_ce_abi/ABITEST.EXE
	@echo "ABI CANDIDATE ONLY: dist_ce_abi/ABITEST.EXE (expect 0x0AB1C0DE)"

.PHONY: elf hh3 ntx all clean compile_commands.json check-pe probe-pe collision-test pager-experiment pager-verify ce-abi-probe

-include $(DEPFILES)

# Experimental measured MSVC SH4 ABI bridges. Both targets are completely
# isolated: no compiler patch, no modified release or pager output.
MSVC_GEN_DIR := obj_msvc_thunks
MSVC_GLOBAL_OBJ := obj_msvc_global
MSVC_GLOBAL_DIST := dist_msvc_global
MSVC_THUNKS_DIST := dist_msvc_thunks

$(MSVC_GLOBAL_OBJ)/native.o: payload/abi_msvc_native.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(MSVC_GLOBAL_OBJ)/dispatch.o: payload/abi_msvc_dispatch.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(MSVC_GLOBAL_OBJ)/runtime.o: payload/abi_msvc_runtime.S
	@mkdir -p $(dir $@)
	$(AS) -c $< -o $@ $(AS_FLAGS)

$(MSVC_GLOBAL_OBJ)/call.o: payload/abi_msvc_call.S
	@mkdir -p $(dir $@)
	$(AS) -c $< -o $@ $(AS_FLAGS)

$(MSVC_GLOBAL_OBJ)/test.o: payload/abi_msvc_test.c payload/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -DNTOS_MSVC_TEST_GLOBAL=1 -c $< -o $@ $(PAYLOAD_CFLAGS)

$(MSVC_GLOBAL_DIST)/ABIGLOBAL.elf: $(MSVC_GLOBAL_OBJ)/native.o $(MSVC_GLOBAL_OBJ)/runtime.o $(MSVC_GLOBAL_OBJ)/dispatch.o payload/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/xamlui.ld -o $@ $(MSVC_GLOBAL_OBJ)/native.o $(MSVC_GLOBAL_OBJ)/runtime.o $(MSVC_GLOBAL_OBJ)/dispatch.o

$(MSVC_GLOBAL_DIST)/ABIGLOBAL.DLL: $(MSVC_GLOBAL_DIST)/ABIGLOBAL.elf tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --base 0x10000000 --dll --dll-name ABIGLOBAL.DLL --exports abi_msvc5,abi_msvc8

$(MSVC_GLOBAL_DIST)/ABIGLOBAL_TEST.elf: $(MSVC_GLOBAL_OBJ)/test.o $(MSVC_GLOBAL_OBJ)/call.o payload/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/payload.ld -o $@ $(MSVC_GLOBAL_OBJ)/test.o $(MSVC_GLOBAL_OBJ)/call.o

$(MSVC_GLOBAL_DIST)/ABIGLOBAL.EXE: $(MSVC_GLOBAL_DIST)/ABIGLOBAL_TEST.elf $(MSVC_GLOBAL_DIST)/ABIGLOBAL.DLL tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --import-module ABIGLOBAL.DLL

$(MSVC_GEN_DIR)/export.S: tools/gen_sh4_msvc_thunks.py
	$(PYTHON) $< --role export --output $@

$(MSVC_GEN_DIR)/import.S: tools/gen_sh4_msvc_thunks.py
	$(PYTHON) $< --role import --output $@

$(MSVC_GEN_DIR)/export.o: $(MSVC_GEN_DIR)/export.S
	$(AS) -c $< -o $@ $(AS_FLAGS)

$(MSVC_GEN_DIR)/import.o: $(MSVC_GEN_DIR)/import.S
	$(AS) -c $< -o $@ $(AS_FLAGS)

$(MSVC_GEN_DIR)/native.o: payload/abi_msvc_native.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(MSVC_GEN_DIR)/test.o: payload/abi_msvc_test.c payload/ntimport.h
	@mkdir -p $(dir $@)
	$(CC) -DNTOS_MSVC_TEST_GLOBAL=0 -c $< -o $@ $(PAYLOAD_CFLAGS)

$(MSVC_THUNKS_DIST)/ABITHUNKS.elf: $(MSVC_GEN_DIR)/native.o $(MSVC_GEN_DIR)/export.o payload/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/xamlui.ld -o $@ $(MSVC_GEN_DIR)/native.o $(MSVC_GEN_DIR)/export.o

$(MSVC_THUNKS_DIST)/ABITHUNKS.DLL: $(MSVC_THUNKS_DIST)/ABITHUNKS.elf tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --base 0x10000000 --dll --dll-name ABITHUNKS.DLL --exports abi_msvc5,abi_msvc8

$(MSVC_THUNKS_DIST)/ABITHUNKS_TEST.elf: $(MSVC_GEN_DIR)/test.o $(MSVC_GEN_DIR)/import.o payload/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/payload.ld -o $@ $(MSVC_GEN_DIR)/test.o $(MSVC_GEN_DIR)/import.o

$(MSVC_THUNKS_DIST)/ABITHUNKS.EXE: $(MSVC_THUNKS_DIST)/ABITHUNKS_TEST.elf $(MSVC_THUNKS_DIST)/ABITHUNKS.DLL tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --import-module ABITHUNKS.DLL

msvc-abi-global: $(MSVC_GLOBAL_DIST)/ABIGLOBAL.DLL $(MSVC_GLOBAL_DIST)/ABIGLOBAL.EXE
	@echo "MSVC SH4 shared runtime ABI test: ABIGLOBAL.EXE (not emulator-validated)"

msvc-abi-thunks: $(MSVC_THUNKS_DIST)/ABITHUNKS.DLL $(MSVC_THUNKS_DIST)/ABITHUNKS.EXE
	@echo "MSVC SH4 generated thunks ABI test: ABITHUNKS.EXE (not emulator-validated)"

msvc-abi-tests: msvc-abi-global msvc-abi-thunks

.PHONY: msvc-abi-global msvc-abi-thunks msvc-abi-tests

# Typed MSVC SH4 ABI boundary experiment. The generated assembly is the
# actual ABI entry; C marshaling deliberately lets native GCC handle R2
# struct returns/aggregate calling conventions. No GCC patch is required.
TYPED_GEN := obj_msvc_typed/generated
TYPED_OBJ := obj_msvc_typed
TYPED_DIST := dist_msvc_typed
TYPED_EXPORTS := abi_typed_5,abi_typed_8,abi_typed_s3,abi_typed_s3split,abi_typed_u64,abi_typed_u64split,abi_typed_ret_s3,abi_typed_ret_u64,abi_typed_ret_bytes3,abi_typed_bytes3,abi_typed_ret_s16,abi_typed_ret_u16,abi_typed_ptr,abi_typed_zero,abi_typed_ret_bytes1,abi_typed_ret_bytes4,abi_typed_ret_s8,abi_typed_ret_u8

$(TYPED_GEN)/stamp: tools/gen_sh4_msvc_typed.py payload/abi_typed_manifest.json
	$(PYTHON) tools/gen_sh4_msvc_typed.py --manifest payload/abi_typed_manifest.json --outdir $(TYPED_GEN)
	@touch $@

$(TYPED_OBJ)/native.o: payload/abi_typed_impl.c $(TYPED_GEN)/stamp
	$(CC) -c $< -o $@ -I$(TYPED_GEN) $(PAYLOAD_CFLAGS)

$(TYPED_OBJ)/body.o: $(TYPED_GEN)/stamp
	$(CC) -c $(TYPED_GEN)/abi_export_body.c -o $@ -I$(TYPED_GEN) $(PAYLOAD_CFLAGS)

$(TYPED_OBJ)/export.o: $(TYPED_GEN)/stamp
	$(AS) -c $(TYPED_GEN)/abi_export.S -o $@ $(AS_FLAGS)

$(TYPED_OBJ)/import.o: $(TYPED_GEN)/stamp
	$(CC) -c $(TYPED_GEN)/abi_import.c -o $@ -I$(TYPED_GEN) $(PAYLOAD_CFLAGS)

$(TYPED_OBJ)/call.o: $(TYPED_GEN)/stamp
	$(AS) -c $(TYPED_GEN)/abi_call.S -o $@ $(AS_FLAGS)

$(TYPED_OBJ)/test.o: payload/abi_typed_test.c $(TYPED_GEN)/stamp
	$(CC) -c $< -o $@ -I$(TYPED_GEN) $(PAYLOAD_CFLAGS)

$(TYPED_OBJ)/memcpy.o: payload/abi_typed_memcpy.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(TYPED_DIST)/ABITYPED.elf: $(TYPED_OBJ)/native.o $(TYPED_OBJ)/body.o $(TYPED_OBJ)/export.o payload/xamlui.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/xamlui.ld -o $@ $(TYPED_OBJ)/native.o $(TYPED_OBJ)/body.o $(TYPED_OBJ)/export.o

$(TYPED_DIST)/ABITYPED.DLL: $(TYPED_DIST)/ABITYPED.elf tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --base 0x10000000 --dll --dll-name ABITYPED.DLL --exports $(TYPED_EXPORTS)

$(TYPED_DIST)/ABITYPED_TEST.elf: $(TYPED_OBJ)/test.o $(TYPED_OBJ)/import.o $(TYPED_OBJ)/call.o $(TYPED_OBJ)/memcpy.o payload/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/payload.ld -o $@ $(TYPED_OBJ)/test.o $(TYPED_OBJ)/import.o $(TYPED_OBJ)/call.o $(TYPED_OBJ)/memcpy.o

$(TYPED_DIST)/ABITYPED.EXE: $(TYPED_DIST)/ABITYPED_TEST.elf $(TYPED_DIST)/ABITYPED.DLL tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --import-module ABITYPED.DLL

msvc-abi-typed: $(TYPED_DIST)/ABITYPED.DLL $(TYPED_DIST)/ABITYPED.EXE
	@echo "TYPED MSVC SH4 ABI experiment: ABITYPED.EXE expects 0xAB1C0421"

.PHONY: msvc-abi-typed

# High-volume typed MSVC ABI stress test, independent native C oracle
# linked into the EXE (the DLL still uses the generated MSVC export thunks).
$(TYPED_OBJ)/stress.o: payload/abi_typed_stress.c $(TYPED_GEN)/stamp
	$(CC) -c $< -o $@ -I$(TYPED_GEN) $(PAYLOAD_CFLAGS)

$(TYPED_OBJ)/oracle.o: payload/abi_typed_impl.c $(TYPED_GEN)/stamp
	$(CC) -DNTOS_MSVC_HOST_TEST=1 -c $< -o $@ -I$(TYPED_GEN) $(PAYLOAD_CFLAGS)

$(TYPED_DIST)/ABISTRESS.elf: $(TYPED_OBJ)/stress.o $(TYPED_OBJ)/oracle.o $(TYPED_OBJ)/import.o $(TYPED_OBJ)/call.o $(TYPED_OBJ)/memcpy.o payload/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/payload.ld -o $@ $(TYPED_OBJ)/stress.o $(TYPED_OBJ)/oracle.o $(TYPED_OBJ)/import.o $(TYPED_OBJ)/call.o $(TYPED_OBJ)/memcpy.o

$(TYPED_DIST)/ABISTRESS.EXE: $(TYPED_DIST)/ABISTRESS.elf $(TYPED_DIST)/ABITYPED.DLL tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --import-module ABITYPED.DLL

msvc-abi-stress: $(TYPED_DIST)/ABISTRESS.EXE
	@echo "MSVC 2304-call stress: ABISTRESS.EXE expects 0xAB1C0600"

.PHONY: msvc-abi-stress

# Opt-in ABI-wrapped *real* COREDLL subset. This is not a release
# replacement for the ~1900 exported COREDLL symbols: unclassified
# signatures must never be passed as if they were scalar ABI functions.
CE_CORE_GEN := obj_msvc_coredll/generated
CE_CORE_OBJ := obj_msvc_coredll
CE_CORE_DIST := dist_msvc_coredll

$(CE_CORE_GEN)/stamp: tools/gen_sh4_msvc_typed.py payload/coredll_msvc_manifest.json
	$(PYTHON) tools/gen_sh4_msvc_typed.py --manifest payload/coredll_msvc_manifest.json --outdir $(CE_CORE_GEN)
	@touch $@

$(CE_CORE_OBJ)/native.o: payload/coredll.c $(CE_CORE_GEN)/stamp
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS) \
	  -Dcoredll_puts=nt_native_coredll_puts \
	  -Dcoredll_getkey=nt_native_coredll_getkey \
	  -Dcoredll_malloc=nt_native_coredll_malloc \
	  -Dcoredll_free=nt_native_coredll_free

$(CE_CORE_OBJ)/probe.o: payload/coredll_msvc_probe.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(PAYLOAD_CFLAGS)

$(CE_CORE_OBJ)/body.o: $(CE_CORE_GEN)/stamp
	$(CC) -c $(CE_CORE_GEN)/abi_export_body.c -o $@ -I$(CE_CORE_GEN) $(PAYLOAD_CFLAGS)

$(CE_CORE_OBJ)/export.o: $(CE_CORE_GEN)/stamp
	$(AS) -c $(CE_CORE_GEN)/abi_export.S -o $@ $(AS_FLAGS)

$(CE_CORE_OBJ)/import.o: $(CE_CORE_GEN)/stamp
	$(CC) -c $(CE_CORE_GEN)/abi_import.c -o $@ -I$(CE_CORE_GEN) $(PAYLOAD_CFLAGS)

$(CE_CORE_OBJ)/call.o: $(CE_CORE_GEN)/stamp
	$(AS) -c $(CE_CORE_GEN)/abi_call.S -o $@ $(AS_FLAGS)

$(CE_CORE_OBJ)/test.o: payload/coredll_msvc_test.c $(CE_CORE_GEN)/stamp
	$(CC) -c $< -o $@ -I$(CE_CORE_GEN) $(PAYLOAD_CFLAGS)

$(CE_CORE_DIST)/COREDLL.elf: $(CE_CORE_OBJ)/native.o $(CE_CORE_OBJ)/probe.o $(CE_CORE_OBJ)/body.o $(CE_CORE_OBJ)/export.o payload/coredll.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/coredll.ld -o $@ $(CE_CORE_OBJ)/native.o $(CE_CORE_OBJ)/probe.o $(CE_CORE_OBJ)/body.o $(CE_CORE_OBJ)/export.o

$(CE_CORE_DIST)/COREDLL.DLL: $(CE_CORE_DIST)/COREDLL.elf tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --base 0x10000000 --dll --dll-name COREDLL.DLL --exports coredll_puts,coredll_getkey,coredll_malloc,coredll_free,coredll_abi5

$(CE_CORE_DIST)/COREDLLABI.elf: $(CE_CORE_OBJ)/test.o $(CE_CORE_OBJ)/import.o $(CE_CORE_OBJ)/call.o payload/payload.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/payload.ld -o $@ $(CE_CORE_OBJ)/test.o $(CE_CORE_OBJ)/import.o $(CE_CORE_OBJ)/call.o

$(CE_CORE_DIST)/COREDLLABI.EXE: $(CE_CORE_DIST)/COREDLLABI.elf $(CE_CORE_DIST)/COREDLL.DLL tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $< -o $@ --import-module COREDLL.DLL

msvc-abi-coredll: $(CE_CORE_DIST)/COREDLL.DLL $(CE_CORE_DIST)/COREDLLABI.EXE
	@echo "REAL COREDLL MSVC ABI SUBSET: COREDLLABI.EXE expects 0xAB1C0500"

.PHONY: msvc-abi-coredll

# The default `make -j4` builds the complete flat native OS tree.
# `unified` is kept as a backwards-compatible alias, not a second distribution.
.PHONY: unified
unified: all
	$(PYTHON) tools/check_unified_bundle.py $(OUTDIR)

# Native WinCE-first phone launcher.
include payload/phone/phone.mk
