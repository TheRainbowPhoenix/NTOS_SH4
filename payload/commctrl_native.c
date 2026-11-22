/* Native GCC-SH4BE COMMCTRL.DLL bootstrap for NTOS COREDLL HWND classes.
 * The actual class factory and dispatch live in the shared USER/GWES path;
 * this DLL keeps the standard WinCE import module and public entrypoints.
 * It intentionally rejects unsupported ICC families instead of advertising
 * the entire Microsoft common-controls DLL surface as implemented.
 */
typedef unsigned int DWORD;
typedef struct {
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX;

#define NT_ICC_PROGRESS_CLASS 0x00000020u
#define NT_ICC_STANDARD_CLASSES 0x00004000u
#define NT_ICC_SUPPORTED (NT_ICC_PROGRESS_CLASS | NT_ICC_STANDARD_CLASSES)

void InitCommonControls(void)
{
    /* Standard BUTTON, STATIC, EDIT, LISTBOX and COMBOBOX are already
     * registered by COREDLL's built-in class resolver. */
}
int InitCommonControlsEx(const INITCOMMONCONTROLSEX *icc)
{
    if (!icc || icc->dwSize != sizeof(*icc) || !icc->dwICC)
        return 0;
    return (icc->dwICC & ~NT_ICC_SUPPORTED) == 0u;
}
int main(void)
{
    return 1;
}
