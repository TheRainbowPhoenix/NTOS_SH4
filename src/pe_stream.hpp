// pe_stream.hpp -- parse only the PE headers, without staging the full file.
// File reads happen with the host MMU/interrupt state, never from VBR faults.
// The reader is deliberately independent of Hollyhock for host regression tests.
#pragma once
#include <stdint.h>
#include "pe.hpp"

namespace pe_stream {
using ReadAt=bool (*)(void *,uint32_t,uint8_t *,uint32_t);

// The header itself is bounded to 1 KiB (8 sections max). This is a
// *file-header* limit, not a limit on PE SizeOfImage or .text/.data lengths.
// Validate the real offsets before reconstructing a compact header at 0x40
// for the existing pe::parse() validator; it still sees the real file size
// when checking section raw ranges.
inline int parse(ReadAt read,void *context,uint32_t file_size,pe::Image *out) {
    if(!read || !out || file_size<64u) return -1;
    uint8_t dos[64]{};
    if(!read(context,0u,dos,sizeof(dos)) ||
       dos[0]!='M' || dos[1]!='Z') return -1;
    const uint32_t offset=pe::rd32le(dos+0x3cu);
    if(offset>file_size || 24u>file_size-offset) return -1;
    uint8_t prefix[24]{};
    if(!read(context,offset,prefix,sizeof(prefix)) ||
       prefix[0]!='P' || prefix[1]!='E' ||
       prefix[2]!=0u || prefix[3]!=0u) return -1;
    const uint32_t sections=pe::rd16le(prefix+6u);
    const uint32_t optional=pe::rd16le(prefix+20u);
    if(!sections || sections>8u || optional<144u) return -1;
    const uint32_t bytes=24u+optional+40u*sections;
    constexpr uint32_t HEADER_BASE=0x40u;
    uint8_t header[1024]{};
    if(bytes>sizeof(header)-HEADER_BASE ||
       bytes>file_size-offset || bytes>file_size-HEADER_BASE)
        return -1;
    header[0]='M';header[1]='Z';
    header[0x3cu]=static_cast<uint8_t>(HEADER_BASE);
    if(!read(context,offset,header+HEADER_BASE,bytes)) return -1;
    return pe::parse(header,file_size,out);
}
} // namespace pe_stream
