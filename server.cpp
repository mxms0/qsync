#include "qsync.h"

using namespace std;
namespace fs = std::filesystem;

const MsQuicAlpn Alpn(QSYNC_ALPN);

void
PrintFilePath(uint8_t* Buffer, uint32_t Length)
{
    kj::ArrayPtr<const uint8_t> Ptr(Buffer, Length);
    auto Array = kj::ArrayInputStream(Ptr);
    auto Message = capnp::PackedMessageReader(Array);
    auto File = Message.getRoot<FileInfo>();
    string_view Path(File.getPath().cStr(), File.getPath().size());
    cout << Path << endl;
}

void
QsyncServer::AddFileToList(QsyncServer* Server, uint8_t* Buffer, uint32_t Length)
{
    ReceivedFileInfo Info;
    Info.FileInfo = SerializedFileInfo(Buffer, Buffer + Length);
    Info.Server = Server;
    Pool.Enqueue(&QsyncServer::QSyncServerWorkerCallback, Server, std::move(Info));
}

void
QsyncServer::DataStreamContext::FileIoWorker()
{
    if (!FileWriteStream.is_open()) {
        FileWriteStream.open(TempDestinationPath, ios::binary | ios::out | ios::trunc);
        if (!FileWriteStream.good()) {
            cerr << "Failed to open file for writing " << TempDestinationPath << " " << strerror(errno) << endl;
            Stream->Shutdown((QUIC_UINT62)QUIC_STATUS_INTERNAL_ERROR);
            return;
        }
    }
    uint64_t TotalWritten = 0;
    for (auto i = 0u; i < BufferCount; ++i) {
        FileWriteStream.write((char*)Buffers[i].Buffer, Buffers[i].Length);
        TotalWritten += Buffers[i].Length;
        if (FileWriteStream.fail()) {
            cerr << "Failed to write to file " << TempDestinationPath << " " << strerror(errno) << endl;
            Stream->Shutdown((QUIC_UINT62)QUIC_STATUS_INTERNAL_ERROR);
            goto Deref;
        }
    }
    BytesWritten += TotalWritten;
    Stream->ReceiveComplete(TotalWritten);
    if (FinalReceive) {
        FileWriteStream.flush();
        FileWriteStream.close();
        error_code Error;
        auto StillExists = fs::exists(DestinationPath, Error);
        if (Error) {
            cerr << "Failed to test if " << DestinationPath << " still exists " << Error << endl;
            StillExists = false;
        }
        if (FileExists && StillExists) {
            // TODO: validate file hasn't changed
            auto FileSize = fs::file_size(DestinationPath, Error);
            if (Error) {
                cerr << "Failed to get file size for existing file " << DestinationPath << " why " << Error << endl;
                fs::remove(TempDestinationPath);
                goto Deref;
            }
            if (FileSize != SnapshotDestSize) {
                cerr << DestinationPath << " changed in size " << FileSize << " vs " << SnapshotDestSize << endl;
                fs::remove(TempDestinationPath);
                goto Deref;
            }
            auto CurrentFileTime = fs::last_write_time(DestinationPath, Error);
            if (Error) {
                cerr << "Failed to get last mod time for existing file " << DestinationPath << " why " << Error << endl;
                fs::remove(TempDestinationPath);
                goto Deref;
            }
            if (CurrentFileTime != SnapshotDestModTime) {
                cerr << DestinationPath << " modified " << CurrentFileTime << " vs " << SnapshotDestModTime << endl;
                fs::remove(TempDestinationPath);
                goto Deref;
            }
        }
        if (BytesWritten != NewFileSize) {
            cerr << "New file size doesn't equal the bytes written to disk! " << BytesWritten << " vs " << NewFileSize << endl;
            goto Deref;
        }
        fs::rename(TempDestinationPath, DestinationPath, Error);
        if (Error) {
            cerr << "Failed to rename " << TempDestinationPath << " to " << DestinationPath << " why " << Error << endl;
            fs::remove(TempDestinationPath);
            goto Deref;
        }
        fs::last_write_time(DestinationPath, FileTime, Error);
        if (Error) {
            cerr << "Failed to set time on " << DestinationPath << " to " << FileTime << " why " << Error << endl;
            goto Deref;
        }
        cout << "Finished file " << (char*)DestinationPath.u8string().c_str() << endl;
    }
Deref:
    if (--RefCount == 0) {
        delete this;
    }
}

