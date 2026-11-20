/* HH3 64 KiB firmware window: 0x8CFF0000..0x8D000000.
 *
 * The SDK's static lib still contributes ~6 KiB of allocated .eh_frame
 * metadata even when *our* translation units use -fno-unwind-tables.
 * NTOS is a freestanding no-C++-exceptions app and does not unwind any
 * C++ frame. Discard ONLY unwind metadata in THIS executable; do not
 * alter .text/.rodata/.data/.bss mappings, ABI, SDK or EXE loader.
 *
 * INSERT AFTER .text preserves the SDK/toolchain default ELF script,
 * but orders this special /DISCARD/ ahead of default .eh_frame matching.
 * CI validates the stripped ELF's actual PT_LOAD end, not this intent.
 */
SECTIONS
{
  /DISCARD/ : { *(.eh_frame) *(.eh_frame.*) *(.gcc_except_table) *(.gcc_except_table.*) }
}
INSERT AFTER .text;
