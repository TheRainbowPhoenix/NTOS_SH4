/* coredll.ld -- fixed layout for the NTOS stub COREDLL.
 *
 * Shares the WinCE-style preferred DLL base 0x10000000 with XAMLUI.
 * NTOS assigns disjoint P0 addresses and relocates SH4BE HIGHLOW words
 * before snapping imports. GNU ld emits the required relocation metadata. Sections are 0x200-aligned for the PE packer
 * (see payload.ld).
 */
ENTRY(main)
SECTIONS
{
  . = 0x10000200;

  .text : ALIGN(0x200) { *(.text*) }
  .rodata : ALIGN(0x200) { *(.rodata*) }
  .data : ALIGN(0x200) { *(.data*) *(.got*) }
  .bss : ALIGN(0x200) { *(.bss*) *(COMMON) }

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
