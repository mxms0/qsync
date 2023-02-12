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
    uint64_t Id,
    SerializedFileInfo&& File)
{
    // for (auto& Buf : Files) {
        // kj::ArrayPtr<const uint8_t> Ptr((const uint8_t*)Files.data(), Files.size());
        // kj::ArrayPtr<const capnp::word> Ptr((const capnp::word*)Buf.data(), Buf.size());
        // kj::ArrayPtr<const capnp::word> Ptr((capnp::word*)Buf.data(), Buf.size() / sizeof (capnp::word));

        // works
        // auto Message = capnp::FlatArrayMessageReader(Buf);

        // works
        kj::ArrayPtr<const uint8_t> Ptr(File.data(), File.size());
        auto Array = kj::ArrayInputStream(Ptr);
        auto Message = capnp::PackedMessageReader(Array);

        auto ParsedFile = Message.getRoot<FileInfo>();
        // auto File = capnp::readDataStruct<FileInfo>(Ptr);
        // if (File.getType() == FileInfo::Type::FILE) {
            // cout << Buf.size() * sizeof(Buf.front())<< endl;
            string_view Path(ParsedFile.getPath().cStr(), ParsedFile.getPath().size());
            // cout << "Path len: " << File.getPath().size() << " File Size: " << File.getSize() << endl;
            // cout << "Object: " << Buf.size() << " " << Path << endl;
            cout << std::hex << setw(16) << setfill('0') << Id << " " << Path << endl;
        // }
    // }
    // cout << "+++++++++++++++++++++++++++" << endl;
    // for (auto& Buf : Files) {
        // kj::ArrayPtr<const capnp::word> Ptr((capnp::word*)Buf.data(), Buf.size() / sizeof (capnp::word));

        // works
        // auto Message = capnp::FlatArrayMessageReader(Buf);

        // works
        // kj::ArrayPtr<const uint8_t> Ptr(Buf.data(), Buf.size());
        // auto Array = kj::ArrayInputStream(Ptr);
        // auto Message = capnp::PackedMessageReader(Array);

        // kj::ArrayPtr<const capnp::word> Ptr((const capnp::word*)Buf.data(), Buf.size()); // doesn't work
        // auto Message = capnp::FlatArrayMessageReader(Ptr);                               // doesn't work
        // auto Dir = Message.getRoot<FileInfo>();
        // if (Dir.getType() == FileInfo::Type::DIR) {
            // cout << Buf.size() * sizeof(Buf.front()) << endl;
            // string_view Path(Dir.getPath().cStr(), Dir.getPath().size());
            // cout << Path << endl;
        // }
    // }
    // cout << "---------------------------" << endl;
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
        } else if (*argv[1] == 's') {
            // qsync s port_number password path
        }
    } else if (argc == 6) {
        if (*argv[1] == 'c') {
            // qsync c addr port_number password path
            uint16_t Port = (uint16_t)atol(argv[3]);
            Client = make_unique<QsyncClient>();
            Client->Start(argv[2], Port, argv[5], argv[4]);
        }
    }
    do {
        cout << "Press enter to exit..." << endl;
    } while (getchar() != '\n');
    return 0;
}
