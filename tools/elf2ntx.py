#!/usr/bin/env python3
"""
elf2ntx.py -- PE linker/packer: big-endian SH4 ELF -> PE EXE/DLL.

Takes a fully linked big-endian ELF32 (SuperH) executable -- built by
`sh4a_nofpueb-elf-ld --emit-relocs` from freestanding objects -- and emits a
**standard PE32 executable**: MZ header, "PE\\0\\0" signature, little-endian
header fields, PE data directories, import/export/base-reloc tables -- all
exactly per the PE spec so common tooling (pefile, dumpbin, Ghidra's PE
loader) can parse the output. The machine type is 0x01A8 ("SH4 BE",
MIPS-style, per our patched shasm); the CPU-visible bytes (SH4 code, literal
pools, .data pointers, IAT slots) are big-endian, while every file-format
structure is little-endian as the spec requires.

NTOS copy-loads the image into the heap and either relocates it (applying
delta = actual_base - preferred_base to every HIGHLOW site) or UTLB-maps it
at its preferred base and snaps its imports against COREDLL.DLL -- the same
two mechanisms a WinCE loader uses. Absolute addresses must be present in
the file: link with `--emit-relocs` so ld retains the relocation sites; every
R_SH_DIR32 site whose symbol is defined inside the image becomes a HIGHLOW
entry.

Usage:
    python3 tools/elf2ntx.py payload.elf -o dist/HELLO.EXE [--base 0x400000]
    python3 tools/elf2ntx.py coredll.elf -o dist/COREDLL.DLL --base 0x800000 \
        --exports coredll_puts,coredll_getkey --dll-name COREDLL.DLL --dll
    python3 tools/elf2ntx.py hello.elf -o dist/HELLOCORE.EXE \
        --import-module COREDLL.DLL

    python3 tools/elf2ntx.py --selftest     (synthetic ELF round-trip check)
"""
import argparse
import struct
import sys

# --- constants ---------------------------------------------------------------
EM_SH = 42

R_SH_DIR32 = 1  # 32-bit absolute reference (literal pools, .data pointers)

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOBITS = 8
SHT_REL = 9

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

SHN_UNDEF = 0
SHN_LORESERVE = 0xFF00
SHN_ABS = 0xFFF1

STT_NOTYPE = 0
STT_OBJECT = 1
STT_FUNC = 2
STT_SECTION = 3
STT_FILE = 4

# --- PE constants ------------------------------------------------------------
IMAGE_FILE_MACHINE_SH4BE = 0x01A8
IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002
IMAGE_FILE_32BIT_MACHINE = 0x0104
IMAGE_FILE_DLL = 0x2000

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_MEM_DISCARDABLE = 0x02000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

IMAGE_SUBSYSTEM_WINDOWS_CE_GUI = 9

BRELOC_ABSOLUTE = 0
BRELOC_HIGHLOW = 3

PE32_DIR_EXPORT = 0
PE32_DIR_IMPORT = 1
PE32_DIR_BASERELOC = 5
PE32_OPTHDR_SIZE = 224  # 28 std + 68 windows + 16*8 data dirs
DOS_HDR_SIZE = 0x40

DEFAULT_BASE = 0x00400000
DEFAULT_COREDLL_BASE = 0x00800000
DEFAULT_IMPORT_MODULE = "COREDLL.DLL"


def le16(x):
    return struct.pack("<H", x & 0xFFFF)


def le32(x):
    return struct.pack("<I", x & 0xFFFFFFFF)


def align_up(x, a):
    return (x + a - 1) & ~(a - 1)


# --- ELF parsing -------------------------------------------------------------
class Section:
    __slots__ = ("name", "sh_type", "sh_flags", "sh_addr", "sh_offset",
                 "sh_size", "sh_link", "sh_info", "sh_addralign",
                 "sh_entsize", "data")


class Symbol:
    __slots__ = ("name", "value", "size", "info", "other", "shndx")

    @property
    def type(self):
        return self.info & 0xF

    @property
    def bind(self):
        return self.info >> 4


class Rela:
    __slots__ = ("r_offset", "r_sym", "r_type", "r_addend", "sec_name")


