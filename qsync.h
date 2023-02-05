#pragma once

#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

#include <msquic.hpp>

#include <kj/array.h>
#include <kj/common.h>
#include <capnp/serialize-packed.h>
#include "fileinfo.capnp.h"

#include "vector_stream.h"
#include "auth.h"
#include "files.h"
#include "server.h"
#include "client.h"

#define QSYNC_ALPN "qsync"

#ifndef WIN32
 #define UNREFERENCED_PARAMETER(x) (void)x
#endif

#define MASSERT(x) do { if (!(x)) { printf("Assert((%s) != true)\n", #x); } } while (0);

enum QsyncPerspective {
    Client = 0,
    Server = 1,
};

typedef struct _Settings {
    enum QsyncPerspective Perspective;
    struct {
        char *ServerAddress;
        uint16_t ServerPort;
    } ClientSettings;
    struct {
    
    } ServerSettings;
} QsyncSettings;
