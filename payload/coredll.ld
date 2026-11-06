/* coredll.ld -- fixed layout for the NTOS stub COREDLL.
 *
 * Links at the preferred base 0x00800000. NTOS UTLB-maps the image at
 * exactly this address (WinCE XIP semantics: e32_vbase is honored, no
 * relocation is ever applied), so absolute constants and PC-relative code
 * both work with zero fixups.
 */
ENTRY(main)
SECTIONS
{
  . = 0x00800000;

  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) *(.got*) }
  .bss : { *(.bss*) *(COMMON) }

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
