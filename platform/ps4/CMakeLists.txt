# Harissa64 for PS4: the eboot. Built in WSL with DolphinPS4's toolchain
# (clang 21 + libc++ 21 on PacBrew's OpenOrbis), see tools/ps4/build.sh:
#   cmake -S platform/ps4 -B <build> -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=third_party/dolphinps4/toolchain/ps4-love-modern.cmake
cmake_minimum_required(VERSION 3.13)
project(harissa64_ps4 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(DPS4 ${ROOT}/third_party/dolphinps4)
set(PRDP ${ROOT}/third_party/parallel-rdp)

# Plain homebrew identity (as DolphinPS4's Vulkan probe): 4.5 GiB of direct
# memory, no Piglet. Our own heap (DolphinPS4's runtime) for every malloc of
# the app, not the system modules.
set(PS4_PAID "0x3800000000000035" CACHE STRING "" FORCE)
set(PS4_MALLOC_REPLACE OFF CACHE BOOL "" FORCE)
include(${DPS4}/toolchain/ps4-love-style.cmake)

file(READ ${ROOT}/VERSION H64_VERSION_RAW)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ROOT}/VERSION)
string(STRIP "${H64_VERSION_RAW}" H64_VERSION)

# ---- zlib (inflate only, for zipped ROMs) ----
set(ZLIB adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c)
list(TRANSFORM ZLIB PREPEND "${ROOT}/third_party/zlib/")
add_library(h64zlib STATIC ${ZLIB})
target_compile_definitions(h64zlib PUBLIC Z_SOLO)
target_compile_options(h64zlib PRIVATE -w)

# ---- the emulation core, RDRAM in host-word order (ParaLLEl-RDP reads it) ----
# ---- rcheevos (RetroAchievements' runtime and client, MIT; core/ra uses its MD5) ----
file(GLOB H64_RCHEEVOS_SOURCES CONFIGURE_DEPENDS "${ROOT}/third_party/rcheevos/src/*/*.c")
add_library(h64rcheevos STATIC ${H64_RCHEEVOS_SOURCES})
target_include_directories(h64rcheevos PUBLIC "${ROOT}/third_party/rcheevos/include" PRIVATE "${ROOT}/third_party/rcheevos/src")
target_compile_definitions(h64rcheevos PUBLIC RC_DISABLE_LUA)
target_compile_options(h64rcheevos PRIVATE -w)

# ---- BearSSL 0.6 (TLS client for the HTTPS calls of fe/xb_net.cpp; MIT) ----
file(GLOB_RECURSE BEARSSL ${ROOT}/third_party/bearssl/src/*.c)
add_library(h64bearssl STATIC ${BEARSSL})
target_include_directories(h64bearssl PUBLIC ${ROOT}/third_party/bearssl/inc PRIVATE ${ROOT}/third_party/bearssl/src)
target_compile_options(h64bearssl PRIVATE -w)

file(GLOB_RECURSE CORE ${ROOT}/core/*.cpp)
add_library(h64core STATIC ${CORE})
target_compile_definitions(h64core PUBLIC H64_VERSION=${H64_VERSION} H64_RDRAM_SWAP=1)
# -fwrapv: the software RDP relies on wrapping signed arithmetic (as GLSL).
target_compile_options(h64core PRIVATE -fwrapv -fno-strict-aliasing -ffp-contract=off -Wno-unused-parameter)
target_link_libraries(h64core PUBLIC h64zlib h64rcheevos)

# ---- ParaLLEl-RDP (parallel-rdp-standalone) ----
file(GLOB PRDP_RDP ${PRDP}/parallel-rdp/*.cpp)
set(PRDP_VK buffer buffer_pool command_buffer command_pool context cookie descriptor_set device
    event_manager fence fence_manager image indirect_layout memory_allocator pipeline_event
    query_pool render_pass sampler semaphore semaphore_manager shader texture/texture_format)
list(TRANSFORM PRDP_VK PREPEND "${PRDP}/vulkan/")
list(TRANSFORM PRDP_VK APPEND ".cpp")
set(PRDP_UTIL arena_allocator logging thread_id aligned_alloc timer timeline_trace_file environment thread_name)
list(TRANSFORM PRDP_UTIL PREPEND "${PRDP}/util/")
list(TRANSFORM PRDP_UTIL APPEND ".cpp")
add_library(prdp STATIC ${PRDP_RDP} ${PRDP_VK} ${PRDP_UTIL} ${PRDP}/volk/volk.c)
target_include_directories(prdp PUBLIC ${PRDP}/parallel-rdp ${PRDP}/volk ${PRDP}/vulkan
                           ${PRDP}/vulkan-headers/include ${PRDP}/util)
target_compile_options(prdp PRIVATE -w)

# ---- shaders: GLSL compiled to SPIR-V headers (ACO compiles them on the console) ----
find_program(GLSLANG glslangValidator REQUIRED)
set(SHADER_HEADERS)
foreach(sh blit.vert blit.frag post.frag ui.vert ui.frag)
    string(REPLACE "." "_" var ${sh})
    set(out ${CMAKE_CURRENT_BINARY_DIR}/shaders/${var}.h)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLANG} -V --target-env vulkan1.1 --vn ${var} -o ${out} ${CMAKE_CURRENT_LIST_DIR}/shaders/${sh}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/shaders/${sh} VERBATIM)
    list(APPEND SHADER_HEADERS ${out})
endforeach()
add_custom_target(h64shaders DEPENDS ${SHADER_HEADERS})

# ---- the app ----
# The Harissa64 front end, ported from the Xbox 360 version (fe/, same file names).
set(FE fe/xb_compat.cpp fe/xb_ui.cpp fe/xb_lang.cpp fe/xb_config.cpp fe/xb_drives.cpp fe/xb_art.cpp fe/xb_menu.cpp fe/xb_net.cpp fe/xb_ra.cpp)
add_executable(harissa64 main.cpp ps4_log.cpp ps4_gpu.cpp ps4_video.cpp ps4_input.cpp ps4_audio.cpp ps4_softtest.cpp ${FE} ${PS4_RUNTIME_SOURCES})
add_dependencies(harissa64 h64shaders)
target_include_directories(harissa64 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(harissa64 PRIVATE -Wall -Wno-format)
target_link_libraries(harissa64 PRIVATE h64core prdp h64bearssl)
ps4_link_mesa(harissa64)
target_link_libraries(harissa64 PRIVATE
                      z SceSysmodule SceSystemService SceUserService SceVideoOut ScePad SceAudioOut SceNet SceImeDialog pthread)
ps4_add_eboot(harissa64)
