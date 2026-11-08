/* XAMLUI shares the WinCE-style preferred base 0x10000000 with COREDLL.
 * NTOS rehomes both modules into its P0 UTLB arena at load time. */
ENTRY(_main)
SECTIONS
{
  . = 0x10000200;
  .text : ALIGN(0x200) { *(.text*) }
  .rodata : ALIGN(0x200) { *(.rodata*) }
  .data : ALIGN(0x200) { *(.data*) *(.got*) }
  .idata : ALIGN(0x200) { *(.idata*) }
  .bss : ALIGN(0x200) { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
