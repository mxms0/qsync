#include "qsync.h"

void ParseArguments(QsyncSettings &Settings, int argc, char **argv) {
    UNREFERENCED_PARAMETER(Settings);
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);
}

int main(
    int argc,
    char** argv)
{
    printf("Hello world!\n");

    QsyncSettings Settings = { };
    ParseArguments(Settings, argc, argv);
    
    const QUIC_API_TABLE *MsQuicApi = nullptr;
    MASSERT(MsQuicOpen2(&MsQuicApi));
    
    return 0;
}
