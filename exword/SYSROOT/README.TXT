NTOSH SYSROOT
Drop SH4BE WinCE *.EXE here or into Apps\subfolders.
Compiled NTOS *.EXE and *.DLL are automatically included by the build.
All shared DLLs are in Windows\System32; EXE-local DLLs take precedence.
COREDLL.DLL is included for a complete WinCE tree, but NTOSH resolves
its supported COREDLL functions from the resident D01 implementation.
No EDL stage-two loader is needed.
