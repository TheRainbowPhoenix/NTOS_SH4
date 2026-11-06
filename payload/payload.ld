/* payload.ld -- freestanding payload layout for .NTX files.
 *
 * Links at the NTOS-preferred base 0x00400000 (same base the MSVC -B2
 * route uses). Everything is PC-relative or goes through the base
 * relocation table elf2ntx.py builds from ld --emit-relocs output.
 */
ENTRY(main)
SECTIONS
{
  . = 0x00400000;

  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) *(.got*) }
  .idata : { *(.idata*) }   /* PE import address table slots (NTOS-patched) */
  .bss : { *(.bss*) *(COMMON) }

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