class Elf:
    def __init__(self, data):
        if data[:4] != b"\x7fELF":
            raise SystemExit("error: not an ELF file")
        # EI_CLASS (index 4): 1 = ELF32. EI_DATA (index 5): 2 = big-endian.
        if data[4] != 1:
            raise SystemExit("error: not a 32-bit ELF")
        if data[5] != 2:
            raise SystemExit("error: not a big-endian ELF (sh4a_nofpueb-elf is EB)")
        if data[6] != 1:
            raise SystemExit("error: unexpected ELF version")
        (self.e_type, self.e_machine, self.e_version, self.e_entry,
         self.e_phoff, self.e_shoff, self.e_flags, self.e_ehsize,
         self.e_phentsize, self.e_phnum, self.e_shentsize, self.e_shnum,
         self.e_shstrndx) = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
        if self.e_machine != EM_SH:
            raise SystemExit(f"error: e_machine {self.e_machine} != EM_SH(42)")
        self.data = data
        self.sections = []
        self.symbols = []
        self.relas = []
        self._parse()

    def _parse(self):
        d = self.data
        raw_secs = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
             sh_link, sh_info, sh_addralign, sh_entsize) = struct.unpack_from(
                ">IIIIIIIIII", d, off)
            raw_secs.append((sh_name, sh_type, sh_flags, sh_addr, sh_offset,
                             sh_size, sh_link, sh_info, sh_addralign,
                             sh_entsize))

        # section header string table
        shstr = raw_secs[self.e_shstrndx]
        strtab = d[shstr[4]:shstr[4] + shstr[5]]

        def cstr(blob, off):
            end = blob.find(b"\x00", off)
            return blob[off:end].decode("ascii", errors="replace")

        for (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
             sh_link, sh_info, sh_addralign, sh_entsize) in raw_secs:
            s = Section()
            s.name = cstr(strtab, sh_name)
            s.sh_type = sh_type
            s.sh_flags = sh_flags
            s.sh_addr = sh_addr
            s.sh_offset = sh_offset
            s.sh_size = sh_size
            s.sh_link = sh_link
            s.sh_info = sh_info
            s.sh_addralign = sh_addralign
            s.sh_entsize = sh_entsize
            if sh_type == SHT_NOBITS:
                s.data = b""
            else:
                s.data = d[sh_offset:sh_offset + sh_size]
            self.sections.append(s)

        # symbol table (first SHT_SYMTAB)
        for s in self.sections:
            if s.sh_type != SHT_SYMTAB:
                continue
            strsec = self.sections[s.sh_link]
            strtab = strsec.data
            count = s.sh_size // 16
            for i in range(count):
                (st_name, st_value, st_size, st_info, st_other,
                 st_shndx) = struct.unpack_from(">IIIBBH", d,
                                                s.sh_offset + i * 16)
                sym = Symbol()
                sym.name = cstr(strtab, st_name)
                sym.value = st_value
                sym.size = st_size
                sym.info = st_info
                sym.other = st_other
                sym.shndx = st_shndx
                self.symbols.append(sym)
            break

        # retained relocations (--emit-relocs): SHT_RELA sections
        for s in self.sections:
            if s.sh_type != SHT_RELA:
                continue
            for i in range(s.sh_size // 12):
                r_offset, r_info, r_addend = struct.unpack_from(
                    ">IIi", d, s.sh_offset + i * 12)
                r = Rela()
                r.r_offset = r_offset
                r.r_sym = r_info >> 8
                r.r_type = r_info & 0xFF
                r.r_addend = r_addend
                r.sec_name = s.name
                self.relas.append(r)


# --- helpers -----------------------------------------------------------------
def _unmangle(name):
    """Strip the target's user-symbol underscore prefix(es): the SH
    toolchain emits `_main`, so `__imp_coredll_puts` appears in the ELF as
    `___imp_coredll_puts`."""
    while name.startswith("_"):
        name = name[1:]
    return name


def collect_base_reloc_sites(elf, base):
    """Return sorted list of image-relative RVAs that hold absolute pointers."""
    sites = set()
    for r in elf.relas:
        if r.r_type != R_SH_DIR32:
            continue
        if r.r_sym >= len(elf.symbols):
            continue
        sym = elf.symbols[r.r_sym]
        if sym.shndx == SHN_UNDEF or sym.shndx >= SHN_LORESERVE:
            continue  # imports/absolutes: nothing to fix against image base
        # the site must live in an allocated section
        site_va = r.r_offset  # after --emit-relocs these are final VAs
        if site_va < base:
            continue
        sites.add(site_va - base)
    return sorted(sites)


def build_reloc_section(sites):
    """PE base-relocation blocks (little-endian per the PE spec; NTOS patches
    the *memory words* at each site big-endian). Returns bytes, always
    non-empty so the data directory is present even with zero fixup sites."""
    out = bytearray()
    if not sites:
        # one empty block: PageRVA=0, BlockSize=8
        out += le32(0) + le32(8)
        return bytes(out)

    pages = {}
    for rva in sites:
        pages.setdefault(rva >> 12, []).append(rva & 0xFFF)

    for page in sorted(pages):
        entries = []
        for ofs in sorted(pages[page]):
            entries.append((BRELOC_HIGHLOW << 12) | ofs)
        # pad to 4-byte boundary with ABSOLUTE entries
        while len(entries) % 2 != 0:
            entries.append(BRELOC_ABSOLUTE << 12)
        block = bytearray()
        block += le32(page << 12)
        block += le32(8 + 2 * len(entries))
        for e in entries:
            block += le16(e)
        out += block
    return bytes(out)


def build_export_section(exports, dll_name, sec_rva):
    """exports: ordered [(name, rva)]. Returns the .exports section bytes
    (little-endian IMAGE_EXPORT_DIRECTORY + tables, per the PE spec).

    Layout: ExportDirectory(40) | module name | ExportAddressTable(4n) |
    NamePointerTable(4n) | name strings | OrdinalTable(2n)."""
    n = len(exports)
    buf = bytearray(40)
    off = 40
    dll_name_off = off
    buf += dll_name.encode("ascii") + b"\x00"
    off += len(dll_name) + 1
    pad = align_up(off, 4) - off
    buf += b"\x00" * pad
    off += pad
    eat_off = off
    buf += b"".join(le32(rva) for _, rva in exports)
    off += 4 * n
    anp_off = off
    strings_off = off + 4 * n  # name strings follow the pointer table
    off = strings_off
    name_str_offs = []
    for name, _ in exports:
        name_str_offs.append(off)
        off += len(name) + 1
    buf += b"".join(le32(sec_rva + o) for o in name_str_offs)
    for name, _ in exports:
        buf += name.encode("ascii") + b"\x00"
    ord_off = off
    buf += b"".join(le16(i) for i in range(n))
    off += 2 * n

    struct.pack_into("<I", buf, 0, 0)                    # Characteristics
    struct.pack_into("<I", buf, 4, 0)                    # TimeDateStamp
    struct.pack_into("<H", buf, 8, 0)                    # MajorVersion
    struct.pack_into("<H", buf, 10, 0)                   # MinorVersion
    struct.pack_into("<I", buf, 12, sec_rva + dll_name_off)  # Name
    struct.pack_into("<I", buf, 16, 1)                   # Base (ordinal)
    struct.pack_into("<I", buf, 20, n)                   # NumberOfFunctions
    struct.pack_into("<I", buf, 24, n)                   # NumberOfNames
    struct.pack_into("<I", buf, 28, sec_rva + eat_off)   # AddressOfFunctions
    struct.pack_into("<I", buf, 32, sec_rva + anp_off)   # AddressOfNames
    struct.pack_into("<I", buf, 36, sec_rva + ord_off)   # ..NameOrdinals
    return bytes(buf)


def collect_import_slots(elf):
    """Return [(slot_rva, func_name)] sorted by slot, one per defined
    `__imp_<func>` symbol (the payload's .idata slots, laid out contiguously
    by the linker script)."""
    slots = []
    for s in elf.symbols:
        nm = _unmangle(s.name)
        if (nm.startswith("imp_") and s.value != 0
                and s.shndx != SHN_UNDEF and s.shndx < SHN_LORESERVE):
            slots.append((s.value, nm[len("imp_"):]))
    slots.sort()
    return slots


def build_import_section(groups, sec_rva):
    """Build one valid IMAGE_IMPORT_DESCRIPTOR per DLL, plus terminator.

    groups: [(module_name, [(slot_rva, import_name), ...]), ...].
    Each module's real .idata IAT slots must be contiguous in memory.
    OriginalFirstThunk points to a separate name RVA array in .imports;
    FirstThunk points to the writable, big-endian SH4 IAT in .idata.
    """
    if not groups:
        raise SystemExit("error: no imported symbols")
    buf = bytearray(20 * (len(groups) + 1))
    for di, (module, slots) in enumerate(groups):
        rvas = [r for r, _ in slots]
        if not slots or rvas != [rvas[0] + i * 4 for i in range(len(rvas))]:
            raise SystemExit(
                f"error: noncontiguous IAT slots for {module}: "
                f"{[hex(r) for r in rvas]} (keep NT_DEFINE calls grouped)")
        hn = []
        for _, name in slots:
            hn.append(len(buf))
            buf += le16(0) + name.encode("ascii") + b"\x00"
            if len(buf) & 1:
                buf += b"\x00"
        oft = len(buf)
        for offset in hn:
            buf += le32(sec_rva + offset)
        buf += le32(0)
        name_offset = len(buf)
        buf += module.encode("ascii") + b"\x00"
        struct.pack_into("<IIIII", buf, di * 20,
                         sec_rva + oft, 0, 0,
                         sec_rva + name_offset, rvas[0])
    return bytes(buf)



def emit_ntx(elf, base, exports=None, dll_name=None, import_module=None,
             image_is_dll=False, extra_imports=()):
    if elf.e_type != 2:
        raise SystemExit("error: input is not ET_EXEC (link with a linker "
                         "script / -Ttext so addresses are final)")

    # Allocatable sections in address order, mapped 1:1 (NTOS supports 8).
    alloc = [s for s in elf.sections
             if (s.sh_flags & SHF_ALLOC) and s.sh_size > 0
             and s.sh_type in (SHT_PROGBITS, SHT_NOBITS)]
    alloc.sort(key=lambda s: s.sh_addr)
    if any(s.sh_addr < base for s in alloc):
        raise SystemExit("error: section below image base; link at "
                         f"0x{base:08X} or pass --base")
    # Section RVAs must be SectionAlignment (0x200) multiples -- the linker
    # scripts do the spacing so linked absolute pointers stay valid.
    for s in alloc:
        if (s.sh_addr - base) % 0x200 != 0:
            raise SystemExit(
                f"error: section {s.name} at 0x{s.sh_addr:08X} is not "
                "0x200-aligned above the base (link with the ALIGN(0x200) "
                "payload linker scripts)")

    name_map = {
        ".text": ".text", ".rodata": ".rodata", ".data": ".data",
        ".bss": ".bss", ".sdata": ".data", ".sbss": ".bss",
        ".got": ".data",
    }

    coff_secs = []
    for s in alloc:
        coff_name = name_map.get(s.name, s.name.lstrip("."))[:8]
        if s.sh_type == SHT_NOBITS:
            chars = (IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                     IMAGE_SCN_MEM_WRITE)
        elif s.sh_flags & SHF_EXECINSTR:
            chars = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                     IMAGE_SCN_MEM_READ)
        else:
            chars = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                     IMAGE_SCN_MEM_WRITE)
        coff_secs.append({
            "name": coff_name,
            "rva": s.sh_addr - base,
            "vsize": s.sh_size,
            "raw": b"" if s.sh_type == SHT_NOBITS else s.data,
            "chars": chars,
        })

    # RVAs are 1:1 with the link addresses (rva = vma - base): the linker
    # scripts place the first section at ImageBase + 0x200 so every
    # absolute literal the linker wrote is already the runtime address.
    # The headers must fit below the first section (0x200 page).
    nsecs_tmp = len(coff_secs)
    headers_len_tmp = DOS_HDR_SIZE + 4 + 20 + PE32_OPTHDR_SIZE + nsecs_tmp * 40
    if headers_len_tmp > 0x200:
        raise SystemExit(
            f"error: {nsecs_tmp} sections need a 0x{headers_len_tmp:X}-byte "
            "header page, but sections start at RVA 0x200 (drop a section "
            "or shrink the payload)")
    sites = collect_base_reloc_sites(elf, base)
    reloc_data = build_reloc_section(sites)
    end_of_image = max((c["rva"] + c["vsize"] for c in coff_secs), default=0)
    extra_rva = align_up(end_of_image, 0x200)
    extra_dirs = {}

    if import_module:
        slots = [(r - base, n) for r, n in collect_import_slots(elf)]
        groups = [(import_module, [])]
        for prefix, module in extra_imports:
            groups.append((module, []))
        for rva, name in slots:
            matches = [(i, prefix) for i, (prefix, _) in enumerate(extra_imports)
                       if name.startswith(prefix)]
            if len(matches) > 1:
                raise SystemExit(f"error: multiple module-prefix matches for {name}")
            index = matches[0][0] + 1 if matches else 0
            groups[index][1].append((rva, name))
        groups = [(module, funcs) for module, funcs in groups if funcs]
        if not groups:
            raise SystemExit("error: import-module but no __imp_ symbols")
        imp_data = build_import_section(groups, extra_rva)
        coff_secs.append({
            "name": ".imports",
            "rva": extra_rva,
            "vsize": len(imp_data),
            "raw": imp_data,
            "chars": IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ,
        })
        extra_dirs[PE32_DIR_IMPORT] = (extra_rva, len(imp_data))
        extra_rva = align_up(extra_rva + len(imp_data), 0x200)

    if exports:
        exp_data = build_export_section(exports, dll_name, extra_rva)
        coff_secs.append({
            "name": ".exports",
            "rva": extra_rva,
            "vsize": len(exp_data),
            "raw": exp_data,
            "chars": IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ,
        })
        extra_dirs[PE32_DIR_EXPORT] = (extra_rva, len(exp_data))
        extra_rva = align_up(extra_rva + len(exp_data), 0x200)

    reloc_rva = extra_rva
    coff_secs.append({
        "name": ".reloc",
        "rva": reloc_rva,
        "vsize": len(reloc_data),
        "raw": reloc_data,
        "chars": (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                  IMAGE_SCN_MEM_DISCARDABLE),
    })

    if len(coff_secs) > 8:
        raise SystemExit(f"error: {len(coff_secs)} sections exceeds the "
                         "NTOS limit of 8")

    size_of_image = align_up(reloc_rva + len(reloc_data), 0x200)

    # The SH4 ELF toolchain prefixes C symbols with '_'.  The linker was
    # previously given -e,main, which does not exist: ld then silently
    # chose the start of .text (base+0x200) instead of _main.  That ran an
    # arbitrary helper function as the PE entry point (notably WINLAB and
    # MEMTOOL).  Select the actual main symbol, regardless of e_entry.
    defined = [s for s in elf.symbols
               if s.shndx != SHN_UNDEF and s.shndx < SHN_LORESERVE
               and s.type != STT_SECTION and s.name]
    main_symbols = [s for s in defined if _unmangle(s.name) == "main"]
    if len(main_symbols) != 1:
        raise SystemExit("error: expected exactly one defined _main "
                         f"symbol, found {len(main_symbols)}")
    entry_va = main_symbols[0].value
    if not any(sec.sh_addr <= entry_va < sec.sh_addr + sec.sh_size
               for sec in alloc if sec.sh_flags & SHF_EXECINSTR):
        raise SystemExit(f"error: main entry 0x{entry_va:08X} "
                         "is not inside an executable section")
    print(f"note: PE entry uses {main_symbols[0].name} "
          f"at 0x{entry_va:08X} (ELF header entry 0x{elf.e_entry:08X})")
    entry_rva = entry_va - base

    # --- file assembly -------------------------------------------------------
    # DOS header (0x40) + "PE\0\0" (4) + COFF header (20) + optional header
    # (224) + section headers (40*n), then file-aligned section raw data.
    nsecs = len(coff_secs)
    headers_len = DOS_HDR_SIZE + 4 + 20 + PE32_OPTHDR_SIZE + nsecs * 40
    raw_cursor = align_up(headers_len, 0x200)

    # DOS header: "MZ" + e_lfanew = 0x40 (pefile only needs magic+lfanew)
    dos = bytearray(DOS_HDR_SIZE)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x40)

    # optional header (PE32, little-endian per the PE spec)
    # offsets: 0 magic, 16 entry, 28 image base, 32 section align,
    # 36 file align, 56 SizeOfImage, 60 SizeOfHeaders, 68 Subsystem,
    # 72-95 stack/heap, 92 NumberOfRvaAndSizes? (92 LoaderFlags,
    # 96 NumberOfRvaAndSizes is PE32+; PE32: 88 LoaderFlags,
    # 92 NumberOfRvaAndSizes, 100 data dirs -- see below)
    opt = bytearray(PE32_OPTHDR_SIZE)
    struct.pack_into("<H", opt, 0, 0x010B)            # PE32 magic
    opt[2] = 9   # linker major (cosmetic)
    struct.pack_into("<I", opt, 4, sum(len(c["raw"]) for c in coff_secs
                                       if c["chars"] & IMAGE_SCN_CNT_CODE))
    struct.pack_into("<I", opt, 16, entry_rva)        # AddressOfEntryPoint
    struct.pack_into("<I", opt, 28, base)             # ImageBase
    struct.pack_into("<I", opt, 32, 0x200)            # SectionAlignment
    struct.pack_into("<I", opt, 36, 0x200)            # FileAlignment
    struct.pack_into("<H", opt, 40, 2)                # MajorOSVersion (WinCE)
    struct.pack_into("<H", opt, 48, 2)                # MajorSubsystemVersion
    struct.pack_into("<I", opt, 56, size_of_image)    # SizeOfImage
    struct.pack_into("<I", opt, 60, raw_cursor)       # SizeOfHeaders
    struct.pack_into("<I", opt, 64, 0)                # CheckSum
    struct.pack_into("<H", opt, 68, IMAGE_SUBSYSTEM_WINDOWS_CE_GUI)
    struct.pack_into("<H", opt, 70, 0)                # DllCharacteristics
    # stack/heap (PE32: 72..95)
    struct.pack_into("<I", opt, 72, 0x10000)          # SizeOfStackReserve
    struct.pack_into("<I", opt, 76, 0x1000)           # SizeOfStackCommit
    struct.pack_into("<I", opt, 80, 0x10000)          # SizeOfHeapReserve
    struct.pack_into("<I", opt, 84, 0x1000)           # SizeOfHeapCommit
    struct.pack_into("<I", opt, 88, 0)                # LoaderFlags
    struct.pack_into("<I", opt, 92, 16)               # NumberOfRvaAndSizes
    # PE32 data directories start at optional-header offset 96.
    struct.pack_into("<II", opt, 96 + PE32_DIR_EXPORT * 8,
                     *extra_dirs.get(PE32_DIR_EXPORT, (0, 0)))
    struct.pack_into("<II", opt, 96 + PE32_DIR_IMPORT * 8,
                     *extra_dirs.get(PE32_DIR_IMPORT, (0, 0)))
    struct.pack_into("<II", opt, 96 + PE32_DIR_BASERELOC * 8, reloc_rva,
                     len(reloc_data))

    # section headers + raw data
    headers = bytearray()
    body = bytearray()
    for c in coff_secs:
        raw_size = len(c["raw"])
        headers += c["name"].encode("ascii").ljust(8, b"\x00")
        headers += le32(c["vsize"])     # VirtualSize
        headers += le32(c["rva"])       # VirtualAddress
        headers += le32(raw_size)       # SizeOfRawData
        headers += le32(raw_cursor + len(body))  # PointerToRawData
        headers += le32(0)              # PointerToRelocations
        headers += le32(0)              # PointerToLinenumbers
        headers += le16(0)              # NumberOfRelocations
        headers += le16(0)              # NumberOfLinenumbers
        headers += le32(c["chars"])     # Characteristics
        body += c["raw"]
        # keep every section's file offset 0x200-aligned
        pad = align_up(raw_cursor + len(body), 0x200) - raw_cursor - len(body)
        body += b"\x00" * pad

    # COFF file header (little-endian)
    fh = bytearray()
    fh += le16(IMAGE_FILE_MACHINE_SH4BE)  # machine: SH4 BE
    fh += le16(nsecs)
    fh += le32(0)                         # timestamp
    fh += le32(0)                         # symbol table offset
    fh += le32(0)                         # symbol count
    fh += le16(PE32_OPTHDR_SIZE)
    chars = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_32BIT_MACHINE
    if image_is_dll:
        chars |= IMAGE_FILE_DLL
    fh += le16(chars)

    gap = raw_cursor - headers_len
    out = (bytes(dos) + b"PE\x00\x00" + bytes(fh) + bytes(opt) +
           bytes(headers) + b"\x00" * gap + bytes(body))
    assert len(out) == raw_cursor + len(body), (len(out), raw_cursor, len(body))
    return out, sites