void
QsyncServer::QSyncServerWorkerCallback(
    _In_ const ReceivedFileInfo& Info)
{
    auto Array = kj::ArrayInputStream(kj::ArrayPtr<const uint8_t>(Info.FileInfo.data(), Info.FileInfo.size()));
    auto Message = capnp::PackedMessageReader(Array);
    auto File = Message.getRoot<FileInfo>();
    QUIC_STATUS Status;
    QUIC_BUFFER* Buffer = (QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER) + sizeof(uint64_t));
    Buffer->Buffer = (uint8_t*)(Buffer + 1);
    Buffer->Length = (uint32_t)sizeof(uint64_t);
    auto Id = File.getId();
    memcpy(Buffer->Buffer, &Id, sizeof(Id));

    if (DoesFileNeedUpdate(BasePath, File)) {
        u8string_view PathView((char8_t*)File.getPath().cStr());
        auto DestinationPath = BasePath / PathView;
        error_code Error;
        if (File.getType() == FileInfo::Type::DIR) {
            // cout << "Directory needs updating " << DestinationPath << endl;
            if (fs::exists(DestinationPath, Error)) {
                if (Error) {
                    cerr << "Failed to test whether the folder exists (success?) " << DestinationPath << " why " << Error << endl;
                    return;
                }
            } else {
                if (Error) {
                    cerr << "Failed to test whether the folder exists (failed?) " << DestinationPath << " why " << Error << endl;
                    return;
                }
                fs::create_directory(DestinationPath, Error);
                if (Error) {
                    cerr << "Failed to create directory " << DestinationPath << " why " << Error << endl;
                    return;
                }
            }
            chrono::utc_time<chrono::seconds> FileTime(chrono::seconds(File.getModifiedTime()));
            fs::last_write_time(DestinationPath, chrono::file_clock::from_utc(FileTime), Error);
            if (Error) {
                cerr << "Failed to set directory modified time " << DestinationPath << " why " << Error << endl;
                return;
            }
            return;
        } else if (File.getType() == FileInfo::Type::FILESYMLINK) {
            // cout << "Symlink needs updating " << DestinationPath << endl;
            if (fs::exists(DestinationPath, Error)) {
                if (Error) {
                    cerr << "Failed to test whether the file symlink exists (success?) " << DestinationPath << " why " << Error << endl;
                    return;
                }
            } else {
                if (Error) {
                    cerr << "Failed to test whether the file symlink exists (failure?) " << DestinationPath << " why " << Error << endl;
                    return;
                }
                u8string_view LinkDestView((char8_t*)File.getLinkPath().cStr());
                fs::path LinkDest(LinkDestView);
                fs::create_symlink(LinkDest, DestinationPath, Error);
                if (Error) {
                    cerr << "Failed to create file symlink " << DestinationPath << " -> " << LinkDest << " why " << Error << endl;
                    return;
                }
            }
            return;
        } else if (File.getType() == FileInfo::Type::DIRSYMLINK) {
            if (fs::exists(DestinationPath, Error)) {
                if (Error) {
                    cerr << "Failed to test whether the dirsymlink exists (success?) " << DestinationPath << " why " << Error << endl;
                    return;
                }
            } else {
                if (Error) {
                    cerr << "Failed to test whether the dirsymlink exists (failure?) " << DestinationPath << " why " << Error << endl;
                    return;
                }
                u8string_view LinkDestView((char8_t*)File.getLinkPath().cStr());
                fs::path LinkDest(LinkDestView);
                fs::create_directory_symlink(LinkDest, DestinationPath, Error);
                if (Error) {
                    cerr << "Failed to create dirsymlink " << DestinationPath << " -> " << LinkDest << " why " << Error << endl;
                    return;
                }
            }
            return;
        }
        ASSERT(File.getType() == FileInfo::Type::FILE);
        auto Context = new DataStreamContext();
        Context->Server = this;
        Context->FileTime =
            chrono::file_clock::from_utc(
                chrono::utc_time<chrono::seconds>(chrono::seconds(File.getModifiedTime())));
        Context->NewFileSize = File.getSize();
        Context->FileInfo = std::move(Info.FileInfo);
        auto TempPath = DestinationPath;
        Context->TempDestinationPath = std::move(TempPath += ".qsync");
        if (fs::exists(DestinationPath, Error)) {
            Context->FileExists = true;
            if (Error) {
                cerr << "Failed to test whether " << DestinationPath << " exists. " << Error << endl;
                return;
            }
            Context->SnapshotDestModTime = fs::last_write_time(DestinationPath, Error);
            if (Error) {
                cerr << "Failed to get lastwritetime on " << DestinationPath << " error: " << Error << endl;
                return;
            }
            Context->SnapshotDestSize = fs::file_size(DestinationPath, Error);
            if (Error) {
                cerr << "Failed to get file size from " << DestinationPath << " error: " << Error << endl;
                return;
            }
        }
        Context->DestinationPath = std::move(DestinationPath);
        MsQuicStream* Stream =
            new MsQuicStream(
                *Connection,
                QUIC_STREAM_OPEN_FLAG_NONE,
                CleanUpAutoDelete,
                QSyncServerDataStreamCallback,
                Context);
        if (QUIC_FAILED(Stream->GetInitStatus())) {
            cerr << "Failed to create Data stream: " << std::hex << Stream->GetInitStatus() << endl;
            delete Context;
            return;
        }
        Context->RefCount = 1; // Ref for the stream.
        Context->Stream = Stream;
        Stream->Send(Buffer, 1, QUIC_SEND_FLAG_FIN, Buffer);
        Stream->Start(QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);
    } else {
        // cout << "File current " << File.getPath().cStr() << endl;
        if (QUIC_FAILED(Status = ControlStream->Send(Buffer, 1, QUIC_SEND_FLAG_NONE, Buffer))) {
            cerr << "[CONTROL] Failed to Send File Id " << Id << " with error: " << std::hex << Status << endl;
            return;
        }
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS
QsyncServer::QsyncListenerCallback(
    _In_ HQUIC /*Listener*/,
    _In_opt_ void* Context,
    _Inout_ QUIC_LISTENER_EVENT* Event)
{
    auto This = (QsyncServer*)Context;
    switch (Event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
        This->Connection = make_unique<MsQuicConnection>(
                Event->NEW_CONNECTION.Connection,
                CleanUpManual,
                QsyncServerConnectionCallback,
                This);
        QUIC_STATUS Status = This->Connection->SetConfiguration(*This->Config);
        if (QUIC_FAILED(Status)) {
            cerr << "Failed to set configuration on connection: " << Status << endl;
            return QUIC_STATUS_CONNECTION_REFUSED;
        }
        break;
    }
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
QsyncServer::QsyncServerConnectionCallback(
    _In_ MsQuicConnection* /*Connection*/,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    auto This = (QsyncServer*)Context;
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        MsQuic->ListenerStop(*This->Listener);
        break;
    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
        if (!QcVerifyCertificate(This->CertPw, Event->PEER_CERTIFICATE_RECEIVED.Certificate)) {
            return QUIC_STATUS_BAD_CERTIFICATE;
        }
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        This->ControlStream = make_unique<MsQuicStream>(
            Event->PEER_STREAM_STARTED.Stream,
            CleanUpManual,
            QSyncServerControlStreamCallback,
            This);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        cout << "Connection shutdown" << endl;
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}


QUIC_STATUS
QsyncServer::QSyncServerControlStreamCallback(
    _In_ MsQuicStream* /*Stream*/,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    auto This = (QsyncServer*)Context;
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE: {
        for (auto BufIdx = 0u; BufIdx < Event->RECEIVE.BufferCount; BufIdx++) {
            auto CurrentBuffer = &Event->RECEIVE.Buffers[BufIdx];
            uint32_t i = This->SkipMessageBytes;
            This->SkipMessageBytes = 0;
            if (This->PartialData == PartialSize) {
                if (CurrentBuffer->Length < sizeof(This->RemainderSize) - This->RemainderFilled) {
                    // How did you get such a small receive???
                    memcpy(
                        This->RemainderSizeBytes + This->RemainderFilled,
                        CurrentBuffer->Buffer,
                        CurrentBuffer->Length);
                    This->RemainderFilled += CurrentBuffer->Length;
                    This->PartialData = PartialSize;
                    return QUIC_STATUS_SUCCESS;
                } else {
                    memcpy(
                        This->RemainderSizeBytes + This->RemainderFilled,
                        CurrentBuffer->Buffer,
                        sizeof(This->RemainderSize) - This->RemainderFilled);
                    i = sizeof(This->RemainderSize) - This->RemainderFilled;
                    This->AddFileToList(This, CurrentBuffer->Buffer + i, This->RemainderSize);
                    i += This->RemainderSize;
                    This->RemainderSize = 0;
                    This->RemainderFilled = 0;
                    This->PartialData = NoPartialData;
                }
            } else if (This->PartialData == PartialMessage) {
                if (CurrentBuffer->Length < This->RemainderMessage.Length - This->RemainderFilled) {
                    // Another partial receive...
                    memcpy(
                        This->RemainderMessage.Buffer + This->RemainderFilled,
                        CurrentBuffer->Buffer,
                        CurrentBuffer->Length);
                    This->RemainderFilled += CurrentBuffer->Length;
                    This->PartialData = PartialMessage;
                    return QUIC_STATUS_SUCCESS;
                } else {
                    memcpy(
                        This->RemainderMessage.Buffer + This->RemainderFilled,
                        CurrentBuffer->Buffer,
                        This->RemainderMessage.Length - This->RemainderFilled);
                    i = This->RemainderMessage.Length - This->RemainderFilled;
                    This->AddFileToList(This, This->RemainderMessage.Buffer, This->RemainderMessage.Length);
                    free(This->RemainderMessage.Buffer);
                    This->RemainderMessage.Buffer = nullptr;
                    This->RemainderMessage.Length = 0;
                    This->RemainderFilled = 0;
                    This->PartialData = NoPartialData;
                }
            }
            while (i < CurrentBuffer->Length) {
                uint32_t MessageSize = 0;
                if (CurrentBuffer->Length - i < sizeof(MessageSize)) {
                    This->RemainderFilled = CurrentBuffer->Length - i;
                    memcpy(&This->RemainderSize, CurrentBuffer->Buffer + i, CurrentBuffer->Length - i);
                    This->PartialData = PartialSize;
                    if (Event->RECEIVE.TotalBufferLength - i < sizeof(MessageSize)) {
                        // cout << "Not enough data received for size..." << Event->RECEIVE.TotalBufferLength - i << " vs " << sizeof(MessageSize) << endl;
                        return QUIC_STATUS_SUCCESS;
                    }
                    cout << "Continuing to read size in next buffer..." << endl;
                    break;
                }
                memcpy(&MessageSize, CurrentBuffer->Buffer + i, sizeof(MessageSize));
                i += sizeof(MessageSize);

                if (CurrentBuffer->Length - i < MessageSize) {
                    This->RemainderMessage.Length = MessageSize;
                    This->RemainderMessage.Buffer = (uint8_t*)malloc(This->RemainderMessage.Length);
                    if (This->RemainderMessage.Buffer == nullptr) {
                        // Skip this message
                        cout << "Failed to allocate buffer for partial message!" << endl;
                        This->RemainderMessage.Length = 0;
                        This->SkipMessageBytes = MessageSize - (CurrentBuffer->Length - i);
                        // TODO: indicate transfer error
                        if (Event->RECEIVE.TotalBufferLength - i < MessageSize) {
                            return QUIC_STATUS_SUCCESS;
                        }
                        break;
                    }
                    This->RemainderFilled = CurrentBuffer->Length - i;
                    memcpy(This->RemainderMessage.Buffer, CurrentBuffer->Buffer + i, CurrentBuffer->Length - i);
                    This->PartialData = PartialMessage;
                    if (Event->RECEIVE.TotalBufferLength - i < MessageSize) {
                        // cout << "Not enough data received for message..." << Event->RECEIVE.TotalBufferLength - i << " vs " << MessageSize << endl;
                        return QUIC_STATUS_SUCCESS;
                    }
                    cout << "Continuing to read message in next buffer" << endl;
                    break;
                }

                This->AddFileToList(This, CurrentBuffer->Buffer + i, MessageSize);
                i += MessageSize;
            }
        }
        break;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        free(Event->SEND_COMPLETE.ClientContext);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
QsyncServer::QSyncServerDataStreamCallback(
    _In_ MsQuicStream* /*Stream*/,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    auto This = (DataStreamContext*)Context;
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        if (QUIC_FAILED(Event->START_COMPLETE.Status)) {
            cerr << "Failed to start data stream: " << std::hex << Event->START_COMPLETE.Status << endl;
        }
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        ASSERT(Event->RECEIVE.BufferCount == 2);
        This->FinalReceive = !!(Event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN);
        memcpy(This->Buffers, Event->RECEIVE.Buffers, sizeof(QUIC_BUFFER) * Event->RECEIVE.BufferCount);
        This->BufferCount = Event->RECEIVE.BufferCount;
        ++This->RefCount;
        This->Server->IoPool.Enqueue(&QsyncServer::DataStreamContext::FileIoWorker, This);
        return QUIC_STATUS_PENDING;
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        free(Event->SEND_COMPLETE.ClientContext);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        if (--This->RefCount == 0) {
            delete This;
        }
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool
QsyncServer::Start(
    uint16_t ListenPort,
    const string& Root = "",
    const string& Password = "")
{
    error_code Error;
    if (Root.length() > 0) {
        BasePath = filesystem::canonical(Root, Error);
        if (Error) {
            cerr << "Failed to canonicalize '" << Root << "': " << Error << endl;
            return false;
        }
    } else {
        BasePath = filesystem::current_path(Error);
        if (Error) {
            cerr << "Failed to get current path: " << Error << endl;
            return false;
        }
    }
    Reg =
        make_unique<MsQuicRegistration>(
            QSYNC_ALPN,
            QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT,
            true);
    if (!Reg->IsValid()) {
        cerr <<
            "Failed to initialize server MsQuicRegistration: "
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

    Creds.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    if (Password.length() > 0) {
        // If password is provided, enable mutual authentication.
        Creds.Flags |=
            QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED
            | QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION
            | QUIC_CREDENTIAL_FLAG_DEFER_CERTIFICATE_VALIDATION;
        this->CertPw = Password;
    }
    Creds.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;

    MsQuicSettings Settings;
    Settings.SetPeerBidiStreamCount(1);
    Settings.SetDisconnectTimeoutMs(10000);
    Settings.SetSendBufferingEnabled(false);

    Config = make_unique<MsQuicConfiguration>(*Reg, Alpn, Settings, Creds);
    if (!Config->IsValid()) {
        cerr <<
            "Failed to initialize server MsQuicConfiguration: "
            << Config->GetInitStatus() << endl;
        return false;
    }

    QUIC_ADDR LocalAddr{};
    memset(&LocalAddr, 0, sizeof(LocalAddr));
    QuicAddrSetFamily(&LocalAddr, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&LocalAddr, ListenPort);

    Listener = make_unique<MsQuicListener>(*Reg, QsyncListenerCallback, this);
    if (!Listener->IsValid()) {
        cerr << "Failed to initialize MsQuicListener: "
            << Listener->GetInitStatus() << endl;
        return false;
    }

    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = Listener->Start(Alpn, &LocalAddr))) {
        cerr << "Failed to start listener: " << Status << endl;
        return false;
    }
    return true;
}
