#!/bin/sh
# Cross-check the compiler's native SH4BE executable bytes with:
# (1) GNU objcopy's big-endian SH COFF writer and
# (2) the PE images consumed by NTOS.
# PE import/export tables are checked independently by verify_pe_linkage.py.
set -eu
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM
OBJCOPY=${OBJCOPY:-sh4a_nofpueb-elf-objcopy}
for name in coredll xamlui metrowin guiwin memtool; do
    case "$name" in
        coredll) image=COREDLL.DLL ;;
        xamlui) image=XAMLUI.DLL ;;
        metrowin) image=METROWIN.EXE ;;
        guiwin) image=GUIWIN.EXE ;;
        memtool) image=MEMTOOL.EXE ;;
    esac
    elf="obj/payload/$name.elf"
    # A fully linked ELF may retain SH R_SH_DIR32 records solely because
    # --emit-relocs is needed by the PE compatibility shim. Strip that
    # debug/link-time metadata for a valid native COFF code-byte comparison.
    "$OBJCOPY" --strip-all "$elf" "$TMP_DIR/$name.stripped.elf"
    "$OBJCOPY" -O coff-sh "$TMP_DIR/$name.stripped.elf" "$TMP_DIR/$name.coff"
    "$OBJCOPY" --dump-section ".text=$TMP_DIR/$name.elf.text" "$elf"
    "$OBJCOPY" --dump-section ".text=$TMP_DIR/$name.coff.text" "$TMP_DIR/$name.coff"
    # The COFF writer is allowed to pad the end of a section to its
    # mandated file alignment. The original ELF bytes must be identical.
    python3 - "$TMP_DIR/$name.elf.text" "$TMP_DIR/$name.coff.text" <<'PY'
import sys
from pathlib import Path
elf, coff = (Path(p).read_bytes() for p in sys.argv[1:])
assert coff.startswith(elf), "BFD COFF conversion changed SH4BE code bytes"
assert len(coff) - len(elf) < 512, "unreasonable COFF section padding"
assert not any(coff[len(elf):]), "nonzero COFF section padding"
PY
    echo "PASS GNU BFD coff-sh code round-trip: $name"
done
python3 tools/verify_pe_linkage.py \
    --text COREDLL.DLL "$TMP_DIR/coredll.elf.text" \
    --text XAMLUI.DLL "$TMP_DIR/xamlui.elf.text" \
    --text METROWIN.EXE "$TMP_DIR/metrowin.elf.text" \
    --text GUIWIN.EXE "$TMP_DIR/guiwin.elf.text" \
    --text MEMTOOL.EXE "$TMP_DIR/memtool.elf.text"
