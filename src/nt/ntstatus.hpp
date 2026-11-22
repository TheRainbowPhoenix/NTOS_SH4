// Minimal NTSTATUS subset for the SH4BE NTOS executive.
// Status is a 32-bit value; high bit denotes failure.
#pragma once
#include <stdint.h>
namespace nt {
using Status=uint32_t;
constexpr Status SUCCESS=0x00000000u;
constexpr Status TIMEOUT=0x00000102u;
constexpr Status INVALID_HANDLE=0xC0000008u;
constexpr Status INVALID_PARAMETER=0xC000000Du;
constexpr Status NO_MEMORY=0xC0000017u;
constexpr Status CONFLICTING_ADDRESSES=0xC0000018u;
constexpr Status NOT_COMMITTED=0xC0000029u;
constexpr Status ALREADY_COMMITTED=0xC0000021u;
constexpr Status ACCESS_VIOLATION=0xC0000005u;
constexpr Status INTERNAL_ERROR=0xC00000E5u;
constexpr Status OBJECT_TYPE_MISMATCH=0xC0000024u;
constexpr Status NOT_SUPPORTED=0xC00000BBu;
constexpr Status INSUFFICIENT_RESOURCES=0xC000009Au;
constexpr Status DEVICE_BUSY=0xC00000E8u;
constexpr bool success(Status s) {return (s&0x80000000u)==0u;}
} // namespace nt
