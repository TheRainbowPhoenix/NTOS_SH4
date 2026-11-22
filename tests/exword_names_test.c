#include "../exword/shortname.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void expect(const char *name,const char *wanted)
{
    char output[13]={0};
    assert(ntos_exword_shortname(name,output));
    assert(!strcmp(output,wanted));
}
int main(void)
{
    expect("HELLO.EXE","HELLO.EXE");
    expect("COREDLL.DLL","COREDLL.DLL");
    expect("HELLOCORE.EXE","HELL6720.EXE");
    expect("hellocore.exe","HELL6720.EXE");
    expect("UIXMOBILE.DLL","UIXM57EB.DLL");
    expect("APPCATALOG.DLL","APPC4173.DLL");
    expect("SHELLCONTROLS.DLL","SHEL82FB.DLL");
    expect("STARTMENU.DLL","STAR15F0.DLL");
    expect("SETTINGSUI.DLL","SETTAA6A.DLL");
    char out[13];
    assert(!ntos_exword_shortname("X/Y.DLL",out));
    assert(!ntos_exword_shortname("../evil.exe",out));
    assert(!ntos_exword_shortname("F.ICON",out));
    assert(!ntos_exword_shortname("NOEXT",out));
    puts("PASS Exword shared 8.3 alias hashing, WinCE import names");
    return 0;
}
