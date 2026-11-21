cmake_minimum_required(VERSION 3.20)
project(NTOS_EXWORD LANGUAGES C CXX ASM)
if(NOT FXSDK_PLATFORM_LONG STREQUAL "exword")
  message(FATAL_ERROR "Build with fxsdk-exword EXWORD.cmake, never the HH3 toolchain")
endif()
include(GenerateD01)
include(GenerateEXWORDPackage)
find_package(Gint 2.11 REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Native stage 0: no ELF/EDL second loader, no other C/C++ runtime.
# The EX-word gint linker script enforces the 128 KiB D01 image and the
# separate 0x7400xxxx RAM/stack regions.
add_executable(ntos_sh main.cpp storage.c pe_runner.cpp profile.cpp invoke.S
  ../payload/coredll.c)
set_source_files_properties(../payload/coredll.c PROPERTIES COMPILE_DEFINITIONS
  "NTOS_CORE_HOST_TEST;NTOS_EXWORD;main=ntos_exword_dll_main")
set_target_properties(ntos_sh PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(ntos_sh PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_compile_options(ntos_sh PRIVATE -Os -ffunction-sections -fdata-sections
  -fno-stack-protector -fno-pic -fno-pie
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-unwind-tables>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-asynchronous-unwind-tables>)
target_link_options(ntos_sh PRIVATE -Wl,--gc-sections -Wl,--build-id=none
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/ntos_sh.map)
target_link_libraries(ntos_sh PRIVATE Gint::Gint)
set_target_properties(ntos_sh PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# App/content ID = NTOSH (five uppercase chars), runtime module ID = ntosh
# (elf2d01 accepts <= 6 chars), installed D01 filename = ntos_sh.d01.
generate_d01(TARGET ntos_sh OUTPUT "${CMAKE_BINARY_DIR}/ntos_sh.d01" MODULE ntosh)
add_custom_target(ntos_d01_alias ALL
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_BINARY_DIR}/ntos_sh.d01" "${CMAKE_BINARY_DIR}/NTOS.d01"
  DEPENDS ntos_sh VERBATIM)

# Optional locally, but the CI workflow supplies the real JP/CN metadata.
if(EXWORD_METADATA_SOURCE)
  generate_exword_package(TARGET ntos_sh D01 "${CMAKE_BINARY_DIR}/ntos_sh.d01"
    APPID NTOSH TITLE "NTOSH" HTML_DIR "${EXWORD_METADATA_SOURCE}/html"
    OUTPUT_DIR "${CMAKE_BINARY_DIR}/packages" LOCALES ja cn)
endif()

# SYSROOT is an on-disk PE tree, NOT part of the 128 KiB D01 image.
# The same dist/ binaries created for NTOS.hh3 are staged into both EX-word
# packages, so SH4BE EXE/DLL changes cannot silently go stale.
if(EXWORD_PAYLOAD_DIR)
  get_filename_component(EXWORD_PAYLOAD_DIR "${EXWORD_PAYLOAD_DIR}" ABSOLUTE)
  if(NOT EXISTS "${EXWORD_PAYLOAD_DIR}/HELLO.EXE")
    message(FATAL_ERROR "EXWORD_PAYLOAD_DIR must contain built NTOS dist/ PE files")
  endif()
endif()
foreach(NTOSH_LOCALE IN ITEMS ja cn)
  if(EXWORD_METADATA_SOURCE)
    add_custom_target("ntos_sh_sysroot_${NTOSH_LOCALE}" ALL
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_BINARY_DIR}/packages/${NTOSH_LOCALE}/NTOSH/SYSROOT/Windows/System32"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/SYSROOT/README.TXT"
        "${CMAKE_BINARY_DIR}/packages/${NTOSH_LOCALE}/NTOSH/SYSROOT/README.TXT"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/SYSROOT/Windows/System32/README.TXT"
        "${CMAKE_BINARY_DIR}/packages/${NTOSH_LOCALE}/NTOSH/SYSROOT/Windows/System32/README.TXT"
      DEPENDS "ntos_sh_package_${NTOSH_LOCALE}" VERBATIM)
    if(EXWORD_PAYLOAD_DIR)
      add_custom_target("ntos_sh_payloads_${NTOSH_LOCALE}" ALL
        COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../tools/package_exword_sysroot.py"
          --from-dist "${EXWORD_PAYLOAD_DIR}"
          --package "${CMAKE_BINARY_DIR}/packages/${NTOSH_LOCALE}/NTOSH"
        DEPENDS "ntos_sh_sysroot_${NTOSH_LOCALE}"
        VERBATIM COMMENT "Copy NTOS EXE/DLL binaries to NTOSH ${NTOSH_LOCALE} SYSROOT")
    endif()
  endif()
endforeach()
