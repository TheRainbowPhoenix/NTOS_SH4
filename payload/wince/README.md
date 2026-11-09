# WinCE common-controls port workspace

The reference is the local SH4-LE WinCE COMMCTRL.DLL and its matching COREDLL,
CONTROL.EXE, TOOLHELP.DLL and text/debug SHELL.EXE. Reference hashes and exact export
names/ordinals are in `abi/`. The normal phone payload and ABI experiment files are
not changed by this workspace.

## Current implementation

The compilable C code is organized as:

* `common/containers.c`: shared DPA/DSA storage, growth, insertion, deletion, cloning,
  stable sorting, search, callbacks and wide-string ownership. Anonymous container
  exports were identified against COREDLL decompilation. Target layout assertions
  check the 32-bit CE structures. DPA unsorted search matches this reference's
  last-match behavior.
* `common/control_model.c` and `control_items.c`: owned list items/columns, selection,
  tree nodes, headers, tabs, toolbar buttons, progress/track/up-down ranges, status
  text and basic rendering hooks. Tree handles remain stable across growth, and
  subtree deletion does not use recursive calls.
* `commctrl/`: command bars/child ownership and property-page lifetimes.
* `coredll/`: CE local-memory accounting, cursor lookup, PE string-resource loading,
  simple integer/UTF-16 formatting, image-list metadata, window/control message
  routing and typed failure stubs for the remaining Control Panel dependencies.
* `include/`: compact shared types, messages/item layouts and control interfaces.

`COMMCTRL.DLL` retains all **84 export slots**. **76 slots** have dedicated bodies,
including six identified anonymous container exports. This count includes partial
models and deliberate stubs; it does NOT mean 76 fully compatible APIs. Eight private
exports still use the diagnostic unsupported handler. `dist_ce_demo/port-status.json`
classifies every export and lists the remaining gaps.

All **73 COREDLL imports of the reference control.exe** now have typed functions.
LoadCursorW recognizes the system arrow; string resources are read from the actual
little-endian PE resource tree and emitted as native UTF-16. LocalSize/ReAlloc track
owned allocation sizes, including CE callers using 0x42 flags. Dependent-library loading,
registry writes, shell execution, menu UI and image pixel decoding still fail or
have limited metadata behavior. The registry stub reports missing values rather
than pretending to return initialized data.

## Full decompilation reference

All **1,414 recognized COMMCTRL functions** were exported successfully to
`obj/wince/commctrl-decomp/`, with an address/name index and completion marker.
This is Ghidra pseudocode for study, not a compilable full port. It is included in
the local source archive under `reference/`. ExportCommctrlC.java recreates it:
open `/recreate/WinPhone/SH4-LE/commctrl.dll` and pass an output directory as the first
script argument. It writes asynchronously; wait for complete.txt or inspect error.txt.

The local CE source trees contain image-list code and private controls headers, but
not a complete COMMCTRL source/header set. The reference decompilation supplements
those sources. No automatic copy of Microsoft PRIVATE source is a build input.

## Explicit remaining work

This is **not yet a complete behavioral COMMCTRL port**. PropertySheetW returns -1
until there is a modal dialog engine. Full menu loading/rendering, bitmap/icon
copies, tooltips, rebar/date controls, notifications, callback/subclass behavior,
keyboard navigation and many control messages remain unfinished. Private exports
55 and 77-83 still require investigation. Some command-bar operations currently
record metadata or acknowledge requests without rendering the original UI.

The CE profile now has explicit MSVC scalar/home-space export adapters for 180
COREDLL and 76 COMMCTRL slots. The signature manifests are reviewable JSON;
unknown exports remain diagnostic failures. Callback calls use upstream's typed
GCC-to-MSVC gate, including WNDPROC and container comparisons/enumeration.
Integer varargs are read from MSVC word slots, not a GCC va_list. Floating-point,
aggregate-by-value and unclassified signatures remain unsupported. Private
CeNativeAlloc/CeNativeFree/CeNativePuts brokers retain the native ABI; phone DLLs
and their native importers are intentionally a separate profile.

The upstream five-function COREDLLABI.EXE works with this profile too. Full firmware
emulation returns 0xAB1C0500, including the fifth stack argument check. Original
Control Panel now draws its resource-derived title and background: CE LoadStringW
with a null buffer returns a native UTF-16 resource pointer. The sample Windows/demo.cpl supplies one NTOS Demo applet. Clear returns from the app.

NTOS console output uses 40 columns and 40 rows, clears complete rows and scrolls
instead of wrapping over old text. It clears the loader log on first app output.

## Verification

Native tests check:

* DSA alias insertion, clone independence, zeroed gaps and allocation failures;
* DPA sorting/search/deletion and callback termination;
* text ownership, list selection, tab selection, toolbar state, range clamping;
* tree parent relationships, subtree cleanup and a 256-level tree;
* constructor failure cleanup, stale-handle lookups and property-page ownership;
* DLL detach clears control/image/local-heap registries, with dependent DLLs first;
* malformed PE resource offsets, string truncation and formatted UTF-16 output.

