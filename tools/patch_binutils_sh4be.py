#!/usr/bin/env python3
"""Prototype GNU BFD SH4BE PE/COFF target, applied to extracted binutils source.

This is a SOURCE PATCHER, not an ELF-to-PE packer. After applying, GNU
objcopy and GNU ld are responsible for writing the actual PE structures.
The patched SH4BE target uses big-endian data words and *little-endian*
COFF/PE headers; it has its own BFD vectors and ld PE emulation.

Requires a binutils 2.4x source tree. The patch intentionally fails closed
when a necessary anchor is absent. See docs/SH4BE_NATIVE_LINKER.md.
"""
import argparse
from pathlib import Path


def edit(root, file, fn):
    path = root / file
    old = path.read_text()
    new = fn(old)
    if new == old:
        raise RuntimeError(f"No patch applied to {file}")
    path.write_text(new)
    print(f"PATCH {file}")


def once(s, before, after, name):
    if before not in s:
        raise RuntimeError(f"Missing {name} patch anchor: {before!r}")
    return s.replace(before, after, 1)


def patch_coffcode(s):
    # Reuse the GNU BFD COFF vector definition verbatim, changing only
    # the per-target data ordering. No private reimplementation of PE.
    a = s.index("#define CREATE_LITTLE_COFF_TARGET_VEC(")
    b = s.find("\n#define ", a + 8)
    # In binutils 2.44, CREATE_LITTLE_COFF_TARGET_VEC is the *last*
    # macro in coffcode.h. Both layouts are valid.
    if b == -1:
        b = len(s)
    original = s[a:b]
    mixed = original.replace(
        "CREATE_LITTLE_COFF_TARGET_VEC", "CREATE_SH4BE_PE_TARGET_VEC", 1
    )
    for old, new in (
        ("BFD_ENDIAN_LITTLE,\t\t/* Data byte order is little.  */",
         "BFD_ENDIAN_BIG,\t\t/* Data byte order is big.  */"),
        ("bfd_getl64, bfd_getl_signed_64, bfd_putl64",
         "bfd_getb64, bfd_getb_signed_64, bfd_putb64"),
        ("bfd_getl32, bfd_getl_signed_32, bfd_putl32",
         "bfd_getb32, bfd_getb_signed_32, bfd_putb32"),
        ("bfd_getl16, bfd_getl_signed_16, bfd_putl16",
         "bfd_getb16, bfd_getb_signed_16, bfd_putb16"),
    ):
        # Only edit DATA functions, keep HEADER functions little endian.
        k = mixed.index("/* Header conversion functions.  */")
        start, end = mixed[:k], mixed[k:]
        if old not in start:
            raise RuntimeError("Missing mixed target conversion " + old)
        mixed = start.replace(old, new) + end
    if "BFD_ENDIAN_BIG" not in mixed:
        raise RuntimeError("Data order was not patched")
    return s[:a] + mixed + "\n" + s[a:]


def patch_coff_sh(s):
    a = """#ifdef COFF_WITH_PE
CREATE_LITTLE_COFF_TARGET_VEC (TARGET_SYM, TARGET_SHL_NAME,"""
    b = """#ifdef COFF_WITH_PE
#ifdef NTOS_SH4BE_PE
CREATE_SH4BE_PE_TARGET_VEC (TARGET_SYM, TARGET_SHL_NAME, BFD_IS_RELAXABLE,
                           SEC_CODE | SEC_DATA, '_', NULL, COFF_SWAP_TABLE);
#else
CREATE_LITTLE_COFF_TARGET_VEC (TARGET_SYM, TARGET_SHL_NAME,"""
    s = once(s, a, b, "coff SH PE vector")
    suffix = """SEC_CODE | SEC_DATA, '_', NULL, COFF_SWAP_TABLE);
#else
CREATE_LITTLE_COFF_TARGET_VEC"""
    if suffix not in s:
        # Find trailing #else in target vectors without depending on layout.
        k = s.index("CREATE_LITTLE_COFF_TARGET_VEC (TARGET_SYM, TARGET_SHL_NAME", s.index("#ifdef NTOS_SH4BE_PE"))
        next_else = s.index("\n#else", k)
        s = s[:next_else] + "\n#endif /* NTOS_SH4BE_PE */" + s[next_else:]
    else:
        s = s.replace(suffix, """SEC_CODE | SEC_DATA, '_', NULL, COFF_SWAP_TABLE);
#endif /* NTOS_SH4BE_PE */
#else
CREATE_LITTLE_COFF_TARGET_VEC""", 1)
    return s


def patch_sh_h(s):
    # GNU historically calls SH3 Windows CE 0x01A2. Provide a new private
    # SH4BE NTOS identity, avoiding the SH5 (0x01A8) collision.
    return once(
        s,
        "#define SH_ARCH_MAGIC_WINCE\t0x01a2",
        "#ifndef SH_ARCH_MAGIC_WINCE\n#define SH_ARCH_MAGIC_WINCE\t0x01a2\n#endif\n"
        "/* 0x01A7 is NTOS-private SH4BE, not assigned by Microsoft. */\n"
        "#define SH_ARCH_MAGIC_NTOS_SH4BE 0x01a7",
        "SH WinCE machine override",
    )


