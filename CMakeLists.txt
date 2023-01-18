cmake_minimum_required (VERSION 3.17)

project ("qsync")

set(QUIC_TLS "openssl" CACHE STRING "TLS Library to use")
set(QUIC_ENABLE_LOGGING ON CACHE BOOL "Enable MsQuic logging")
set(QUIC_BUILD_SHARED OFF CACHE BOOL "Dynamically linking")
add_subdirectory(submodules/msquic)
target_compile_features(inc INTERFACE cxx_std_20)

add_subdirectory(submodules/capnproto)
capnp_generate_cpp(qsyncSources qsyncHeaders fileinfo.capnp)

# Add source to this project's executable.
add_executable (qsync "qsync.cpp" "qsync.h" "files.cpp" "files.h" "auth.cpp" "auth.h" "server.cpp" "server.h" "client.cpp" "client.h" ${qsyncSources} ${qsyncHeaders})
target_link_libraries(qsync msquic CapnProto::capnp)
target_include_directories(qsync PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(qsync PRIVATE cxx_std_20)

if (WIN32)
    # statically link against the OpenSSL in MsQuic
    target_link_libraries(qsync base_link OpenSSLQuic)
    target_compile_options(qsync PRIVATE /sdl /GF /Gy /WX /W4 /Zi /Zf
        $<$<CONFIG:RELEASE>:/O1 /Zo>)
    target_link_options(qsync PUBLIC /DEBUG:FULL /WX
        $<$<CONFIG:RELEASE>:/INCREMENTAL:NO /OPT:REF>)
else()
    target_compile_options(qsync PRIVATE -Werror -Wall -Wextra -Wformat=2 -Wno-type-limits
        -Wno-unknown-pragmas -Wno-multichar -Wno-missing-field-initializers
        $<$<CONFIG:DEBUG>:-g -Og>
        $<$<CONFIG:RELEASE>:-Os>)
    # link against OpenSSL's libcrypto for auth
    target_link_options(qsync PUBLIC -lcrypto)
endif()