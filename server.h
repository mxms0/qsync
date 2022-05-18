#pragma once

class QsyncServer {
    uint32_t Pkcs12Length;

    QUIC_CERTIFICATE_PKCS12 Pkcs12Config;
    MsQuicCredentialConfig Creds;
    std::unique_ptr<MsQuicConnection> Connection;
    std::unique_ptr<uint8_t[]> Pkcs12;
    std::unique_ptr<MsQuicConfiguration> Config;
    std::unique_ptr<MsQuicListener> Listener;
    std::unique_ptr<MsQuicRegistration> Reg;

    QsyncServer(const QsyncServer&) = delete;

    bool
    Start(
        uint16_t ListenPort,
        const std::string& Password);

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
};
