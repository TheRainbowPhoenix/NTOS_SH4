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
    "$OBJCOPY" -O coff-sh "$elf" "$TMP_DIR/$name.coff"
    "$OBJCOPY" --dump-section ".text=$TMP_DIR/$name.elf.text" "$elf"
    "$OBJCOPY" --dump-section ".text=$TMP_DIR/$name.coff.text" "$TMP_DIR/$name.coff"
    cmp "$TMP_DIR/$name.elf.text" "$TMP_DIR/$name.coff.text"
    echo "PASS GNU BFD coff-sh code round-trip: $name"
done
python3 tools/verify_pe_linkage.py \
    --text COREDLL.DLL "$TMP_DIR/coredll.elf.text" \
    --text XAMLUI.DLL "$TMP_DIR/xamlui.elf.text" \
    --text METROWIN.EXE "$TMP_DIR/metrowin.elf.text" \
    --text GUIWIN.EXE "$TMP_DIR/guiwin.elf.text" \
    --text MEMTOOL.EXE "$TMP_DIR/memtool.elf.text"
