// Host-only test for the raw BE COFF file emitted by tools/elf2ntx.py.
// No calculator SDK or emulator required.
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include "../src/coff.hpp"

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    coff::Image image{};
    assert(coff::parse(bytes.data(), static_cast<uint32_t>(bytes.size()), &image) == 0);
    assert(image.fh.machine == coff::MACHINE_SH4BE);
    assert(image.fh.opthdr_size == 224);
    assert(image.oh.magic == 0x010b);
    assert(image.oh.image_base == 0x00400000);
    assert(image.oh.entry_rva == 0x100);
    assert(image.oh.size_of_image == 4096);
    assert(image.oh.size_of_headers == 512);
    assert(image.oh.basereloc_rva != 0);
    assert(image.oh.basereloc_size >= 12);
    // A normal PE32 image must declare that it has 16 data directories.
    // The packer previously wrote this count at +96 rather than +92,
    // which made third-party PE tools ignore its .reloc directory.
    assert(coff::rd32(bytes.data() + 20 + 88) == 0); // LoaderFlags
    assert(coff::rd32(bytes.data() + 20 + 92) == 16);
    assert(coff::rd32(bytes.data() + 20 + 136) == image.oh.basereloc_rva);
    assert(coff::rd32(bytes.data() + 20 + 140) == image.oh.basereloc_size);
    assert((image.fh.flags & coff::F_RELFLG) != 0);

    bool found_reloc = false;
    for (uint32_t i = 0; i < image.section_count; ++i) {
        const coff::SectionHeader &section = image.sections[i];
        if (section.name[0] == '.' && section.name[1] == 'r' &&
            section.name[2] == 'e' && section.name[3] == 'l') {
            assert(section.rva == image.oh.basereloc_rva);
            assert(section.raw_offset + section.raw_size <= bytes.size());
            found_reloc = true;
        }
    }
    assert(found_reloc);
    // Execute the same section-copy/base-fixup path used by NTOS on a
    // synthetic BE .NTX. This proves that an F_RELFLG executable can still
    // carry a usable PE .reloc directory (the previous false rejection).
    std::vector<uint8_t> loaded(image.oh.size_of_image, 0);
    for (uint32_t i = 0; i < image.section_count; ++i) {
        const coff::SectionHeader &section = image.sections[i];
        assert(section.rva <= loaded.size());
        assert(section.raw_size <= loaded.size() - section.rva);
        assert(section.raw_offset <= bytes.size());
        assert(section.raw_size <= bytes.size() - section.raw_offset);
        for (uint32_t j = 0; j < section.raw_size; ++j)
            loaded[section.rva + j] = bytes[section.raw_offset + j];
    }
    constexpr uint32_t actual_base = 0x8C280000u;
    const uint32_t delta = actual_base - image.oh.image_base;
    uint32_t off = image.oh.basereloc_rva;
    const uint32_t end = off + image.oh.basereloc_size;
    uint32_t patched = 0;
    assert(end <= loaded.size());
    while (off + 8 <= end) {
        const uint32_t page = coff::rd32(loaded.data() + off);
        const uint32_t block_size = coff::rd32(loaded.data() + off + 4);
        assert(block_size >= 8 && block_size % 2 == 0);
        assert(block_size <= end - off);
        for (uint32_t k = 0; k < (block_size - 8) / 2; ++k) {
            uint16_t entry = coff::rd16(loaded.data() + off + 8 + k * 2);
            uint16_t type = static_cast<uint16_t>(entry >> 12);
            if (type == coff::BRELOC_ABSOLUTE) continue;
            assert(type == coff::BRELOC_HIGHLOW);
            uint32_t site = page + (entry & 0xFFF);
            assert(site <= loaded.size() && loaded.size() - site >= 4);
            uint8_t *word = loaded.data() + site;
            coff::wr32(word, coff::rd32(word) + delta);
            ++patched;
        }
        off += block_size;
    }
    assert(patched == 1);
    assert(coff::rd32(loaded.data() + 8) == actual_base + 0x100);
    std::puts("PASS: actual COFF loader relocation detection and base fixup");
}
