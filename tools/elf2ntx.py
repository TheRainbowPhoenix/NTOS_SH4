#!/usr/bin/env python3
"""
elf2ntx.py -- COFF late linker/packer: big-endian SH4 ELF -> NTX EXE.

Takes a fully linked big-endian ELF32 (SuperH) executable -- built by
`sh4a_nofpueb-elf-ld --emit-relocs` from freestanding objects -- and emits a
big-endian COFF EXE (machine 0x01A8, "SH4 BE" MIPS-style, per our patched
shasm) with a PE32 optional header and a PE `.reloc` base-relocation table.

NTOS (this repo) copy-loads the .NTX anywhere in the HH3 heap and applies
delta = actual_base - preferred_base to every HIGHLOW entry, exactly like a
WinCE loader would. Absolute addresses must therefore be present in the file:
link with `--emit-relocs` so ld retains the relocation sites; every R_SH_DIR32
site whose symbol is defined inside the image becomes a HIGHLOW entry.

Usage:
    python3 tools/elf2ntx.py payload.elf -o dist/HELLO.NTX [--base 0x400000]

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

STYP_TEXT = 0x20
STYP_DATA = 0x40
STYP_BSS = 0x80

F_EXEC = 0x0002
F_RELFLG = 0x0001  # "relocation info stripped" -> NTOS uses PE .reloc instead
F_AR32WR = 0x0100

BRELOC_ABSOLUTE = 0
BRELOC_HIGHLOW = 3

PE32_DIR_BASERELOC = 5
PE32_OPTHDR_SIZE = 224  # 28 std + 68 windows + 16*8 data dirs

DEFAULT_BASE = 0x00400000


def be16(x):
    return struct.pack(">H", x & 0xFFFF)


def be32(x):
    return struct.pack(">I", x & 0xFFFFFFFF)


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
        if data[4] != 2:
            raise SystemExit("error: not a big-endian ELF (sh4a_nofpueb-elf is EB)")
        if data[5] != 1:
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


# --- COFF emission -----------------------------------------------------------
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
    """PE base-relocation blocks (written big-endian; NTOS reads the loaded
    image with BE readers). Returns bytes, always non-empty so the PE data
    directory is present even for images with zero fixup sites."""
    out = bytearray()
    if not sites:
        # one empty block: PageRVA=0, BlockSize=8
        out += be32(0) + be32(8)
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
        block += be32(page << 12)
        block += be32(8 + 2 * len(entries))
        for e in entries:
            block += be16(e)
        out += block
    return bytes(out)


def emit_ntx(elf, base):
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

    name_map = {
        ".text": ".text", ".rodata": ".rodata", ".data": ".data",
        ".bss": ".bss", ".sdata": ".data", ".sbss": ".bss",
        ".got": ".data",
    }

    coff_secs = []
    for s in alloc:
        coff_name = name_map.get(s.name, s.name.lstrip("."))[:8]
        if s.sh_type == SHT_NOBITS:
            styp = STYP_BSS
        elif s.sh_flags & SHF_EXECINSTR:
            styp = STYP_TEXT
        else:
            styp = STYP_DATA
        coff_secs.append({
            "name": coff_name,
            "rva": s.sh_addr - base,
            "vsize": s.sh_size,
            "raw": b"" if styp == STYP_BSS else s.data,
            "flags": styp,
        })

    # base relocation sites + .reloc section placed after everything else
    sites = collect_base_reloc_sites(elf, base)
    reloc_data = build_reloc_section(sites)
    end_of_image = max((c["rva"] + c["vsize"] for c in coff_secs), default=0)
    reloc_rva = align_up(end_of_image, 4)
    coff_secs.append({
        "name": ".reloc",
        "rva": reloc_rva,
        "vsize": len(reloc_data),
        "raw": reloc_data,
        "flags": STYP_DATA,
    })

    if len(coff_secs) > 8:
        raise SystemExit(f"error: {len(coff_secs)} sections exceeds the "
                         "NTOS limit of 8")

    size_of_image = align_up(reloc_rva + len(reloc_data), 0x1000)
    entry_rva = elf.e_entry - base
    if entry_rva < 0:
        raise SystemExit("error: entry point below image base")

    # --- file assembly -------------------------------------------------------
    # COFF header (20) + optional header (224) + section headers (40*n),
    # then file-aligned section raw data.
    nsecs = len(coff_secs)
    headers_len = 20 + PE32_OPTHDR_SIZE + nsecs * 40
    raw_cursor = align_up(headers_len, 0x200)

    # optional header (PE32, all fields big-endian like the rest of the file)
    # PE32 field offsets: 0 magic, 16 entry, 28 image base, 32 section
    # align, 36 file align, 56 SizeOfImage, 60 SizeOfHeaders, 64 CheckSum,
    # 68 Subsystem, 70 DllCharacteristics, 72-95 stack/heap, 96 datadirs
    opt = bytearray(PE32_OPTHDR_SIZE)
    struct.pack_into(">H", opt, 0, 0x010B)            # PE32 magic
    opt[2] = 9   # linker major (cosmetic)
    struct.pack_into(">I", opt, 4, sum(len(c["raw"]) for c in coff_secs
                                       if c["flags"] & STYP_TEXT))  # text size
    struct.pack_into(">I", opt, 16, entry_rva)        # AddressOfEntryPoint
    struct.pack_into(">I", opt, 28, base)             # ImageBase
    struct.pack_into(">I", opt, 32, 0x1000)           # SectionAlignment
    struct.pack_into(">I", opt, 36, 0x200)            # FileAlignment
    struct.pack_into(">I", opt, 56, size_of_image)    # SizeOfImage
    struct.pack_into(">I", opt, 60, raw_cursor)       # SizeOfHeaders
    struct.pack_into(">I", opt, 64, 0)                # CheckSum
    struct.pack_into(">H", opt, 68, 9)                # Subsystem = WinCE
    struct.pack_into(">H", opt, 70, 0)                # DllCharacteristics
    # stack/heap (PE32: 72..95)
    struct.pack_into(">I", opt, 72, 0x10000)          # StackReserve
    struct.pack_into(">I", opt, 76, 0x1000)           # StackCommit
    struct.pack_into(">I", opt, 80, 0x10000)          # HeapReserve
    struct.pack_into(">I", opt, 84, 0x1000)           # HeapCommit
    struct.pack_into(">I", opt, 92, 0)                # LoaderFlags
    struct.pack_into(">I", opt, 96, 16)               # NumberOfRvaAndSizes
    # data directory #5 = base relocation table (offset 96 + 5*8)
    struct.pack_into(">II", opt, 96 + 5 * 8, reloc_rva, len(reloc_data))

    # section headers + raw data
    headers = bytearray()
    body = bytearray()
    for c in coff_secs:
        raw_size = len(c["raw"])
        headers += c["name"].encode("ascii").ljust(8, b"\x00")
        headers += be32(c["vsize"])     # VirtualSize
        headers += be32(c["rva"])       # VirtualAddress
        headers += be32(raw_size)       # SizeOfRawData
        headers += be32(raw_cursor + len(body))  # PointerToRawData
        headers += be32(0)              # PointerToRelocations
        headers += be32(0)              # PointerToLinenumbers
        headers += be16(0)              # NumberOfRelocations
        headers += be16(0)              # NumberOfLinenumbers
        headers += be32(c["flags"])     # Characteristics
        body += c["raw"]
        # keep every section's file offset 0x200-aligned
        pad = align_up(raw_cursor + len(body), 0x200) - raw_cursor - len(body)
        body += b"\x00" * pad

    # COFF file header
    fh = bytearray()
    fh += be16(0x01A8)                    # machine: SH4 BE
    fh += be16(nsecs)
    fh += be32(0)                         # timestamp
    fh += be32(0)                         # symbol table offset
    fh += be32(0)                         # symbol count
    fh += be16(PE32_OPTHDR_SIZE)
    fh += be16(F_EXEC | F_RELFLG | F_AR32WR)

    gap = raw_cursor - headers_len
    out = bytes(fh) + bytes(opt) + bytes(headers) + b"\x00" * gap + bytes(body)
    assert len(out) == raw_cursor + len(body), (len(out), raw_cursor, len(body))
    return out, sites


# --- selftest ----------------------------------------------------------------
def make_synthetic_elf(base):
    """A minimal ET_EXEC: .text with one literal-pool absolute pointer and one
    R_SH_DIR32 retained reloc against a defined symbol."""
    text = bytearray()
    text += b"\x00\x09" * 4            # 8 bytes filler (nop)
    lit_off = len(text)
    text += be32(base + 0x100)         # .Lpool: .long some_symbol (+addend)

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
    # symbol 1: 'foo' defined in .text (section 1) at value 0x100
    symtab_data += struct.pack(">IIIBBH", 7, 0x100, 0, (1 << 4) | 2, 0, 1)
    strtab_data = b"\x00foo\x00"
    rela_data = struct.pack(">IIi", base + lit_off, (1 << 8) | R_SH_DIR32, 0)

    body_layout = []
    # .text at base
    s1, p1, d1 = addsec(shstr_off, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                        base, bytes(text))
    body_layout.append((p1, d1))
    # .symtab / .strtab / .rela.text (not alloc)
    s2, p2, d2 = addsec(7, SHT_SYMTAB, 0, 0, symtab_data, link=3, entsize=16)
    body_layout.append((p2, d2))
    s3, p3, d3 = addsec(15, SHT_STRTAB, 0, 0, strtab_data)
    body_layout.append((p3, d3))
    s4, p4, d4 = addsec(23, SHT_RELA, 0, 0, rela_data, link=2, entsize=12)
    body_layout.append((p4, d4))

    eh = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\x00" * 8
    eh += struct.pack(">HHIIIIIHHHHHH",
                      2,          # e_type ET_EXEC
                      EM_SH, 1,
                      base,       # e_entry
                      0,          # e_phoff
                      0,          # e_shoff patched later
                      0, 52, 0, 0, 40, 5, 4)  # 5 sections, shstrndx = index of shstrtab
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
    for s in (s1, s2, s3, s4, s5):
        blob += s
    struct.pack_into(">I", blob, 32, e_shoff)  # e_shoff (field 6 at 16+16)
    return bytes(blob)


def selftest():
    base = DEFAULT_BASE
    lit_off = 8  # synthetic .text: 8 bytes filler, then the 4-byte pool word
    elf = Elf(make_synthetic_elf(base))
    out, sites = emit_ntx(elf, base)
    assert sites == [lit_off], sites
    # parse the COFF back
    machine, nsecs = struct.unpack_from(">HH", out, 0)
    assert machine == 0x01A8, hex(machine)
    opt = out[20:20 + PE32_OPTHDR_SIZE]
    entry = struct.unpack_from(">I", opt, 16)[0]      # AddressOfEntryPoint
    img_base = struct.unpack_from(">I", opt, 28)[0]   # ImageBase
    assert entry == 0 and img_base == base
    dir5_rva, dir5_size = struct.unpack_from(">II", opt, 96 + 5 * 8)
    assert dir5_size >= 8
    # find .reloc section raw content and verify block structure
    for i in range(nsecs):
        h = out[20 + PE32_OPTHDR_SIZE + i * 40:][:40]
        name = h[:8].rstrip(b"\x00").decode()
        if name == ".reloc":
            raw_off = struct.unpack_from(">I", h, 20)[0]
            raw_sz = struct.unpack_from(">I", h, 16)[0]
            blob = out[raw_off:raw_off + raw_sz]
            page, bsz = struct.unpack_from(">II", blob, 0)
            assert page == (lit_off >> 12) << 12
            assert bsz == 8 + 4  # entry + ABSOLUTE pad to 4 bytes
            entry_word = struct.unpack_from(">H", blob, 8)[0]
            assert (entry_word >> 12) == BRELOC_HIGHLOW
            assert (entry_word & 0xFFF) == (lit_off & 0xFFF)
            assert struct.unpack_from(">H", blob, 10)[0] == 0
            print("selftest OK: 1 section-parsed COFF, 1 HIGHLOW site "
                  f"@ RVA 0x{lit_off:X}")
            return
    raise SystemExit("selftest FAIL: .reloc section missing")


def main():
    ap = argparse.ArgumentParser(description="ELF -> SH4BE COFF NTX packer")
    ap.add_argument("input", nargs="?", help="linked big-endian ELF32")
    ap.add_argument("-o", "--output", help="output .NTX file")
    ap.add_argument("--base", default=f"0x{DEFAULT_BASE:X}",
                    help=f"image base (default 0x{DEFAULT_BASE:X})")
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
    out, sites = emit_ntx(elf, base)
    with open(args.output, "wb") as f:
        f.write(out)
    print(f"wrote {args.output}: {len(out)} bytes, "
          f"{len(sites)} base-reloc site(s), base 0x{base:08X}")


if __name__ == "__main__":
    main()
