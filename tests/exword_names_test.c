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
    expect("HELLOCORE.EXE","HELLOCOR.EXE");
    expect("hellocore.exe","HELLOCOR.EXE");
    expect("UIXMOBILE.DLL","UIXMOBIL.DLL");
    expect("APPCATALOG.DLL","APPCATAL.DLL");
    expect("SHELLCONTROLS.DLL","SHELLCON.DLL");
    expect("STARTMENU.DLL","STARTMEN.DLL");
    expect("SETTINGSUI.DLL","SETTINGS.DLL");
    char out[13];
    assert(!ntos_exword_shortname("X/Y.DLL",out));
    assert(!ntos_exword_shortname("../evil.exe",out));
    assert(!ntos_exword_shortname("F.ICON",out));
    assert(!ntos_exword_shortname("NOEXT",out));
    puts("PASS Exword shared 8.3 readable prefix aliases, WinCE import names");
    return 0;
}
