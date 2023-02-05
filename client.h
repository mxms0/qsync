#pragma once

class QsyncClient {
    uint32_t Pkcs12Length;

    QUIC_CERTIFICATE_PKCS12 Pkcs12Config;
    MsQuicCredentialConfig Creds;
    std::unique_ptr<uint8_t[]> Pkcs12;
    std::unique_ptr<MsQuicRegistration> Reg;
    std::unique_ptr<MsQuicConfiguration> Config;
    std::unique_ptr<MsQuicConnection> Connection;
    std::unique_ptr<MsQuicStream> ControlStream;
    std::string CertPw;
    std::string SyncPath;

public:
    QsyncClient() = default;
    QsyncClient(const QsyncClient&) = delete;
    QsyncClient(QsyncClient&&) = default;
    ~QsyncClient() = default;

    bool
    Start(
        const std::string& ServerAddr,
        uint16_t ServerPort,
        const std::string& SyncPath,
        const std::string& Password);

private:
    static
    QUIC_STATUS
    QsyncClientConnectionCallback(
        _In_ MsQuicConnection* Connection,
        _In_opt_ void* Context,
        _Inout_ QUIC_CONNECTION_EVENT* Event);

    static
    QUIC_STATUS
    QSyncClientControlStreamCallback(
        _In_ MsQuicStream* /*Stream*/,
        _In_opt_ void* Context,
        _Inout_ QUIC_STREAM_EVENT* Event);

};
