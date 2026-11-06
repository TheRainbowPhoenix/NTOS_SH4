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

# --- .NTX payload (big-endian SH4 COFF EXE, machine 0x01A8) -----------------
PAYLOAD_DIR := payload
PAYLOAD_NAME := HELLO
PAYLOAD_LD := $(PAYLOAD_DIR)/payload.ld
PAYLOAD_SRCS := $(wildcard $(PAYLOAD_DIR)/*.c)
PAYLOAD_OBJ := $(BUILDDIR)/payload/hello.o
PAYLOAD_ELF := $(BUILDDIR)/payload/hello.elf
PAYLOAD_NTX := $(OUTDIR)/$(PAYLOAD_NAME).NTX

# Native SH4BE MZ+PE32 proof of concept: real machine code and COREDLL IAT.
PAYLOAD_CE_OBJ := $(BUILDDIR)/payload/hello_ce.o
PAYLOAD_CE_ELF := $(BUILDDIR)/payload/hello_ce.elf
PAYLOAD_CE_NTX := $(BUILDDIR)/payload/hello_ce.ntx
PAYLOAD_CE_PE := $(OUTDIR)/HELLO_PE.EXE

$(PAYLOAD_CE_OBJ): $(PAYLOAD_DIR)/hello_ce.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -ffreestanding -fno-builtin -fno-stack-protector \
		-fno-unwind-tables -fno-asynchronous-unwind-tables -O2 $(INCLUDES)

$(PAYLOAD_CE_ELF): $(PAYLOAD_CE_OBJ) $(PAYLOAD_LD)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_LD) -o $@ $(PAYLOAD_CE_OBJ)

$(PAYLOAD_CE_NTX): $(PAYLOAD_CE_ELF) tools/elf2ntx.py
	$(PYTHON) tools/elf2ntx.py $(PAYLOAD_CE_ELF) -o $@

$(PAYLOAD_CE_PE): $(PAYLOAD_CE_NTX) $(PAYLOAD_CE_ELF) tools/ntx2pe.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/ntx2pe.py $(PAYLOAD_CE_NTX) --elf $(PAYLOAD_CE_ELF) -o $@

AS_SOURCES:=$(shell find $(SOURCEDIR) -name '*.S')
CC_SOURCES:=$(shell find $(SOURCEDIR) -name '*.c')
CXX_SOURCES:=$(shell find $(SOURCEDIR) -name '*.cpp')
OBJECTS := $(addprefix $(BUILDDIR)/,$(AS_SOURCES:.S=.o)) \
	$(addprefix $(BUILDDIR)/,$(CC_SOURCES:.c=.o)) \
	$(addprefix $(BUILDDIR)/,$(CXX_SOURCES:.cpp=.o))

NOLTOOBJS := $(foreach obj, $(OBJECTS), $(if $(findstring /nolto/, $(obj)), $(obj)))

DEPFILES := $(OBJECTS:$(BUILDDIR)/%.o=$(DEPDIR)/%.d)

hh3: $(APP_HH3) Makefile
elf: $(APP_ELF) Makefile
ntx: $(PAYLOAD_NTX) $(PAYLOAD_CE_PE) Makefile

all: elf hh3 ntx $(PAYLOAD_CE_PE)
.DEFAULT_GOAL := all
.SECONDARY: # Prevents intermediate files from being deleted

.NOTPARALLEL: clean
clean:
	rm -rf $(BUILDDIR) $(OUTDIR) $(DEPDIR)

%.hh3: %.elf
	$(STRIP) -o $@ $^

# Payload: freestanding compile, static link at 0x00400000 with retained
# relocations, then the ELF->COFF packer emits the BE NTX EXE.
$(PAYLOAD_OBJ): $(PAYLOAD_SRCS)
	@mkdir -p $(dir $@)
	$(CC) -c $(PAYLOAD_DIR)/hello.c -o $@ \
		-ffreestanding -fno-builtin -fno-stack-protector \
		-fno-unwind-tables -fno-asynchronous-unwind-tables \
		-O2 -Wall -Wextra $(INCLUDES)

$(PAYLOAD_ELF): $(PAYLOAD_OBJ) $(PAYLOAD_LD)
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -Wl,--emit-relocs -Wl,-e,main -T $(PAYLOAD_LD) -o $@ $(PAYLOAD_OBJ)

$(PAYLOAD_NTX): $(PAYLOAD_ELF) tools/elf2ntx.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/elf2ntx.py $(PAYLOAD_ELF) -o $@

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