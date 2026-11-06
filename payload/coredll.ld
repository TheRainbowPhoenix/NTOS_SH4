/* coredll.ld -- fixed layout for the NTOS stub COREDLL.
 *
 * Links at the preferred base 0x00800000. NTOS UTLB-maps the image at
 * exactly this address (WinCE XIP semantics: e32_vbase is honored, no
 * relocation is ever applied), so absolute constants and PC-relative code
 * both work with zero fixups. Sections are 0x200-aligned for the PE packer
 * (see payload.ld).
 */
ENTRY(main)
SECTIONS
{
  . = 0x00800000;

  .text : ALIGN(0x200) { *(.text*) }
  .rodata : ALIGN(0x200) { *(.rodata*) }
  .data : ALIGN(0x200) { *(.data*) *(.got*) }
  .bss : ALIGN(0x200) { *(.bss*) *(COMMON) }

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
