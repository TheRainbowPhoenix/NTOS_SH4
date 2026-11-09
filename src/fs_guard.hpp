// fs_guard.hpp -- single-owner SDK filesystem gate.
// The CP400 filesystem is not assumed safe for concurrent opens. Every
// NTOS-controlled file and directory enumeration goes through this gate.
// File_Open/FindFirst failures do not create tracked handles; a successful
// Close failure keeps the ownership marker so we never open on top of it.
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

    bool busy() const { return kind!=none; }
    bool file_is(int32_t h) const { return kind==file && handle==h; }
    bool finder_is(int32_t h) const { return kind==finder && handle==h; }
    bool acquire_file(int32_t h) {
        if(busy() || h<0) { ++rejected;return false; }
        kind=file;handle=h;return true;
    }
    bool acquire_finder(int32_t h) {
        if(busy() || h<0) { ++rejected;return false; }
        kind=finder;handle=h;return true;
    }
    void released() { kind=none;handle=-1; }
    // Returns false on a genuine SDK close failure and retains ownership.
    template<class FileClose, class FindClose>
    bool close(FileClose close_file, FindClose close_find) {
        if(!busy())return true;
        int result=kind==file ? close_file(handle) : close_find(handle);
        if(result<0) { ++close_failures; return false; }
        released();return true;
    }
    template<class FileClose, class FindClose>
    bool cleanup(FileClose close_file, FindClose close_find) {
        if(!busy())return true;
        if(!close(close_file,close_find))return false;
        ++rescued;return true;
    }
};
}
