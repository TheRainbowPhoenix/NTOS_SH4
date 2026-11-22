// fs_guard.hpp -- single-owner SDK filesystem gate.
// The CP400 filesystem is not assumed safe for concurrent opens. Every
// NTOS-controlled file and directory enumeration goes through this gate.
// File_Open/FindFirst failures do not create tracked handles; a successful
// A failed SDK close may already have closed its descriptor. Poison the
// gate permanently rather than retry a potentially recycled flash fd.
#pragma once
#include <stdint.h>

namespace ntos_fs {
struct Gate {
    enum Kind : uint8_t { none, file, finder };
    Kind kind=none;
    int32_t handle=-1;
    unsigned rejected=0;
    unsigned close_failures=0;
    unsigned rescued=0;
    bool poisoned=false;  // never retry an ambiguous SDK close

    bool busy() const { return kind!=none; }
    bool file_is(int32_t h) const { return !poisoned && kind==file && handle==h; }
    bool finder_is(int32_t h) const { return !poisoned && kind==finder && handle==h; }
    bool acquire_file(int32_t h) {
        if(busy() || poisoned || h<0) { ++rejected;return false; }
        kind=file;handle=h;return true;
    }
    bool acquire_finder(int32_t h) {
        if(busy() || h<0) { ++rejected;return false; }
        kind=finder;handle=h;return true;
    }
    void released() { kind=none;handle=-1; }
    // Only SDK return code 0 proves close success. Any other result is ambiguous\n    // and permanently poisons ownership (never call Close twice).
    template<class FileClose, class FindClose>
    bool close(FileClose close_file, FindClose close_find) {
        if(poisoned)return false;
        if(!busy())return true;
        int result=kind==file ? close_file(handle) : close_find(handle);
        if(result!=0) { ++close_failures;poisoned=true; return false; }
        released();return true;
    }
    template<class FileClose, class FindClose>
    bool cleanup(FileClose close_file, FindClose close_find) {
        if(poisoned)return false;
        if(!busy())return true;
        if(!close(close_file,close_find))return false;
        ++rescued;return true;
    }
};
}
