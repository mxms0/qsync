#pragma once

class QsyncServer {
    uint32_t Pkcs12Length;

    QUIC_CERTIFICATE_PKCS12 Pkcs12Config;
    MsQuicCredentialConfig Creds;
    std::unique_ptr<MsQuicStream> ControlStream;
    std::unique_ptr<MsQuicConnection> Connection;
    std::unique_ptr<uint8_t[]> Pkcs12;
    std::unique_ptr<MsQuicConfiguration> Config;
    std::unique_ptr<MsQuicListener> Listener;
    std::unique_ptr<MsQuicRegistration> Reg;
    std::string CertPw;

public:
    QsyncServer() = default;
    QsyncServer(const QsyncServer&) = delete;
    QsyncServer(QsyncServer&&) = default;
    ~QsyncServer() = default;

    bool
    Start(
        uint16_t ListenPort,
        const std::string& Password);

private:
    static
    QUIC_STATUS
    QsyncListenerCallback(
        _In_ HQUIC /*Listener*/,
        _In_opt_ void* Context,
        _Inout_ QUIC_LISTENER_EVENT* Event);

    static
    QUIC_STATUS
    QsyncServerConnectionCallback(
        _In_ MsQuicConnection* /*Connection*/,
        _In_opt_ void* Context,
        _Inout_ QUIC_CONNECTION_EVENT* Event
        );

    static
    QUIC_STATUS
    QSyncServerControlStreamCallback(
        _In_ MsQuicStream* /*Stream*/,
        _In_opt_ void* Context,
        _Inout_ QUIC_STREAM_EVENT* Event
    );
};
