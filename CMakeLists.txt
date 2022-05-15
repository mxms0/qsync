cmake_minimum_required (VERSION 3.17)

project ("qsync")

set(QUIC_TLS "openssl" CACHE STRING "TLS Library to use")
set(QUIC_ENABLE_LOGGING ON CACHE BOOL "Enable MsQuic logging")
set(QUIC_BUILD_SHARED ON CACHE BOOL "Dynamically linking")
add_subdirectory(submodules/msquic)
target_compile_features(inc INTERFACE cxx_std_20)

# Add source to this project's executable.
add_executable (qsync "qsync.cpp" "qsync.h" "files.cpp" "files.h")
target_link_libraries(qsync msquic)
target_compile_features(qsync PRIVATE cxx_std_20)

if (WIN32)
    target_compile_options(qsync PRIVATE /sdl /GF /Gy /WX /W4 /Zi /Zf
        $<$<CONFIG:RELEASE>:/O1 /Zo>)
    target_link_options(qsync PUBLIC /DEBUG:FULL /WX
        $<$<CONFIG:RELEASE>:/INCREMENTAL:NO /OPT:REF>)
else()
    target_compile_options(qsync PRIVATE -Werror -Wall -Wextra -Wformat=2 -Wno-type-limits
        -Wno-unknown-pragmas -Wno-multichar -Wno-missing-field-initializers
        $<$<CONFIG:DEBUG>:-g -Og>
        $<$<CONFIG:RELEASE>:-Os>)
endif()