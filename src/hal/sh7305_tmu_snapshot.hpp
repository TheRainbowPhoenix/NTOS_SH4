// SH7305 standard TMU0 READ-ONLY hardware snapshot.
// Register addresses and layout checked against gint-exword
// include/gint/mpu/tmu.h and intc.h. No reset, priority, start,
// acknowledge, USB or VBR write. Never invoked by an IRQ callback.
// Hardware use only in a normal privileged P1 Ke device-control IRP.
#pragma once
#include <stdint.h>
#include "../nt/ntstatus.hpp"
namespace ntos_hal {
constexpr uint32_t TMU_SNAPSHOT_IOCTL=0x4E540100u;
constexpr uint32_t TMU_SNAPSHOT_TAG=0x544D5531u;
constexpr uint32_t TMU_TSTR=0xA4490004u;
constexpr uint32_t TMU_TCOR0=0xA4490008u;
constexpr uint32_t TMU_TCNT0=0xA449000Cu;
constexpr uint32_t TMU_TCR0=0xA4490010u;
constexpr uint32_t INTC_IPRA=0xA4080000u;
using RegisterLoad=uint32_t(*)(void *,uint32_t,unsigned);
struct TmuSnapshot {
    uint32_t tag=0u;
    uint32_t running=0u;
    uint32_t period=0u;
    uint32_t counter=0u;
    uint32_t control=0u;
    uint32_t priority=0u;
};
static_assert(sizeof(TmuSnapshot)==24u);
inline nt::Status tmu_snapshot(void *dest,uint32_t bytes,uint32_t code,
                               RegisterLoad rd,void *source) {
    if(!dest || bytes!=sizeof(TmuSnapshot))return nt::INVALID_PARAMETER;
    if(code!=TMU_SNAPSHOT_IOCTL)return nt::NOT_SUPPORTED;
    if(!rd)return nt::NOT_SUPPORTED;
    TmuSnapshot value{};
    value.tag=TMU_SNAPSHOT_TAG;
    value.running=rd(source,TMU_TSTR,1u)&7u;
    value.period=rd(source,TMU_TCOR0,4u);
    value.counter=rd(source,TMU_TCNT0,4u);
    value.control=rd(source,TMU_TCR0,2u)&0xffffu;
    value.priority=rd(source,INTC_IPRA,2u)&0xffffu;
    *static_cast<TmuSnapshot *>(dest)=value;
    return nt::SUCCESS;
}
// On SH7305 the standard timer MMIO block is in P2 (physical,
// translation bypass). The loader must be privileged before calling.
inline uint32_t tmu_read_native(void *,uint32_t address,unsigned bytes) {
#if defined(__sh__)
    if(bytes==1u)
        return *reinterpret_cast<volatile const uint8_t *>(address);
    if(bytes==2u)
        return *reinterpret_cast<volatile const uint16_t *>(address);
    if(bytes==4u)
        return *reinterpret_cast<volatile const uint32_t *>(address);
#else
    (void)address;(void)bytes;
#endif
    return 0u;
}
} // namespace ntos_hal
