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
    std::puts("PASS: raw COFF/PE32 parser, section table, and .reloc directory");
}
