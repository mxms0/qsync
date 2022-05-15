#include "qsync.h"

using namespace std;

void ParseArguments(QsyncSettings &Settings, int argc, char **argv) {
    UNREFERENCED_PARAMETER(Settings);
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);
}

void PrintFilesAndDirs(
    const vector<u8string>& Files,
    const vector<u8string>& Dirs)
{
    for (auto& File : Files) {
        cout << (char*)File.data() << endl;
    }
    cout << "+++++++++++++++++++++++++++" << endl;
    for (auto& Dir : Dirs) {
        cout << (char*)Dir.data() << endl;
    }
    cout << "---------------------------" << endl;
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
    
    if (argc == 2) {
        vector<string> Files;
        vector<string> Dirs;
        if (!FindFiles(argv[1], PrintFilesAndDirs)) {
            cout << "Failed to finish parsing!" << endl;
        }
    }
    return 0;
}
