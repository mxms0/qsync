#include "qsync.h"

using namespace std;

MsQuicApi Api;
const MsQuicApi* MsQuic;

void ParseArguments(QsyncSettings &Settings, int argc, char **argv) {
    UNREFERENCED_PARAMETER(Settings);
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);
}

void PrintFilesAndDirs(
    const vector<std::filesystem::path>& Files,
    const vector<std::filesystem::path>& Dirs)
{
    for (auto& File : Files) {
        cout << (char*)(File.u8string().data()) << endl;
    }
    cout << "+++++++++++++++++++++++++++" << endl;
    for (auto& Dir : Dirs) {
        cout << (char*)(Dir.u8string().data()) << endl;
    }
    cout << "---------------------------" << endl;
}

int main(
    int argc,
    char** argv)
{
    QsyncSettings Settings = { };
    ParseArguments(Settings, argc, argv);
    
    MASSERT(QUIC_SUCCEEDED(Api.GetInitStatus()));
    MsQuic = &Api;
    std::unique_ptr<QsyncServer> Server;
    std::unique_ptr<QsyncClient> Client;
    
    if (argc == 2) {
        vector<string> Files;
        vector<string> Dirs;
        if (!FindFiles(argv[1], PrintFilesAndDirs)) {
            cout << "Failed to finish parsing!" << endl;
        }
    } else if (argc == 3) {
        if (*argv[1] == 's') {
            // qsync s port_number
            uint16_t Port = (uint16_t)atol(argv[2]);
            Server = make_unique<QsyncServer>();
            Server->Start(Port, "");
        }
    } else if (argc == 4) {
        if (*argv[1] == 's') {
            // qsync s port_number password
            uint16_t Port = (uint16_t)atol(argv[2]);
            Server = make_unique<QsyncServer>();
            Server->Start(Port, argv[3]);
        } else if (*argv[1] == 'c') {
            // qsync c addr port_number
            uint16_t Port = (uint16_t)atol(argv[3]);
            Client = make_unique<QsyncClient>();
            Client->Start(argv[2], Port, "", "");
        }
    } else if (argc == 5) {
        if (*argv[1] == 'c') {
            // qsync c addr port_number password
            uint16_t Port = (uint16_t)atol(argv[3]);
            Client = make_unique<QsyncClient>();
            Client->Start(argv[2], Port, "", argv[4]);
        }
    }
    do {
        cout << "Press enter to exit..." << endl;
    } while (getchar() != '\n');
    return 0;
}
