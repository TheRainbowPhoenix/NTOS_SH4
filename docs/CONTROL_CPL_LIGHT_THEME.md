# Default light NTOS + native Control Panel applet test

## Build and installation

The standard build now contains the **pager-enabled** NTOS image and
the complete flat phone / WinCE-native payload tree:

```sh
git pull origin main
make clean
make -j4
make unified
```

A pre-existing checkout might contain old eager-loader objects;
`make clean` is a prudent one-time migration even though the Makefile
now tracks the MMU configuration flags and recompiles when they change.

Install `dist/NTOS.hh3` and the contents of `dist/` onto the
emulator/device storage, retaining the `Windows/` and `Apps/`
subdirectories. Do **not** substitute the original-CE
`dist_ce_demo/COREDLL.DLL` for the single native `dist/COREDLL.DLL`.

`dist/DEMO.CPL` is the link-visible PE module and
`dist/Windows/demo.cpl` is the installed discovery copy;
the two files are byte-for-byte identical.

## What to test in the emulator

1. Run `CONTROL.EXE`: it first paints a light-blue loading popup
   ("Scanning Windows/*.cpl") before filesystem enumeration.
2. Confirm a discovered `NTOS Demo` applet appears. That caption
   comes from the installed `DEMO.CPL`'s `CPlApplet`
   `CPL_NEWINQUIRE` metadata, not a hard-coded Control Panel item.
3. Select the tile and activate it with EXE or a second tap. The
   imported applet receives `CPL_INIT`, `CPL_GETCOUNT`,
   `CPL_DBLCLK`, and `CPL_EXIT`. Return to Control Panel.
4. Use View > Refresh or the top-right Refresh text. Confirm the
   progress popup appears again while enumerating storage.
5. Place another compatible `*.cpl` file in `Windows/`. It
   should be *listed*, but the prototype currently **cannot
   execute unknown CPLs**: typed dynamic PE `LoadLibrary` and
   `GetProcAddress` have not been implemented yet.
6. Test Start, Settings, the touch keyboard and bottom shell bar:
   backgrounds should be soft light-gray/blue, navigation ink should
   be legible, blue accent actions remain distinct, and the native
   WinCE title bar keeps its requested black/white chrome.

## Important limitations

The pager default removes the **eager 55-page rejection** from
normal PE loading, but demand faults remain dependent on SH7305
exception behavior. The user has validated FAULTTEST in an emulator;
the implementation is **not physical-device certified** and still
lacks file-backed page eviction and true reversible gint hardware
ownership.

The built-in `DEMO.CPL` uses the native SH4 GCC ABI at a fixed
four-word interface. Other original Microsoft SH4LE applets need
binary endian/relocation conversion and signature-aware MSVC ABI
bridges in addition to dynamic DLL loading. The original Microsoft
Control Panel is not yet running as a general CE applet host.
