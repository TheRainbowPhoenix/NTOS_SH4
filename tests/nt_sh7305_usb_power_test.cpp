#include "../src/hal/sh7305_usb_power.hpp"
#include <cassert>
#include <cstdio>
struct Bus {
    unsigned count=0u;
    uint32_t addresses[2]{};
    static uint32_t read(void *arg,uint32_t addr) {
        auto &bus=*static_cast<Bus *>(arg);
        assert(bus.count<2u);
        bus.addresses[bus.count++]=addr;
        if(addr==ntos_hal::SH7305_USBCLKCR)return 0x100u;
        if(addr==ntos_hal::SH7305_MSTPCR2)return 0x100000u;
        assert(false);return 0u;
    }
};
int main() {
    using namespace ntos_hal;
    UsbPowerStatus output{};
    Bus bus{};
    assert(usb_power_status(&output,8u,USB_STATUS_IOCTL,
                            &Bus::read,&bus)==nt::INVALID_PARAMETER);
    assert(usb_power_status(&output,12u,0xdeadbeefu,
                            &Bus::read,&bus)==nt::NOT_SUPPORTED);
    assert(usb_power_status(&output,12u,USB_STATUS_IOCTL,
                            nullptr,&bus)==nt::NOT_SUPPORTED);
    assert(bus.count==0u);
    assert(usb_power_status(&output,12u,USB_STATUS_IOCTL,
                            &Bus::read,&bus)==nt::SUCCESS);
    assert(output.magic==USB_STATUS_MAGIC &&
           output.usb_clock==0x100u &&
           output.module_stop==0x100000u);
    assert(bus.count==2u && bus.addresses[0]==SH7305_USBCLKCR &&
           bus.addresses[1]==SH7305_MSTPCR2);
    std::puts("PASS SH7305 native USB: CPG/POWER passive snapshot only, no register mutation or unpowered USB controller access");
}
