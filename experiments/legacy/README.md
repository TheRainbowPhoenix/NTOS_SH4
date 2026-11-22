# Historical SH7305 ROM diagnostic (archived, not a supported payload)

This directory preserves the unique sources from the old
`feature/sh7305-probe-exe` branch. It is deliberately **not compiled,
installed, or auto-run** as part of the main NTOS package.

The probe expects a 29-element `NtosApi` table whose **slot 28** is a
`backing_pa(virtual_address)` function. Modern NTOS slot 28 is
`ntos_create_event`, and additional slots have been appended. Calling slot
28 with the old function signature is an ABI mismatch and would be unsafe.

The original probe also reads privileged SH7305 MMRs and tries to sample
P1 aliases directly; those operations need hardware-mode and protection
auditing before reintroduction. These sources are historical reference
only; do not build them with the current `payload.ld` and run them on a
physical CP400.

The ROM generator is retained here to facilitate a future safe rewrite.
No mainline Makefile/kernel/packaging regression was resurrected from this
old diverged branch.

Git ancestry retains all original feature-branch commits.