# --- selftest ----------------------------------------------------------------
def make_synthetic_elf(base):
    """A minimal ET_EXEC: .text with one literal-pool absolute pointer and one
    R_SH_DIR32 retained reloc against a defined symbol."""
    text = bytearray()
    text += b"\x00\x09" * 4            # 8 bytes filler (nop)
    lit_off = len(text)
    text += be32_be(base + 0x100)      # .Lpool: .long some_symbol (+addend)

    secnames = b"\x00.text\x00.symtab\x00.strtab\x00.rela.text\x00"
    ehp = {}
    off = 52  # ELF header size

    def addsec(name_off, typ, flags, addr, data, link=0, entsize=0):
        nonlocal off
        s = struct.pack(">IIIIIIIIII", name_off, typ, flags, addr, off,
                        len(data), link, 0, 4, entsize)
        pos = off
        off += len(data)
        return s, pos, data

    shstr_off = 1  # ".text" at 1
    symtab_data = b"\x00" * 16  # null symbol
    # Sections sit at base+0x200 (the header page), like the real linker
    # scripts, so rva = vma - base and linked absolutes stay valid.
    # symbol 1: 'main' defined in .text (section 1) at base+0x200
    symtab_data += struct.pack(">IIIBBH", 1, base + 0x200, 0, (1 << 4) | 2, 0, 1)
    # symbol 2: 'coredll_puts' (export test) at base+0x400
    symtab_data += struct.pack(">IIIBBH", 6, 0x00400400, 0, (1 << 4) | 2, 0, 1)
    # symbol 3: '__imp_coredll_puts' (import test) at base+0x208
    symtab_data += struct.pack(">IIIBBH", 19, base + 0x208, 0, (1 << 4) | 1, 0, 1)
    strtab_data = (b"\x00main\x00coredll_puts\x00__imp_coredll_puts\x00")
    rela_data = struct.pack(">IIi", base + 0x200 + lit_off,
                            (1 << 8) | R_SH_DIR32, 0)

    body_layout = []
    # .text at base+0x200 (past the header page)
    s1, p1, d1 = addsec(shstr_off, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                        base + 0x200, bytes(text))
    body_layout.append((p1, d1))
    # .symtab / .strtab / .rela.text (not alloc)
    s2, p2, d2 = addsec(7, SHT_SYMTAB, 0, 0, symtab_data, link=3, entsize=16)
    body_layout.append((p2, d2))
    s3, p3, d3 = addsec(15, SHT_STRTAB, 0, 0, strtab_data)
    body_layout.append((p3, d3))
    s4, p4, d4 = addsec(23, SHT_RELA, 0, 0, rela_data, link=2, entsize=12)
    body_layout.append((p4, d4))

    eh = b"\x7fELF" + bytes([1, 2, 1, 0]) + b"\x00" * 8
    eh += struct.pack(">HHIIIIIHHHHHH",
                      2,          # e_type ET_EXEC
                      EM_SH, 1,
                      0,             # e_entry (tests the 'main' fallback)
                      0,          # e_phoff
                      0,          # e_shoff patched later
                      0, 52, 0, 0, 40, 6, 5)  # 6 sections (null + 5), shstrndx = 5
    # append shstrtab data too
    s5, p5, d5 = addsec(0, SHT_STRTAB, 0, 0, secnames)
    body_layout.append((p5, d5))

    blob = bytearray(eh)
    for pos, d in body_layout:
        assert pos == len(blob), (pos, len(blob))
        blob += d
    if len(blob) % 4:
        blob += b"\x00" * (4 - len(blob) % 4)
    e_shoff = len(blob)
    blob += b"\x00" * 40  # mandatory null section header (index 0)
    for s in (s1, s2, s3, s4, s5):
        blob += s
    struct.pack_into(">I", blob, 32, e_shoff)  # e_shoff (field 6 at 16+16)
    return bytes(blob)


