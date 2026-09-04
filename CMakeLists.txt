# ---------------------------------------------------------------------------
# ESP-IDF component: OpenH264 decoder for the ESP32 (Xtensa / RISC-V).
#
# Decoder + common sources only, portable C paths (no assembly). This file is
# only consumed by the ESP-IDF build system; meson / make builds of OpenH264
# ignore it.
#
# Use: drop this repo into your project as  components/openh264  and add
# "openh264" to your component REQUIRES.
# ---------------------------------------------------------------------------

set(_OH264_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(_OH264 ${_OH264_ROOT}/codec)

file(GLOB _OH264_SRCS
    ${_OH264}/decoder/core/src/*.cpp
    ${_OH264}/decoder/plus/src/*.cpp
    ${_OH264}/common/src/*.cpp
)

idf_component_register(
    SRCS ${_OH264_SRCS}
    INCLUDE_DIRS ${_OH264}/api/wels
    PRIV_INCLUDE_DIRS
        ${_OH264}/common/inc
        ${_OH264}/decoder/core/inc
        ${_OH264}/decoder/plus/inc
    REQUIRES pthread
    PRIV_REQUIRES heap
)

# No target intrinsics for Xtensa / RISC-V -> OpenH264 falls back to portable C
# as long as none of X86_ASM / HAVE_NEON* / HAVE_LSX are defined.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -w
    -fno-strict-aliasing
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