All test allocations must be released. The suite passes strict GCC diagnostics,
GCC's analyzer, and Linux AddressSanitizer/UndefinedBehaviorSanitizer with leak
checking. Clang-format checks cover all implementation/test C files and headers.
SH4 builds use -Wall -Wextra -Werror, with only unused static functions suppressed
because the core includes the existing larger COREDLL implementation.

The clemu machine-code harness verifies original converted Toolhelp walkers and
ReadProcessMemory through both ABI layers, all six COMMCTRL ordinal imports used
by Control Panel, and the original shell help/exit flow after DLL rebasing.
Converted control.exe reaches window creation, child controls, ShowWindow,
UpdateWindow and GetMessageW before the scripted Clear event returns it. This is
complemented by tools/clemu_wince_smoke.py: real firmware, SDK entry/startup,
Fugue file access, NTOS loader and MMU, plus framebuffer screenshots. Its direct
HH3 mode bypasses the interactive HH3 picker/import binder. Physical Clear did
not produce an event in this bypassed boot; exit is verified separately using
the machine-code API harness. It does not certify the normal manual picker; no .cpl applets are supplied.

## Build and check

From the repository root, with the existing compiler container:

```powershell
python -m pip install --target obj/wince/format-tool -r payload/wince/format-requirements.txt
python payload/wince/check.py --native --analyze --cc D:/TDM-GCC-64/bin/gcc.exe
# Apply the local clang-format policy:
python payload/wince/check.py --format --cc D:/TDM-GCC-64/bin/gcc.exe
python payload/wince/build_compat.py
python payload/wince/audit_port.py
$env:PYTHONPATH='C:/Python39/Lib/site-packages'
& C:/Users/Phoebe/Documents/git/clemu-310c/venv/Scripts/python.exe payload/wince/runtime_check.py --control
& C:/Users/Phoebe/Documents/git/clemu-310c/venv/Scripts/python.exe payload/wince/runtime_check.py --shell --ntos-bases
```

The pre-existing local converted reference EXEs/Toolhelp remain inputs. To recreate
those, run DumpCeInventory.java (after SeedCeCommands.java on shell), then:

```powershell
python payload/wince/transcode.py --reference C:/Users/Phoebe/Documents/git/pythonextra-template/NK-extracted/fs/Windows --inventory C:/Users/Phoebe/Documents/git/pythonextra-template/ce-ghidra-inventory
```

PE metadata stays little endian; known native data/instructions become big endian.
The full binary `commctrl.full-candidate.dll` remains an unverified offline candidate,
not the compiled C port. Its 87 pages exceed the current 55 resident-image budget.

Outputs stay in ignored `obj/wince` and `dist_ce_demo`. Keep the experimental CE
COREDLL separate from the shipping phone bundle. Sources and converted binaries
have not been pushed to GitHub.

## Manual clemu profile and visual checks

The local clemu ntos_launch.py now accepts `--wince`, selecting dist_ce_demo and a
separate fugue_vfs_wince volume. This avoids reseeding the native phone COREDLL
or the five-export ABI-only pilot over the CE ordinal profile.

```powershell
# From C:/Users/Phoebe/Documents/git/clemu-310c:
venv/Scripts/python.exe ntos_launch.py --wince
# From NTOS_SH4, isolated full-system screenshot test:
$env:PYTHONPATH=''
& C:/Users/Phoebe/Documents/git/clemu-310c/venv/Scripts/python.exe tools/clemu_wince_smoke.py --direct-hh3 --app control
# Also supports --app shell and --app abi.
```

After rebuilding NTOS, copy dist/NTOS.hh3 into dist_ce_demo/NTOS.hh3 before
using --wince. The standard ntos_launch.py command still selects the phone bundle.

## Sample applet

`demo/demo_cpl.c` builds `dist_ce_demo/Windows/demo.cpl` (2,560 bytes), a genuine
import-free SH4-BE PE DLL exporting MSVC-compatible CPlApplet. It reports one
NTOS Demo applet via CPL_INIT, CPL_GETCOUNT and CPL_NEWINQUIRE (468-byte Unicode
NEWCPLINFO), with synthetic icon metadata. Its activation callback draws a small
hello page through NTOS gates; launching ctlpnl.exe remains outside this sample.

The CE core maps \Windows paths to \fls0\Windows, loads/relocates import-free
sample DLLs in uncached SH4 memory, resolves named exports and frees loaded images.
DLLs with imports remain unsupported by this small dynamic loader. Control Panel's
window-extra slot and LVN_GETDISPINFOW text callback are supported for its applet
list. The launcher seeds Windows/*.cpl into the isolated CE volume.

This addition was compiled with strict warnings and formatted, without a new
emulator/test run as requested. Existing verification artifacts predate this CPL.

Control Panel title chrome now includes a touch X button at (288,0)-(320,24).
Press and release inside it destroys the window and posts WM_QUIT, avoiding the
physical Clear path. This addition was built and deployed without an emulator run.