def be32_be(x):
    return struct.pack(">I", x & 0xFFFFFFFF)


def sec_by_name(out, want):
    """(rva, size, raw_bytes) of a PE section in an emitted image, or None."""
    nsecs = struct.unpack_from("<H", out, 0x44 + 2)[0]
    optsz = struct.unpack_from("<H", out, 0x44 + 16)[0]
    table = 0x44 + 20 + optsz  # 0x44 = COFF file header (lfanew+4)
    for i in range(nsecs):
        h = out[table + i * 40:][:40]
        name = h[:8].rstrip(b"\x00").decode()
        if name == want:
            rva = struct.unpack_from("<I", h, 12)[0]
            rsz = struct.unpack_from("<I", h, 16)[0]
            roff = struct.unpack_from("<I", h, 20)[0]
            return rva, rsz, out[roff:roff + rsz]
    return None


def selftest():
    base = DEFAULT_BASE
    lit_off = 8  # synthetic .text: 8 bytes filler, then the 4-byte pool word
    elf = Elf(make_synthetic_elf(base))
    out, sites = emit_ntx(elf, base)
    off = 0x200  # sections live at base+off; rva = vma - base
    assert sites == [off + lit_off], sites

    # parse the PE back (little-endian, standard layout)
    assert out[:2] == b"MZ"
    lfanew = struct.unpack_from("<I", out, 0x3C)[0]
    assert out[lfanew:lfanew + 4] == b"PE\x00\x00"
    fh_at = lfanew + 4
    machine, nsecs = struct.unpack_from("<HH", out, fh_at)
    assert machine == IMAGE_FILE_MACHINE_SH4BE, hex(machine)
    optsz = struct.unpack_from("<H", out, fh_at + 16)[0]
    assert optsz == PE32_OPTHDR_SIZE
    opt_at = fh_at + 20
    entry = struct.unpack_from("<I", out, opt_at + 16)[0]   # AddressOfEntryPoint
    img_base = struct.unpack_from("<I", out, opt_at + 28)[0]  # ImageBase
    assert entry == off and img_base == base  # 'main' fallback
    soi = struct.unpack_from("<I", out, opt_at + 56)[0]
    assert soi == 0x600, hex(soi)  # header page + .text page + .reloc page
    dir5_rva, dir5_size = struct.unpack_from(
        "<II", out, opt_at + 96 + PE32_DIR_BASERELOC * 8)
    assert dir5_size >= 8

    # .reloc block structure (LE table, site = BE memory word)
    rva, rsz, blob = sec_by_name(out, ".reloc")
    page, bsz = struct.unpack_from("<II", blob, 0)
    assert page == ((off + lit_off) >> 12) << 12
    assert bsz == 8 + 4  # entry + ABSOLUTE pad to 4 bytes
    entry_word = struct.unpack_from("<H", blob, 8)[0]
    assert (entry_word >> 12) == BRELOC_HIGHLOW
    assert (entry_word & 0xFFF) == ((off + lit_off) & 0xFFF)
    assert struct.unpack_from("<H", blob, 10)[0] == 0

    # --- export + import directory round trip ------------------------------
    out2, _ = emit_ntx(elf, base,
                       exports=[("coredll_puts", 0x00400400 - 0x400000)],
                       dll_name=DEFAULT_IMPORT_MODULE,
                       import_module=DEFAULT_IMPORT_MODULE,
                       image_is_dll=True)
    er, esz, ebytes = sec_by_name(out2, ".exports")
    name_rva = struct.unpack_from("<I", ebytes, 12)[0]
    nfuncs, nnames = struct.unpack_from("<II", ebytes, 20)
    assert nfuncs == 1 and nnames == 1
    dllname = ebytes[name_rva - er:].split(b"\x00")[0]
    assert dllname == b"COREDLL.DLL", dllname
    anp = struct.unpack_from("<I", ebytes, 32)[0]
    name_ptr = struct.unpack_from("<I", ebytes, anp - er)[0]
    nm = ebytes[name_ptr - er:].split(b"\x00")[0]
    assert nm == b"coredll_puts", nm
    aord = struct.unpack_from("<I", ebytes, 36)[0]
    ord0 = struct.unpack_from("<H", ebytes, aord - er)[0]
    eat = struct.unpack_from("<I", ebytes, 28)[0]
    assert struct.unpack_from("<I", ebytes, eat - er + ord0 * 4)[0] \
        == 0x200 + off

    ir, isz, ibytes = sec_by_name(out2, ".imports")
    oft, tds, fwd, iname, ft = struct.unpack_from("<IIIII", ibytes, 0)
    assert tds == 0 and fwd == 0
    mn = ibytes[iname - ir:].split(b"\x00")[0]
    assert mn == b"COREDLL.DLL", mn
    hn0 = struct.unpack_from("<I", ibytes, oft - ir)[0]
    hint = struct.unpack_from("<H", ibytes, hn0 - ir)[0]
    fname = ibytes[hn0 - ir + 2:].split(b"\x00")[0]
    assert hint == 0 and fname == b"coredll_puts", (hint, fname)
    assert ft == off + 0x8, hex(ft)  # __imp_coredll_puts slot rva
    # Multi-DLL import directory: two module descriptors, two independent
    # terminated lookup arrays, and real (contiguous) IAT slot RVAs.
    multi = build_import_section([
        ("COREDLL.DLL", [(0x800, "BeginPaint"), (0x804, "EndPaint")]),
        ("XAMLUI.DLL", [(0x808, "xamlui_tile"),
                        (0x80C, "xamlui_switch")]),
    ], 0x1200)
    for index, module, first in [
        (0, b"COREDLL.DLL", 0x800),
        (1, b"XAMLUI.DLL", 0x808),
    ]:
        oft, _, _, name, ft = struct.unpack_from("<IIIII", multi, index * 20)
        assert ft == first
        assert multi[name - 0x1200:].split(b"\x00")[0] == module
        assert struct.unpack_from("<I", multi, oft - 0x1200 + 8)[0] == 0
    assert multi[40:60] == b"\x00" * 20  # terminator descriptor

    # dir 0/1 wired in the optional header; DLL characteristic set
    opt2_at = struct.unpack_from("<I", out2, 0x3C)[0] + 24
    d0r, d0s = struct.unpack_from("<II", out2, opt2_at + 96 + 0 * 8)
    d1r, d1s = struct.unpack_from("<II", out2, opt2_at + 96 + 1 * 8)
    assert (d0r, d0s) == (er, esz) and (d1r, d1s) == (ir, isz)
    fh_chars = struct.unpack_from("<H", out2, 0x44 + 18)[0]
    assert fh_chars & IMAGE_FILE_DLL

    # optional: validate with the real pefile module when available
    try:
        import pefile  # noqa: F401
    except ImportError:
        print("selftest OK: PE32 COFF parse, 1 HIGHLOW site "
              f"@ RVA 0x{lit_off:X}, export + import dir round-trip "
              "(pefile not installed -- skipped)")
        return
    pe = pefile.PE(data=out2)
    assert pe.FILE_HEADER.Machine == IMAGE_FILE_MACHINE_SH4BE
    assert pe.OPTIONAL_HEADER.AddressOfEntryPoint == off
    assert pe.OPTIONAL_HEADER.ImageBase == base
    assert pe.OPTIONAL_HEADER.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CE_GUI
    assert [e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols] == \
        ["coredll_puts"]
    assert [i.dll.decode() for i in pe.DIRECTORY_ENTRY_IMPORT] == \
        ["COREDLL.DLL"]
    assert [i.name.decode() for i in pe.DIRECTORY_ENTRY_IMPORT[0].imports] == \
        ["coredll_puts"]
    print("selftest OK: PE32 parse, pefile agrees (exports + imports)")
    return


