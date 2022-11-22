use std::marker::PhantomPinned;
use std::os::raw::c_void;
use std::{pin::Pin, ptr::null};

use msquic::{self, Buffer};
use openssl::x509::X509Object;

use crate::auth::{generate_auth_certificate, verify_auth_certificate};

const APP_NAME: &[u8] = "qsync".as_bytes();

pub struct QsyncServer {
    api: msquic::Api,
    registration: msquic::Registration,
    config: msquic::Configuration,
    listener: Option<msquic::Listener>,
    password: String,
    _pin: PhantomPinned,
}

extern "C" fn qsync_connection_handler(
    connection: msquic::Handle,
    context: *mut c_void,
    event: &msquic::ConnectionEvent,
) -> u32 {
    let mut result = 0;
    unsafe {
        let server = Box::from_raw(context as *mut QsyncServer);
        match event.event_type {
            msquic::CONNECTION_EVENT_SHUTDOWN_COMPLETE => {
                server.api.close_connection(connection);
            }
            msquic::CONNECTION_EVENT_CONNECTED => {
                // TODO: handle 
            }
            msquic::CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED => {
                // TODO: Validate peer certificate
                //event.payload.
                // if (verify_auth_certificate(&server.password, "qsync", cert).is_err()) {
                //     result = 1
                // }
            }
            _ => {

            }
        }
        let _ = Box::into_raw(server); // remove from the box so it doesn't get freed
    }
    result
}

extern "C" fn qsync_listener_handler(
    _: *const c_void, // Listener
    context: *mut c_void,
    event: &msquic::ListenerEvent,
) -> u32 {
    unsafe {
        let server = Box::from_raw(context as *mut QsyncServer);
        match event.event_type {
            msquic::LISTENER_EVENT_NEW_CONNECTION => {
                let new_conn = msquic::Connection::from_parts(
                    event.payload.new_connection.connection,
                    &server.api,
                );
                new_conn.set_configuration(&server.config);
                new_conn.set_callback_handler(qsync_connection_handler, context);
            }
            _ => {}
        }
        let _ = Box::into_raw(server); // remove from the box so it doesn't get freed
    }
    0
}

impl QsyncServer {
    pub fn Start(listen_port: u16, password: &str) -> Result<Pin<Box<Self>>, ()> {
        let pkcs12 = match generate_auth_certificate(password, "qsync") {
            Err(_) => {
                return Err(());
            }
            Ok(val) => val,
        };
        let api = msquic::Api::new();
        let reg_config = msquic::RegistrationConfig {
            app_name: APP_NAME.as_ptr() as *const i8,
            execution_profile: msquic::EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT,
        };
        let alpn = Buffer {
            buffer: APP_NAME.as_ptr() as *mut u8, // This won't actually be mutated, so it's "safe"
            length: APP_NAME.len() as u32,
        };
        let listen_addr = msquic::Addr::ipv4(0, listen_port, 0);
        let reg = msquic::Registration::new(&api, &reg_config);
        let mut settings = msquic::Settings::new();
        settings.set_peer_unidi_stream_count(1);
        let config = msquic::Configuration::new(&reg, &alpn, &settings);
        let pkcs12_config = msquic::CertificatePkcs12 {
            ans1_blob: pkcs12.as_ptr(),
            ans1_blob_length: pkcs12.len() as u32,
            private_key_password: null(),
        };
        let cert_config = msquic::CertificateUnion {
            pkcs12: &pkcs12_config,
        };
        let cred_config = msquic::CredentialConfig {
            allowed_cipher_suites: msquic::ALLOWED_CIPHER_SUITE_NONE,
            async_handler: None,
            cred_type: msquic::CREDENTIAL_TYPE_CERTIFICATE_PKCS12,
            cred_flags: msquic::CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED
                | msquic::CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION
                | msquic::CREDENTIAL_FLAG_DEFER_CERTIFICATE_VALIDATION,
            certificate: cert_config,
            principle: null(),
            reserved: null(),
        };
        config.load_credential(&cred_config);
        let server = QsyncServer {
            api: api,
            registration: reg,
            config: config,
            listener: None,
            password: String::from(password),
            _pin: PhantomPinned,
        };
        let server_box = Box::new(server);
        let server_ptr = Box::into_raw(server_box);
        unsafe {
            let mut server_box = Box::from_raw(server_ptr);
            let listener = msquic::Listener::new(
                &server_box.registration,
                qsync_listener_handler,
                server_ptr as *const c_void,
            );
            server_box.listener = Some(listener);
            server_box
                .listener
                .as_ref()
                .expect("Listener not initialized!")
                .start(&alpn, 1, &listen_addr);
            let server_pin = Box::into_pin(server_box);
            Ok(server_pin)
        }
    }
}
