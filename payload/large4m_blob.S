/* Four million initialized bytes, deliberately not BSS, so both PE file
 * length and mapped image are >4 MiB. Compressed/sparse files do not qualify.
 * Large probe is built on demand and never installed by package_device.py. */
    .section .rodata.large,"a"
    .balign 4096
    .global _ntos_large_blob
_ntos_large_blob:
    .fill 4194304,1,0xA5
    .global _ntos_large_blob_end
_ntos_large_blob_end:
