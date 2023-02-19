#include "qsync.h"

using namespace std;

const MsQuicAlpn Alpn(QSYNC_ALPN);
const uint16_t CONTROL_STREAM_PRIORITY = 0x7FFF;
const uint32_t MAX_OUTSTANDING_SENDS = 4;
const uint32_t FILE_IO_SIZE = 0xFFFF;

void
QsyncClient::DataStreamContext::FileIoWorker()
{
    if (EndOfFile) {
        return;
    }
    QUIC_BUFFER* Buffer = (QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER) + FILE_IO_SIZE);
    if (Buffer == nullptr) {
        cerr << "Failed to allocate buffer for file IO!" << endl;
    }
    Buffer->Buffer = (uint8_t*)(Buffer + 1);
    Buffer->Length = FILE_IO_SIZE;

    FileReadStream.read((char*)Buffer->Buffer, Buffer->Length);
    auto BytesRead = FileReadStream.gcount();
    if (BytesRead < Buffer->Length || FileReadStream.eof()) {
        EndOfFile = true;
        Buffer->Length = (uint32_t)BytesRead;
    }
    QUIC_SEND_FLAGS Flags = EndOfFile ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;
    QUIC_STATUS Status = Stream->Send(Buffer, 1, Flags, Buffer);
    if (QUIC_FAILED(Status)) {
        cerr << "Data stream send failed with " << std::hex << Status << endl;
        free(Buffer);
        return;
    }
    ++OutstandingSends;
    if (!EndOfFile && OutstandingSends < MAX_OUTSTANDING_SENDS) {
        Client->Pool.Enqueue(&QsyncClient::DataStreamContext::FileIoWorker, this);
    }
}

