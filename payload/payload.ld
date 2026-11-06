/* payload.ld -- freestanding payload layout for .NTX files.
 *
 * Links at the NTOS-preferred base 0x00400200 (same base the MSVC -B2
 * route uses). Sections are 0x200-aligned so the PE packer (elf2ntx.py)
 * can map them 1:1 onto PE section RVAs with SectionAlignment 0x200 --
 * the PE spec requires section VirtualAddresses to be SectionAlignment
 * multiples, and re-spacing here (instead of in the packer) keeps every
 * linked absolute pointer valid.
 */
ENTRY(main)
SECTIONS
{
  . = 0x00400200;

  .text : ALIGN(0x200) { *(.text*) }
  .rodata : ALIGN(0x200) { *(.rodata*) }
  .data : ALIGN(0x200) { *(.data*) *(.got*) }
  .idata : ALIGN(0x200) { *(.idata*) }  /* PE import address table slots */
  .bss : ALIGN(0x200) { *(.bss*) *(COMMON) }

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
