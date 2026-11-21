# NTOSH / EX-word SH7305

Native `ntos_sh.d01`, 528×320, built by
[Exword CI](../.github/workflows/exword-d01.yml) with gint-exword.

## Install and use

The JP/CN packages install **NTOSH**. Add SH4BE WinCE PEs to the
`SYSROOT` directory **beside `ntos_sh.d01` in the installed `_CONTENT`
folder**. Transfer utilities may require manually copying that folder:

```text
NTOSH/_CONTENT/
  ntos_sh.d01
  SYSROOT/
    HELLO.EXE
    HELLOCORE.EXE
    UIAPP.EXE
    APPTEST/
      EXAMPLE.EXE
    DLL/
      XAMLUI.DLL
    COREDLL.DLL        # not required: COREDLL built into NTOSH
```

Expected native locations include
`\\drv0\NTOSH\_CONTENT\SYSROOT` and
`\\crd0\CASIO\NTOSH\_CONTENT\SYSROOT`. NTOSH searches actual
directory-entry spelling, including lower-case installer names; it still
offers raw volume browsing if SYSROOT is missing. LEFT/RIGHT selects disk,
UP/DOWN navigates (16-entry pages), EXE opens/runs, EXIT goes up or returns
at the SYSROOT root, and ACON/MENU quits.

Exe imports are resolved recursively: first alongside the requesting
EXE/DLL, then `SYSROOT\DLL`, then `SYSROOT`; the original `\fls0`
WinCE path maps to SYSROOT in the active disk. OS FS remains **read-only**.

The loader reuses `src/pe.hpp`, `pe_stream.hpp`, and `pe_rebase.hpp`
for file parsing and SH4BE relocations, and snaps imports to native
COREDLL implementations or a loaded DLL's named/ordinal exports.
Each dependency is initialized via `DllMain(DLL_PROCESS_ATTACH)` in
dependency order and detached on return. Supports up to 7 DLLs inside the
verified P1 image arena; unsupported forwarders, missing exports, fixed
non-rebasable images and too-large module graphs fail with a message.
It does **not** yet offer the CP400 P0 guest VBR or full GDI orientation.

## Exit

Use the EX-word return code `-2` and `touch_exword_quit()`, matching
Edelweiss. EXE memory, DLLs, guest handles and synthetic COREDLL state are
released before gint restores the EX-word OS. Hardware retest required.

## Memory / ROM

A PE is executable, not just readable data. The add-in does *not*
take ownership of firmware TLB slots. Executing in an unverified P1
address can overwrite live firmware RAM during syscall/file I/O.
Until EX-word exposes a proven, owned, pinned executable RAM lease
on more models, large PE execution is **limited to the two verified
Edelweiss ROM32A layouts**. Arbitrary ROM support must **not** be
enabled by bypassing that check; a generic RAM allocator is the next
required HAL milestone.

Successful cross-compilation cannot prove the guest won't fault.
Start with HELLO.EXE and a small known-import DLL test on backed-up
hardware. DLLs containing unsupported WinCE dependencies may still
fail safely. `NTOS.hh3` and the CP400 build remain independent.