def pe_source(root, original_name, target_name, vec, tag):
    source = (root / "bfd" / original_name).read_text()
    source = source.replace(
        "#define TARGET_SHL_SYM sh_pe_le_vec", f"#define TARGET_SHL_SYM {vec}"
    ).replace(
        "#define TARGET_SHL_SYM sh_pei_le_vec", f"#define TARGET_SHL_SYM {vec}"
    ).replace(
        f'#define TARGET_SHL_NAME "{tag}"',
        f'#define TARGET_SHL_NAME "{target_name}"',
    )
    if f"#define TARGET_SHL_SYM {vec}" not in source:
        raise RuntimeError(f"Cannot rewrite {original_name} symbol")
    source = source.replace(
        '#include "coff-sh.c"',
        '#define NTOS_SH4BE_PE\n'
        '#define SH_ARCH_MAGIC_WINCE 0x01a7\n'
        '#include "coff-sh.c"'
    )
    (root / "bfd" / target_name.replace("-", "_")).parent.mkdir(exist_ok=True)
    path = root / "bfd" / ("pe-shbe.c" if tag == "pe-shl" else "pei-shbe.c")
    path.write_text(source)
    print("CREATE", path)


def patch_make_lists(s):
    s = s.replace("pe-sh.lo \\", "pe-sh.lo \\\n\tpe-shbe.lo \\", 1)
    s = s.replace("pei-sh.lo \\", "pei-sh.lo \\\n\tpei-shbe.lo \\", 1)
    s = s.replace("pe-sh.c \\", "pe-sh.c \\\n\tpe-shbe.c \\", 1)
    s = s.replace("pei-sh.c \\", "pei-sh.c \\\n\tpei-shbe.c \\", 1)
    if s.count("pe-shbe") < 2 or s.count("pei-shbe") < 2:
        raise RuntimeError("Failed to add new BFD object files")
    return s


def patch_targets(s):
    s = once(
        s,
        "extern const bfd_target sh_pe_le_vec;",
        "extern const bfd_target sh_pe_be_vec;\n"
        "extern const bfd_target sh_pei_be_vec;\n"
        "extern const bfd_target sh_pe_le_vec;",
        "BFD target symbols",
    )
    # Insert the new vectors in the target registry (not just externs).
    marker = "\t&sh_pe_le_vec,"
    return once(
        s, marker,
        "\t&sh_pe_be_vec,\n\t&sh_pei_be_vec,\n" + marker,
        "BFD target registry",
    )


def patch_config_bfd(s):
    return once(
        s,
        "  sh-*-pe)\n    targ_defvec=sh_pe_le_vec\n"
        '    targ_selvecs="sh_pe_le_vec sh_pei_le_vec"',
        "  sh-*-pe)\n    targ_defvec=sh_pe_be_vec\n"
        '    targ_selvecs="sh_pe_be_vec sh_pei_be_vec sh_pe_le_vec sh_pei_le_vec sh_coff_vec"',
        "configure SH PE target",
    )


def patch_ld_tgt(s):
    return once(
        s,
        "sh-*-pe)\t\t\ttarg_emul=shpe ;",
        'sh-*-pe)\t\t\ttarg_emul=shbepe\n'
        '\t\t\ttarg_extra_emuls="shpe" ;',
        "GNU ld SH PE emulation",
    )


def patch_ld_make(s):
    # GNU generates the emulation from ld/emulparams/shbepe.sh.
    return once(s, "\teshpe.c \\", "\teshbepe.c \\\n\teshpe.c \\", "ld emulation list")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path, help="extracted binutils source root")
    args = ap.parse_args()
    root = args.source.resolve()
    if not (root / "bfd" / "coff-sh.c").is_file():
        ap.error("expected binutils source tree")
    edit(root, "bfd/coffcode.h", patch_coffcode)
    edit(root, "bfd/coff-sh.c", patch_coff_sh)
    edit(root, "include/coff/sh.h", patch_sh_h)
    pe_source(root, "pe-sh.c", "pe-shbe", "sh_pe_be_vec", "pe-shl")
    pe_source(root, "pei-sh.c", "pei-shbe", "sh_pei_be_vec", "pei-shl")
    edit(root, "bfd/config.bfd", patch_config_bfd)
    edit(root, "bfd/targets.c", patch_targets)
    edit(root, "bfd/Makefile.am", patch_make_lists)
    edit(root, "bfd/Makefile.in", patch_make_lists)
    edit(root, "ld/configure.tgt", patch_ld_tgt)
    edit(root, "ld/Makefile.am", patch_ld_make)
    edit(root, "ld/Makefile.in", patch_ld_make)
    params = root / "ld" / "emulparams" / "shbepe.sh"
    params.write_text(
        "# Native NTOS SH4BE PE emulation; based on upstream shpe.sh\n"
        ". ${srcdir}/emulparams/shpe.sh\n"
        'OUTPUT_FORMAT="pei-shbe"\n'
    )
    print("CREATE", params)
    print("Prototype BFD SH4BE-PE backend configured. Test linker output before use.")


if __name__ == "__main__":
    main()
