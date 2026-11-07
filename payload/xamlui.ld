/* XAMLUI.DLL preferred base, disjoint from COREDLL 0x00800000
 * and EXE 0x00400000. All sections 0x200-aligned SH4 BE. */
ENTRY(_main)
SECTIONS
{
  . = 0x00C00200;
  .text : ALIGN(0x200) { *(.text*) }
  .rodata : ALIGN(0x200) { *(.rodata*) }
  .data : ALIGN(0x200) { *(.data*) *(.got*) }
  .idata : ALIGN(0x200) { *(.idata*) }
  .bss : ALIGN(0x200) { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
