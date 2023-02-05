#include "qsync.h"

using namespace std;

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
        uint32_t i = This->SkipMessageBytes;
        This->SkipMessageBytes = 0;
        if (This->PartialData == PartialSize) {
            if (Event->RECEIVE.Buffers[0].Length < sizeof(This->RemainderSize) - This->RemainderFilled) {
                // How did you get such a small receive???
                memcpy(
                    This->RemainderSizeBytes + This->RemainderFilled,
                    Event->RECEIVE.Buffers[0].Buffer,
                    Event->RECEIVE.Buffers[0].Length);
                This->RemainderFilled += Event->RECEIVE.Buffers[0].Length;
                This->PartialData = PartialSize;
                return QUIC_STATUS_SUCCESS;
            } else {
                memcpy(
                    This->RemainderSizeBytes + This->RemainderFilled,
                    Event->RECEIVE.Buffers[0].Buffer,
                    sizeof(This->RemainderSize) - This->RemainderFilled);
                i = sizeof(This->RemainderSize) - This->RemainderFilled;
                PrintFilePath(Event->RECEIVE.Buffers[0].Buffer + i, This->RemainderSize);
                i += This->RemainderSize;
                This->RemainderSize = 0;
                This->RemainderFilled = 0;
                This->PartialData = NoPartialData;
            }
        } else if (This->PartialData == PartialMessage) {
             if (Event->RECEIVE.Buffers[0].Length < This->RemainderMessage.Length - This->RemainderFilled) {
                // Another partial receive...
                memcpy(
                    This->RemainderMessage.Buffer + This->RemainderFilled,
                    Event->RECEIVE.Buffers[0].Buffer,
                    Event->RECEIVE.Buffers[0].Length);
                This->RemainderFilled += Event->RECEIVE.Buffers[0].Length;
                This->PartialData = PartialMessage;
                return QUIC_STATUS_SUCCESS;
             } else {
                memcpy(
                    This->RemainderMessage.Buffer + This->RemainderFilled,
                    Event->RECEIVE.Buffers[0].Buffer,
                    This->RemainderMessage.Length - This->RemainderFilled);
                i = This->RemainderMessage.Length - This->RemainderFilled;
                PrintFilePath(This->RemainderMessage.Buffer, This->RemainderMessage.Length);
                free(This->RemainderMessage.Buffer);
                This->RemainderMessage.Buffer = nullptr;
                This->RemainderMessage.Length = 0;
                This->RemainderFilled = 0;
                This->PartialData = NoPartialData;
             }
        }
        while (i < Event->RECEIVE.Buffers[0].Length) {
            uint32_t MessageSize = 0;
            if (Event->RECEIVE.Buffers[0].Length - i < sizeof(MessageSize)) {
                if (Event->RECEIVE.TotalBufferLength - i >= sizeof(MessageSize) || Event->RECEIVE.TotalBufferLength > Event->RECEIVE.Buffers[0].Length) {
                    cout << "Not enough space in first buffer for size..." << Event->RECEIVE.Buffers[0].Length - i << " vs " << sizeof(MessageSize) << endl;
                    Event->RECEIVE.TotalBufferLength = i;
                    return QUIC_STATUS_CONTINUE;
                } else {
                    cout << "Not enough data received for size..." << Event->RECEIVE.TotalBufferLength - i << " vs " << sizeof(MessageSize) << endl;
                    This->RemainderFilled = Event->RECEIVE.Buffers[0].Length - i;
                    memcpy(&This->RemainderSize, Event->RECEIVE.Buffers[0].Buffer + i, Event->RECEIVE.Buffers[0].Length - i);
                    This->PartialData = PartialSize;
                    return QUIC_STATUS_SUCCESS;
                }
            }
            memcpy(&MessageSize, Event->RECEIVE.Buffers[0].Buffer + i, sizeof(MessageSize));
            i += sizeof(MessageSize);

            if (Event->RECEIVE.Buffers[0].Length - i < MessageSize) {
                if (Event->RECEIVE.TotalBufferLength - i >= MessageSize || Event->RECEIVE.TotalBufferLength > Event->RECEIVE.Buffers[0].Length) {
                    cout << "Not enough space for message..." <<Event->RECEIVE.Buffers[0].Length - i << " vs " << MessageSize << endl;
                    Event->RECEIVE.TotalBufferLength = i;
                    return QUIC_STATUS_CONTINUE;
                } else {
                    cout << "Not enough data received for message..." << Event->RECEIVE.TotalBufferLength - i << " vs " << MessageSize << endl;
                    This->RemainderMessage.Length = MessageSize;
                    This->RemainderMessage.Buffer = (uint8_t*)malloc(This->RemainderMessage.Length);
                    if (This->RemainderMessage.Buffer == nullptr) {
                        // Skip this message
                        cout << "Failed to allocate buffer for partial message!" << endl;
                        This->RemainderMessage.Length = 0;
                        This->SkipMessageBytes = MessageSize - (Event->RECEIVE.Buffers[0].Length - i);
                        return QUIC_STATUS_SUCCESS;
                    }
                    This->RemainderFilled = Event->RECEIVE.Buffers[0].Length - i;
                    memcpy(This->RemainderMessage.Buffer, Event->RECEIVE.Buffers[0].Buffer + i, Event->RECEIVE.Buffers[0].Length - i);
                    This->PartialData = PartialMessage;
                    return QUIC_STATUS_SUCCESS;
                }
            }

            PrintFilePath(Event->RECEIVE.Buffers[0].Buffer + i, MessageSize);
            i += MessageSize;
        }
        break;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        (This);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool
QsyncServer::Start(
    uint16_t ListenPort,
    const string& Password = "")
{
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
