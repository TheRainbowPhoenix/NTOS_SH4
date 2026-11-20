// Serial foreground launch fallback: after guest-driven handoff chains,
// the optional WinCE Start host should run only when it is installed.
// No SDK access here: caller supplies a read-only, host-context exists()
// callback that does not create File_Open handles. This prevents misleading
// "PE file unavailable" errors after successfully completed test chains.
#pragma once

namespace ntos_launch_policy {
inline bool ascii_equal_ci(const char *a,const char *b) {
    if(!a || !b)return false;
    for(unsigned i=0u;a[i] || b[i];++i) {
        char x=a[i],y=b[i];
        if(x>='a' && x<='z')x-=32;
        if(y>='a' && y<='z')y-=32;
        if(x!=y)return false;
    }
    return true;
}
constexpr const char HOME[]="\\fls0\\STARTHOST.EXE";

template<class Exists>
inline bool queue_optional_home(const char *last,Exists exists) {
    if(!last || ascii_equal_ci(last,HOME) ||
       ascii_equal_ci(last,"\\fls0\\PSCHILD.EXE") ||
       ascii_equal_ci(last,"\\fls0\\PROCFINAL.EXE"))
        return false; // kernel test-chain exit returns to picker
    return exists(HOME); // host handles absence without an error screen
}
} // namespace ntos_launch_policy