QUIC_STATUS
QsyncClient::QSyncClientDataStreamCallback(
    _In_ MsQuicStream* Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    DataStreamContext* This = (DataStreamContext*)Context;
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        if (QUIC_FAILED(Event->START_COMPLETE.Status)) {
            cerr << "Data Stream Start failed! " << std::hex << Event->START_COMPLETE.Status << endl;
        } else {
            cout << "Data Stream opened! " << endl;
        }
        break;
    case QUIC_STREAM_EVENT_RECEIVE: {
        for (auto i = 0u; i < Event->RECEIVE.BufferCount; i++) {
            const QUIC_BUFFER* Buffer = Event->RECEIVE.Buffers + i;
            uint32_t BytesRead = 0;
            do {
                if (This->FileIdBytes != 0) {
                    if (Buffer->Length - BytesRead < sizeof(This->FileId) - This->FileIdBytes) {
                        // Tiny receive...
                        memcpy(
                            This->PartialFileIdBytes + This->FileIdBytes,
                            Buffer->Buffer + BytesRead,
                            Buffer->Length - BytesRead);
                        This->FileIdBytes += Buffer->Length - BytesRead;
                        BytesRead += Buffer->Length - BytesRead;
                        continue;
                    } else {
                        // Completed the partial
                        memcpy(
                            This->PartialFileIdBytes + This->FileIdBytes,
                            Buffer->Buffer + BytesRead,
                            sizeof(This->FileId) - This->FileIdBytes);
                        This->FileIdBytes = 0;
                        BytesRead += sizeof(This->FileId) - This->FileIdBytes;
                    }
                } else {
                    if (Buffer->Length - BytesRead < sizeof(This->FileId)) {
                        // Read partial FileId
                        memcpy(
                            This->PartialFileIdBytes,
                            Buffer->Buffer + BytesRead,
                            Buffer->Length - BytesRead);
                        This->FileIdBytes = Buffer->Length - BytesRead;
                        BytesRead += Buffer->Length - BytesRead;
                        continue;
                    } else {
                        memcpy(
                            This->PartialFileIdBytes,
                            Buffer->Buffer + BytesRead,
                            sizeof(This->FileId));
                        BytesRead += sizeof(This->FileId);
                    }
                }
                auto FileItr = This->Client->FileInfos.find(This->FileId);
                if (FileItr != This->Client->FileInfos.end()) {
                    auto Array = kj::ArrayInputStream(kj::ArrayPtr<const uint8_t>(FileItr->second.data(), FileItr->second.size()));
                    auto Message = capnp::PackedMessageReader(Array);
                    auto File = Message.getRoot<FileInfo>();

                    u8string_view PathView((char8_t*)File.getPath().cStr());
                    filesystem::path SyncRoot(This->Client->SyncPath);
                    auto Source = SyncRoot.has_stem() ? SyncRoot.parent_path() : SyncRoot;
                    Source /= PathView;
                    This->FileReadStream = std::fstream(Source, ios::binary | ios::in);
                    if (!This->FileReadStream.good()) {
                        cerr << "Failed to open file for reading " << Source << endl;
                        Stream->Shutdown(QUIC_STATUS_NOT_FOUND);
                        return QUIC_STATUS_SUCCESS;
                    }
                    This->Client->Pool.Enqueue(&QsyncClient::DataStreamContext::FileIoWorker, This);
                    This->Client->FileInfos.erase(FileItr);
                } else {
                    cerr << "No File found for FileId " << This->FileId << endl;
                }
            } while (BytesRead < Buffer->Length);
        }
        break;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        --This->OutstandingSends;
        free(Event->SEND_COMPLETE.ClientContext);
        if (!This->EndOfFile && This->OutstandingSends < MAX_OUTSTANDING_SENDS) {
            This->Client->Pool.Enqueue(&QsyncClient::DataStreamContext::FileIoWorker, This);
        }
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        delete This;
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
QsyncClient::QSyncClientControlStreamCallback(
    _In_ MsQuicStream* /*Stream*/,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    QsyncClient* This = (QsyncClient*)Context;
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        if (QUIC_FAILED(Event->START_COMPLETE.Status)) {
            cerr << "Control Stream Start failed! " << std::hex << Event->START_COMPLETE.Status << endl;
        } else {
            cout << "Control Stream opened! " << endl;
        }
        break;
    case QUIC_STREAM_EVENT_RECEIVE: {
        for (auto i = 0u; i < Event->RECEIVE.BufferCount; i++) {
            const QUIC_BUFFER* Buffer = Event->RECEIVE.Buffers + i;
            uint32_t BytesRead = 0;
            do {
                if (This->FileIdBytes != 0) {
                    if (Buffer->Length - BytesRead < sizeof(This->PartialFileId) - This->FileIdBytes) {
                        // Tiny receive...
                        memcpy(
                            This->PartialFileIdBytes + This->FileIdBytes,
                            Buffer->Buffer + BytesRead,
                            Buffer->Length - BytesRead);
                        This->FileIdBytes += Buffer->Length - BytesRead;
                        BytesRead += Buffer->Length - BytesRead;
                        continue;
                    } else {
                        // Completed the partial
                        memcpy(
                            This->PartialFileIdBytes + This->FileIdBytes,
                            Buffer->Buffer + BytesRead,
                            sizeof(This->PartialFileId) - This->FileIdBytes);
                        This->FileIdBytes = 0;
                        BytesRead += sizeof(This->PartialFileId) - This->FileIdBytes;
                    }
                } else {
                    if (Buffer->Length - BytesRead < sizeof(This->PartialFileId)) {
                        // Read partial FileId
                        memcpy(
                            This->PartialFileIdBytes,
                            Buffer->Buffer + BytesRead,
                            Buffer->Length - BytesRead);
                        This->FileIdBytes = Buffer->Length - BytesRead;
                        BytesRead += Buffer->Length - BytesRead;
                        continue;
                    } else {
                        memcpy(
                            This->PartialFileIdBytes,
                            Buffer->Buffer + BytesRead,
                            sizeof(This->PartialFileId));
                        BytesRead += sizeof(This->PartialFileId);
                    }
                }
                auto FileItr = This->FileInfos.find(This->PartialFileId);
                if (FileItr != This->FileInfos.end()) {
                    This->FileInfos.erase(FileItr);
                }
            } while (BytesRead < Buffer->Length);
        }
        break;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        free(Event->SEND_COMPLETE.ClientContext);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        cout << "Control stream shutdown" << endl;
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
QsyncClient::QsyncClientConnectionCallback(
    _In_ MsQuicConnection* /*Connection*/,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    QsyncClient* This = (QsyncClient*)Context;
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        cout << "Connected!" << endl;
        break;
    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
        if (!QcVerifyCertificate(This->CertPw, Event->PEER_CERTIFICATE_RECEIVED.Certificate)) {
            return QUIC_STATUS_BAD_CERTIFICATE;
        }
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
        DataStreamContext* StreamContext = new DataStreamContext();
        if (StreamContext == nullptr) {
            cerr << "Failed to allocate DataStreamContext" << endl;
            return QUIC_STATUS_ABORTED;
        }
        MsQuicStream* Stream = new MsQuicStream(Event->PEER_STREAM_STARTED.Stream, CleanUpAutoDelete, QsyncClient::QSyncClientDataStreamCallback, StreamContext);
        if (Stream == nullptr || QUIC_FAILED(Stream->InitStatus)) {
            cerr << "Failed to create Client Data Stream" << endl;
            return QUIC_STATUS_ABORTED;
        }
        StreamContext->Stream = Stream;
        StreamContext->Client = This;
        break;
    }
    case QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE:
        cout << "Allowed Streams: " << Event->STREAMS_AVAILABLE.BidirectionalCount << endl;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        cout << "Connection shutdown" << endl;
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool
QsyncClient::Start(
        const std::string& ServerAddr,
        uint16_t ServerPort,
        const std::string& StartPath,
        const string& Password = "")
{
    Reg =
        make_unique<MsQuicRegistration>(
            QSYNC_ALPN,
            QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT,
            true);
    if (!Reg->IsValid()) {
        cerr <<
            "Failed to initialize client MsQuicRegistration: "
            << Reg->GetInitStatus() << endl;
        return false;
    }

    if (!QcGenerateAuthCertificate(Password, Pkcs12, Pkcs12Length)) {
        return false;
    }

    Creds.CertificatePkcs12 = &Pkcs12Config;
    Pkcs12Config.Asn1Blob = Pkcs12.get();
    Pkcs12Config.Asn1BlobLength = Pkcs12Length;
    Pkcs12Config.PrivateKeyPassword = nullptr;

    Creds.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    if (Password.length() > 0) {
        // If password is provided, enable mutual authentication.
        Creds.Flags |=
            QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED
            | QUIC_CREDENTIAL_FLAG_DEFER_CERTIFICATE_VALIDATION;
        this->CertPw = Password;
        Creds.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;
    } else {
        Creds.Flags |=
            QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
        Creds.Type = QUIC_CREDENTIAL_TYPE_NONE;
    }

    MsQuicSettings Settings;
    Settings.SetPeerBidiStreamCount(10);
    Settings.SetDisconnectTimeoutMs(10000);
    Settings.SetSendBufferingEnabled(false);

    Config = make_unique<MsQuicConfiguration>(*Reg, Alpn, Settings, Creds);
    if (!Config->IsValid()) {
        cerr <<
            "Failed to initialize client MsQuicConfiguration: "
            << Config->GetInitStatus() << endl;
        return false;
    }

    Connection = make_unique<MsQuicConnection>(*Reg, CleanUpManual, QsyncClientConnectionCallback, this);
    if (!Connection->IsValid()) {
        cerr << "Failed to initialize MsQuicConnection: "
            << Connection->GetInitStatus() << endl;
        return false;
    }

    const QUIC_STREAM_SCHEDULING_SCHEME RoundRobin = QUIC_STREAM_SCHEDULING_SCHEME_ROUND_ROBIN;
    if (QUIC_FAILED(Connection->SetParam(QUIC_PARAM_CONN_STREAM_SCHEDULING_SCHEME, sizeof(RoundRobin), &RoundRobin))) {
        cerr << "Failed to set Round Robin on MsQuicConnection" << endl;
        return false;
    }

    this->ControlStream = make_unique<MsQuicStream>(*Connection, QUIC_STREAM_OPEN_FLAG_NONE, CleanUpManual, QSyncClientControlStreamCallback, this);
    if (!this->ControlStream->IsValid()) {
        cerr << "Failed to initialize MsQuicStream: "
            << this->ControlStream->GetInitStatus() << endl;
        return false;
    }

    uint16_t Priority = CONTROL_STREAM_PRIORITY;
    if (QUIC_FAILED(MsQuic->SetParam(this->ControlStream->Handle, QUIC_PARAM_STREAM_PRIORITY, sizeof(Priority), &Priority))) {
        cerr << "Failed to set priority on ControlStream" << endl;
        return false;
    }

    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = this->ControlStream->Start(
        QUIC_STREAM_START_FLAG_IMMEDIATE | QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL))) {
        cerr << "Failed to start control stream: " << Status << endl;
        return false;
    }

    if (QUIC_FAILED(Status = Connection->Start(*Config, ServerAddr.c_str(), ServerPort))) {
        cerr << "Failed to start connection: " << Status << endl;
        return false;
    }
    SyncPath = StartPath;
    FindFiles(
        SyncPath,
        [this](uint64_t Id, SerializedFileInfo&& File) {
            if (auto Search = this->FileInfos.find(Id); Search != this->FileInfos.end()) {
                auto Array = kj::ArrayInputStream(kj::ArrayPtr<const uint8_t>(File.data(), File.size()));
                auto Message = capnp::PackedMessageReader(Array);
                auto ParsedFile = Message.getRoot<FileInfo>();
                cout << "ERROR: " << ParsedFile.getPath().cStr() << " and ";
                auto Array2 = kj::ArrayInputStream(
                    kj::ArrayPtr<const uint8_t>(Search->second.data(), Search->second.size()));
                auto Message2 = capnp::PackedMessageReader(Array2);
                auto ParsedFile2 = Message2.getRoot<FileInfo>();
                cerr << ParsedFile2.getPath().cStr() << " hash to the same value!" << std::hex << Id << endl;
                exit(0);
            }
            const auto BufferCount = 2u;
            uint64_t AllocSize = (BufferCount * sizeof(QUIC_BUFFER)) + sizeof(uint32_t);
            QUIC_BUFFER* Buffer = (QUIC_BUFFER*)malloc(AllocSize);
            Buffer->Buffer = (uint8_t*)(Buffer + BufferCount);
            Buffer->Length = sizeof(uint32_t);
            uint32_t Size = (uint32_t)File.size();
            memcpy(Buffer->Buffer, &Size, sizeof(Size));
            // Advance to the second QUIC_BUFFER
            QUIC_BUFFER* FileBuffer = Buffer + 1;
            FileBuffer->Buffer = File.data();
            FileBuffer->Length = (uint32_t)File.size();
            auto Result = this->FileInfos.emplace(Id, std::move(File));
            QUIC_STATUS Status;
            if (QUIC_FAILED(Status = this->ControlStream->Send(Buffer, BufferCount, QUIC_SEND_FLAG_NONE, Buffer))) {
                cerr << "Error sending buffer: " << std::hex << Status << endl;
                free(Buffer);
            }
    });
    if (QUIC_FAILED(Status = ControlStream->Shutdown((QUIC_UINT62)QUIC_STATUS_SUCCESS, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL))) {
        cerr << "Failed to shutdown control stream " << std::hex << Status << endl;
    }
    return true;
}
