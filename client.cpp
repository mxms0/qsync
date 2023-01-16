#include "qsync.h"

using namespace std;

const MsQuicAlpn Alpn(QSYNC_ALPN);


QUIC_STATUS
QsyncClient::QSyncClientControlStreamCallback(
    _In_ MsQuicStream* /*Stream*/,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    QsyncClient* This = (QsyncClient*)Context;
    (This);
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        cout << "Stream opened!" << endl;
        break;
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
QsyncClient::QsyncClientConnectionCallback(
    _In_ MsQuicConnection* Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    QsyncClient* This = (QsyncClient*)Context;
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        This->ControlStream = make_unique<MsQuicStream>(*Connection, QUIC_STREAM_OPEN_FLAG_NONE, CleanUpManual, QSyncClientControlStreamCallback, This);
        This->ControlStream->Start(
            QUIC_STREAM_START_FLAG_FAIL_BLOCKED | QUIC_STREAM_START_FLAG_IMMEDIATE | QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);
        cout << "Connected!" << endl;
        break;
    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
        if (!QcVerifyCertificate(This->CertPw, Event->PEER_CERTIFICATE_RECEIVED.Certificate)) {
            return QUIC_STATUS_BAD_CERTIFICATE;
        }
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
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

    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = Connection->Start(*Config, ServerAddr.c_str(), ServerPort))) {
        cerr << "Failed to start connection: " << Status << endl;
        return false;
    }
    SyncPath = StartPath;
    return true;
}