def main():
    ap = argparse.ArgumentParser(description="ELF -> SH4BE PE32 image packer")
    ap.add_argument("input", nargs="?", help="linked big-endian ELF32")
    ap.add_argument("-o", "--output", help="output .EXE/.DLL image")
    ap.add_argument("--base", default=f"0x{DEFAULT_BASE:X}",
                    help=f"image base (default 0x{DEFAULT_BASE:X})")
    ap.add_argument("--exports", default=None,
                    help="comma-separated ELF symbol names to export via a "
                         "PE export directory (COREDLL-style)")
    ap.add_argument("--exports-file", default=None,
                    help="file with one export name per line (WinCE "
                         "coredll.def-style, ~2000 names); names not defined "
                         "in the ELF resolve to --stub-symbol")
    ap.add_argument("--stub-symbol", default=None,
                    help="ELF symbol whose RVA backs all undefined "
                         "--exports-file names (e.g. coredll_stub)")
    ap.add_argument("--dll-name", default=DEFAULT_IMPORT_MODULE,
                    help="module name recorded in the export directory "
                         "(default COREDLL.DLL)")
    ap.add_argument("--dll", action="store_true",
                    help="set the IMAGE_FILE_DLL characteristic")
    ap.add_argument("--import-module", default=None, metavar="MODULE",
                    help="build a PE import directory for MODULE from the "
                         "ELF's __imp_* symbols (e.g. COREDLL.DLL)")
    ap.add_argument("--extra-import-prefix", action="append", default=[],
                    metavar="PREFIX:MODULE",
                    help="route __imp_ names beginning with PREFIX to MODULE; "
                         "can be repeated for multiple import DLLs")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return

    if not args.input or not args.output:
        ap.error("need input ELF and -o output (or --selftest)")

    base = int(args.base, 0)
    with open(args.input, "rb") as f:
        elf = Elf(f.read())

    extra_imports = []
    for spec in args.extra_import_prefix:
        if ":" not in spec:
            ap.error("--extra-import-prefix expects PREFIX:MODULE")
        prefix, module = spec.split(":", 1)
        if not prefix or not module or len(module) > 24:
            ap.error("invalid --extra-import-prefix " + spec)
        extra_imports.append((prefix, module))
    if extra_imports and not args.import_module:
        ap.error("extra imports require --import-module")

    exports = []
    if args.exports:
        want = [n.strip() for n in args.exports.split(",") if n.strip()]
        defined = {_unmangle(s.name): s.value for s in elf.symbols
                   if s.shndx != SHN_UNDEF and s.shndx < SHN_LORESERVE}
        missing = [n for n in want if n not in defined]
        if missing:
            ap.error(f"--exports: symbols not found in the ELF: {missing}")
        exports = [(n, defined[n] - base) for n in want]

    if args.exports_file:
        want = [l.strip() for l in open(args.exports_file, encoding="utf-8",
                                        errors="replace")
                if l.strip() and not l.startswith(";")]
        defined = {_unmangle(s.name): s.value for s in elf.symbols
                   if s.shndx != SHN_UNDEF and s.shndx < SHN_LORESERVE}
        stub_rva = None
        if args.stub_symbol:
            sv = defined.get(_unmangle(args.stub_symbol))
            if sv is None:
                ap.error(f"--stub-symbol {args.stub_symbol} not in the ELF")
            stub_rva = sv - base
        have = {n for n, _ in exports}
        n_real = n_stub = 0
        for n in want:
            if n in have:
                continue
            sv = defined.get(n)
            if sv is not None:
                exports.append((n, sv - base))
                n_real += 1
            elif stub_rva is not None:
                exports.append((n, stub_rva))
                n_stub += 1
            else:
                ap.error(f"--exports-file: {n} not defined and no "
                         "--stub-symbol given")
        print(f"note: exports-file: {n_real} real, {n_stub} stubbed")

    out, sites = emit_ntx(elf, base, exports=exports,
                          dll_name=args.dll_name,
                          import_module=args.import_module,
                          image_is_dll=args.dll,
                          extra_imports=extra_imports)
    with open(args.output, "wb") as f:
        f.write(out)
    extra = ""
    if exports:
        extra += f", {len(exports)} export(s)"
    if args.import_module:
        dependencies = [args.import_module] + [
            module for _, module in extra_imports]
        extra += (f", {len(collect_import_slots(elf))} import(s) from "
                  + ", ".join(dependencies))
    print(f"wrote {args.output}: {len(out)} bytes, "
          f"{len(sites)} base-reloc site(s), base 0x{base:08X}{extra}")


if __name__ == "__main__":
    main()
