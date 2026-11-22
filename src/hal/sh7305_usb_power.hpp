// SH7305 CP400 USB0 clock/module-stop READ-ONLY status.
// Addresses from gint-exword include/gint/mpu/cpg.h,power.h:
// CPG.USBCLKCR=0xA4150014; POWER.MSTPCR2=0xA4150038.
// Never touch USB protocol registers unless the hardware clock and
// power ownership transition has been safely established.
#pragma once
#include <stdint.h>
#include "../nt/ntstatus.hpp"
namespace ntos_hal {
constexpr uint32_t USB_STATUS_IOCTL=0x4E540200u;
constexpr uint32_t USB_STATUS_MAGIC=0x55534231u;
constexpr uint32_t SH7305_USBCLKCR=0xA4150014u;
constexpr uint32_t SH7305_MSTPCR2=0xA4150038u;
struct UsbPowerStatus {
    uint32_t magic=0u,usb_clock=0u,module_stop=0u;
};
static_assert(sizeof(UsbPowerStatus)==12u);
using UsbReadReg=uint32_t(*)(void *,uint32_t);
inline nt::Status usb_power_status(void *out,uint32_t bytes,uint32_t code,
                                   UsbReadReg read,void *opaque) {
    if(!out || bytes!=sizeof(UsbPowerStatus))return nt::INVALID_PARAMETER;
    if(code!=USB_STATUS_IOCTL || !read)return nt::NOT_SUPPORTED;
    // These two always-on CPG/POWER registers can be read even when the
    // USB peripheral clock is gated. The USB controller is NOT accessed.
    const UsbPowerStatus status{
        USB_STATUS_MAGIC,
        read(opaque,SH7305_USBCLKCR),
        read(opaque,SH7305_MSTPCR2)
    };
    *static_cast<UsbPowerStatus *>(out)=status;
    return nt::SUCCESS;
}
inline uint32_t usb_power_read_native(void *,uint32_t addr) {
#if defined(__sh__)
    return *reinterpret_cast<volatile const uint32_t *>(addr);
#else
    (void)addr;return 0u;
#endif
}
} // namespace ntos_hal
