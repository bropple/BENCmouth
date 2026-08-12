# Our own top level for the wrappers.
#
# clap-wrapper's convenience path (cmake/top_level_default.cmake) builds every
# format it thinks the platform can manage, and on MSVC that includes AAX -
# which then demands the Avid SDK and stops the configure dead. AAX is
# GPL3-or-commercial and needs Avid/PACE signing to load anywhere, so it is not
# a thing this project wants by accident.
#
# Adding clap-wrapper as a subdirectory instead means that file never runs and
# we ask for exactly the wrappers we want.
#
# Nothing of BENCmouth is compiled here. A VST3 built this way is a shim that
# finds BENCmouth.clap at runtime and loads it - so the two always install
# together, and a shim on its own does nothing at all.
cmake_minimum_required(VERSION 3.21)

# Before project(), and not negotiable: clap-wrapper uses u8"" string literals,
# which are char8_t from C++20 onward and will not convert to const char*. Its
# own top level pins 17; taken as a subdirectory we inherit the compiler's
# default instead, and a new enough GCC defaults to C++23.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The wrapper and the VST3 SDK are built as static libraries and then linked
# into a shared module, so every object in them has to be position independent.
# Without this the link fails on "relocation R_X86_64_PC32 ... can not be used
# when making a shared object", which is a long way from saying "add -fPIC" -
# and is the same message the .clap gave when it was linked against
# libbencmouth.a.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC: the static CRT, because the VST3 SDK builds itself that way and mixing
# the two is a link error listing every object in the wrapper
# ("mismatch detected for 'RuntimeLibrary'"). Static also means the plugin does
# not need the Visual C++ redistributable on someone else's machine, which is
# the same reason the CLAP and the GUI are linked static under MinGW.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Objective-C++ on Apple, because both wrappers have .mm sources there. Without
# OBJCXX in this list CMake configures happily and then fails at generate time
# with "required internal CMake variable not set: CMAKE_OBJCXX_COMPILE_OBJECT",
# which does not obviously mean "declare the language".
if(APPLE)
    project(bencmouth-wrappers LANGUAGES C CXX OBJC OBJCXX)
else()
    project(bencmouth-wrappers LANGUAGES C CXX)
endif()

if(NOT DEFINED CLAP_WRAPPER_DIR)
    message(FATAL_ERROR "set -DCLAP_WRAPPER_DIR to the clap-wrapper checkout")
endif()

# Handed down from include/bencmouth.h by the Makefile, so a bundle cannot
# claim a version the synthesizer inside it disagrees with. BENCsynth shipped
# exactly that - a component at 1.1.1 wrapping a CLAP at 0.1.0 - and auval was
# what noticed.
if(NOT DEFINED BM_VERSION OR BM_VERSION STREQUAL "")
    message(FATAL_ERROR "set -DBM_VERSION (the Makefile reads it from bencmouth.h)")
endif()

set(CLAP_WRAPPER_OUTPUT_NAME "BENCmouth" CACHE STRING "" FORCE)
set(CLAP_WRAPPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(${CLAP_WRAPPER_DIR} clap-wrapper EXCLUDE_FROM_ALL)

# Each wrapper only if its SDK is actually here.
#
# One CMakeLists declares both, so without these guards configuring for either
# format demands both SDKs - which is how BENCsynth's first AU build failed:
# au-fetch clones clap-wrapper and the AudioUnit SDK, and the configure then
# stopped dead on a missing VST3 tree it had no use for.
#
# The test is clap-wrapper's own: the file guarantee_vst3sdk checks for.
if(EXISTS "${VST3_SDK_ROOT}/public.sdk/source/main/pluginfactory.cpp")
    add_library(bencmouth_as_vst3 MODULE)
    target_add_vst3_wrapper(
        TARGET bencmouth_as_vst3
        OUTPUT_NAME "BENCmouth"
        BUNDLE_IDENTIFIER "net.ropple.bencmouth"
        BUNDLE_VERSION "${BM_VERSION}"
    )
else()
    message(STATUS "bencmouth: no VST3 SDK at '${VST3_SDK_ROOT}' - skipping the VST3 wrapper")
endif()

# AU, on macOS only. The only way into Logic and GarageBand, which between them
# are most of the Macs that will ever open this.
#
# Configured "explicitly" rather than from a CLAP target: the wrapper can read a
# plugin's identity out of a clap_entry it builds in the same CMake project, and
# ours is built by the Makefile instead, so the four-character codes are spelled
# out here. They are permanent - a host remembers a plugin by
# type/subtype/manufacturer, and changing any of them makes every saved project
# stop finding it.
if(APPLE AND EXISTS "${AUDIOUNIT_SDK_ROOT}/src/AudioUnitSDK")
    add_library(bencmouth_as_auv2 MODULE)
    target_add_auv2_wrapper(
        TARGET bencmouth_as_auv2
        OUTPUT_NAME "BENCmouth"
        BUNDLE_IDENTIFIER "net.ropple.bencmouth.auv2"
        BUNDLE_VERSION "${BM_VERSION}"
        MANUFACTURER_NAME "BENCO"
        MANUFACTURER_CODE "BNCO"
        SUBTYPE_CODE "BNCM"
        INSTRUMENT_TYPE "aumu"
    )
elseif(APPLE)
    message(STATUS "bencmouth: no AudioUnit SDK at '${AUDIOUNIT_SDK_ROOT}' - skipping the AU wrapper")
endif()
