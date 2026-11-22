# Native USER/GWES control HWNDs and next-stage process composition

This is the SH4BE / no-FPU shipping COREDLL profile, **not** the separate
MSVC-compatible original WinCE ABI experiment. The physical ClassPad display
is 320×528 RGB565, flash is slow, and antialiasing is deliberately absent.

## What works on hardware-facing code

COREDLL now resolves child `CreateWindowExW` requests for these standard
UTF-16 class names: `STATIC`, `BUTTON` (including checkbox styles),
`EDIT`, `LISTBOX`, `COMBOBOX`, and `msctls_progress32`.
Each object has a bounded, non-recycled opaque HWND token, a real parent HWND
and a scoped rectangle. The first legacy EDIT and additional independent
EDIT handles coexist. The fixed model has 8 native control slots, 8 list
entries/control and short bounded text. A parent `DestroyWindow` closes its
controls and makes their old tokens invalid.

`SendMessageW` supports basic WinCE `WM_GETTEXT`, `WM_SETTEXT`,
`WM_GETTEXTLENGTH`, `WM_ENABLE`, `BM_GETCHECK/SETCHECK/CLICK`,
`PBM_SETRANGE/SETPOS`, and a small `LB_*`/`CB_*` subset.
Button touch down/up delivers `WM_COMMAND` to the parent; list selection
delivers an LBN_SELCHANGE-style notification. The system TouchKB routes
`WM_CHAR` to whichever EDIT has focus and paints after the controls,
so a SIP overlay is not erased by the application.

The new **COMMCTRL.DLL** exports real (GCC SH4BE ABI) entrypoints
`InitCommonControls` and `InitCommonControlsEx`. InitCommonControlsEx
accepts only STANDARD_CLASSES and PROGRESS_CLASS flags, since the other
Microsoft common-control families are not implemented. The classes
themselves reside in COREDLL. `CTRLDEMO.EXE` imports COMMCTRL.DLL and
exercises the native control creation, touch commands and input fields.

Native control composition uses the existing foreground parent window's
paint pass, with drawing order: application, controls, modal dialogs,
keyboard. No large bitmaps or a second full-screen framebuffer are
allocated for controls. Each pixel operation stays within the display's
integer-only RGB565 path.

## A process-owned compositor contract, but not yet a running DWM

`src/nt/gwes_compositor.hpp` implements the **kernel-side contract** for
two PID-owned opaque window handles, verified P1-owned RGB565 surface
snapshots, split/columns/full layout, swap, SIP work area and row-wise
screen composition through a caller-provided scanline writer. Its
`tests/gwes_compositor_test.cpp` checks cross-PID access denial,
token reuse, clipping, hit testing, rotation, swap and SIP reservation.
It does not allocate its own framebuffer or call MMIO or SDK functions.

**Important:** this header is not currently connected to NTOS's live guest
dispatch path. The existing `nt::Process`, `KernelContext` and COREDLL
still execute a single foreground guest image at a time. `CreateProcessW`
hands off serially. The hardware dual-image root/IRQ probes and the
host-only cross-process scheduler have not yet become a production
preemptive or cooperative two-process guest scheduler. Thus two different
PE programs **cannot yet keep simultaneously runnable WNDPROCs** or
publish concurrently updated compositor surfaces. A second child HWND is
real inside the current PE but it is not a separate OS process.

For true cross-process composition, the next safe integration requires:
a process-resident DLL/context lifetime model (including owner-specific
GDI state), validated P1 surface allocation and syscall-mediated publish,
TRAPA/Ke cross-process context switching with SH4AL-DSP and MMU root
handoff, and a kernel-owned event queue that delivers touch/keyboard and
damage notifications only to the active owner. Kernel FS/SDK calls must
remain serialized and never run from VBR/ISR context. The compositor
must clear exposed regions when a pane closes or moves; the model
currently emits only active pane scanlines and relies on the caller's
background clear/damage pass.

Do not describe this as a finished WinCE GWES/DWM, broad COMCTL32
compatibility, original Microsoft ABI certification, or as tested
on physical hardware.

## Tests

- `tests/nt_common_controls_test.c`: token lifetime, bounds, roles, list
  storage and bounded EDIT text.
- `tests/coredll_common_controls_test.c`: actual shipped COREDLL control
  messages, paint and TouchKB.
- `tests/coredll_gwes_test.c`: updated to require two separate EDIT HWNDs
  with correct keyboard focus.
- `tests/commctrl_native_test.c`: supported-class bootstrap and rejection
  of unsupported ICC masks.
- `tests/gwes_compositor_test.cpp`: PID ownership and row-wise RGB565
  composition model with SIP and swap.
- `CTRLDEMO.EXE`: manual hardware/emulator smoke test.

The repository-wide clang-format validation has baseline failures that
predate these changes; the dedicated native controls tests are separate.
