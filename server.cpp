#include "qsync.h"

using namespace std;

const MsQuicAlpn Alpn(QSYNC_ALPN);

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
        auto Conn =
            new MsQuicConnection(
                Event->NEW_CONNECTION.Connection,
                CleanUpAutoDelete,
                QsyncServerConnectionCallback,
                nullptr); // todo - Create context class for each connection
        This->Connections.push_back(Conn);
        QUIC_STATUS Status = Conn->SetConfiguration(*This->Config);
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
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Event);
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
    }
    Creds.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;

    MsQuicSettings Settings;
    Settings.SetPeerBidiStreamCount(100);

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

