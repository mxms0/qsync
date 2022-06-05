//
// Licensed under the MIT license.
// Copyright (c) rossica 2022
//
use openssl::asn1::Asn1Time;
use openssl::bn;
use openssl::error::ErrorStack;
use openssl::nid::Nid;
use openssl::pkcs12::Pkcs12;
use openssl::pkey::{PKey, Private};
use openssl::pkey::Id;
use openssl::pkcs5::pbkdf2_hmac;
use openssl::rand::rand_bytes;
use openssl::hash::MessageDigest;
use openssl::x509::{X509Builder, X509NameBuilder, X509, X509NameRef};

use std::cmp::Ordering;
use std::time::{SystemTime, UNIX_EPOCH, Duration};

const PBKDF_ITERATIONS:usize = 15000;
const ED448_KEYLEN:usize = 57;
const SIGNING_SALT_LENGTH:usize = 64;

fn generate_signing_key(password: &str, salt: &[u8]) -> Result<PKey<Private>, ErrorStack> {
    let mut signing_bytes: [u8; ED448_KEYLEN] = [0; ED448_KEYLEN];
    match pbkdf2_hmac(password.as_bytes(), salt, PBKDF_ITERATIONS, MessageDigest::sha512(), &mut signing_bytes) {
        Err(why) => { return Err(why); }
        Ok(_) => {}
    }

    match PKey::private_key_from_raw_bytes(&signing_bytes, Id::ED448) {
        Err(why) => Err(why),
        Ok(key) => Ok(key)
    }
}

fn check_cert_name(actual_name: &X509NameRef, expected_name: &str, log_name: &str) -> bool {
    let mut checked = false;
    let mut itr_count = 0;
    for entry in actual_name.entries() {
        if itr_count == 0 {
            match entry.data().as_slice().cmp(expected_name.as_bytes()) {
                Ordering::Equal => {
                    checked = true;
                }
                _ => {
                    println!("{} name doesn't match! {:?} vs {}", log_name, entry.data(), expected_name);
                    return false;
                }
            }
            itr_count += 1;
        } else {
            println!("Multiple entries in {} name!", log_name);
            return false;
        }
    }
    if !checked {
        println!("No {} name on the certificate!", log_name);
        return false;
    }
    return true;
}

    /// Generates a PKCS12 with a ED448 certificate and private key
    /// signed with a ED448 key derived from the password.
    /// The PKCS12 has an empty password, and null encryption, so it
    /// MUST remain in memory and not be written to disk.
    ///
    /// # Examples
    ///
    /// Usage:
    ///
    /// ```
    /// let pkcs12 = generate_auth_certificate("password", "app").unwrap();
    /// let pkcs12 = Pkcs12::from_der(&pkcs12).unwrap();
    /// let pkcs12 = pkcs12.parse("").unwrap();
    /// verify_auth_certificate("password", "app", &pkcs12.cert).expect("Cert verification failed");
    /// ```
pub fn generate_auth_certificate(password: &str, cert_name: &str) -> Result<Vec<u8>, bool> {
    let mut salt_bytes: [u8; SIGNING_SALT_LENGTH] = [0; SIGNING_SALT_LENGTH];

    if let Err(error) = rand_bytes(&mut salt_bytes) {
        println!("Failed to get random bytes: {}", error);
        return Err(false);
    }

    let salt = match bn::BigNum::from_slice(&salt_bytes) {
        Err(error) => {
            println!("Failed to create BigNum from salt bytes: {}", error);
            return Err(false);
        }
        Ok(bn) => bn
    };

    let serial_number = match salt.to_asn1_integer() {
        Err(error) => {
            println!("Failed to convert BigNum to ASN1 integer: {}", error);
            return Err(false);
        }
        Ok(sn) => sn
    };

    let mut cert_builder = match X509Builder::new() {
        Err(error) => {
            println!("Failed to create new X509Builder: {}", error);
            return Err(false);
        }
        Ok(x509) => x509
    };

    if let Err(error) = cert_builder.set_version(2) {
        println!("Failed to set version number: {}", error);
        return Err(false);
    }

    if let Err(error) = cert_builder.set_serial_number(&serial_number) {
        println!("Failed to set serial number: {}", error);
        return Err(false);
    }

    let not_after = match Asn1Time::days_from_now(366) {
        Err(err) => {
            println!("Failed to create NotAfter time: {}", err);
            return Err(false);
        }
        Ok(val) => val
    };

    if let Err(error) = cert_builder.set_not_after(&not_after) {
        println!("Failed to set NotAfter date: {}", error);
        return Err(false);
    }

    let now = SystemTime::now();
    let five_minutes_ago = match now.checked_sub(Duration::from_secs(300)) {
        None => {
            println!("Failed to subtract 5 minutes from current time");
            return Err(false);
        }
        Some(time) => time
    };

    let not_before = match five_minutes_ago.duration_since(UNIX_EPOCH) {
        Err(why) => {
            println!("failed to get time since UNIX epoch: {}", why);
            return Err(false);
        }
        Ok(time) => time
    };

    let not_before = not_before.as_secs() as i64;

    let not_before = match Asn1Time::from_unix(not_before) {
        Err(why) => {
            println!("Failed to create NotBefore time: {}", why);
            return Err(false);
        }
        Ok(time) => time
    };

    if let Err(error) = cert_builder.set_not_before(&not_before) {
        println!("Failed to set NotBefore date: {}", error);
        return Err(false);
    }

    let private_key = match PKey::generate_ed448() {
        Err(error) => {
            println!("Keygen failed: {}", error);
            return Err(false);
        }
        Ok(key) => key
    };

    if let Err(error) = cert_builder.set_pubkey(&private_key) {
        println!("Failed to set the private key on the cert: {}", error);
        return Err(false);
    }

    let mut name_builder = match X509NameBuilder::new() {
        Err(why) => {
            println!("Failed to create X509NameBuilder: {}", why);
            return Err(false);
        }
        Ok(builder) => builder
    };

    if let Err(error) = name_builder.append_entry_by_text("CN", cert_name) {
        println!("Failed to create name: {}", error);
        return Err(false);
    }

    let name = name_builder.build();

    if let Err(error) = cert_builder.set_subject_name(&name) {
        println!("Failed to set subject name: {}", error);
        return Err(false);
    }
    
    if let Err(error) = cert_builder.set_issuer_name(&name) {
        println!("Failed to set issuer name: {}", error);
        return Err(false);
    }

    let signing_key = match generate_signing_key(password, &salt_bytes) {
        Err(why) => {
            println!("Failed to generate signing key: {}", why);
            return Err(false);
        }
        Ok(key) => key
    };

    if let Err(error) = cert_builder.sign(&signing_key, MessageDigest::null()) {
        println!("Failed to sign certificate: {}", error);
        return Err(false);
    }

    let cert = cert_builder.build();

    let mut pkcs12_builder = Pkcs12::builder();
    pkcs12_builder.key_algorithm(Nid::from_raw(-1));
    pkcs12_builder.cert_algorithm(Nid::from_raw(-1));
    pkcs12_builder.key_iter(0);
    pkcs12_builder.mac_iter(0);

    let pkcs12 = match pkcs12_builder.build("", cert_name, &private_key, &cert) {
        Err(why) => {
            println!("Failed to create Pkcs12: {}", why);
            return Err(false);
        }
        Ok(pfx) => pfx
    };

    match pkcs12.to_der() {
        Err(why) => {
            println!("Failed to export pkcs12: {}", why);
            Err(false)
        }
        Ok(der) => Ok(der)
    }
}

pub fn verify_auth_certificate(password: &str, cert_name: &str, cert: &X509) -> Result<(), bool> {
    let serial_number = cert.serial_number();

    let serial_number = match serial_number.to_bn() {
        Err(why) => {
            println!("Failed to convert ASN SerialNumber to BIGNUM Salt: {}", why);
            return Err(false);
        }
        Ok(sn) => sn
    };

    if serial_number.num_bytes() > SIGNING_SALT_LENGTH as i32 {
        println!(
            "Serial number is not correct size! {} vs {}",
            serial_number.num_bytes(),
            SIGNING_SALT_LENGTH);
        return Err(false);
    }

    if serial_number.num_bits() < 64 {
        println!("Serial number is less than 64 bits in length!");
        return Err(false);
    }

    let salt = match serial_number.to_vec_padded(SIGNING_SALT_LENGTH as i32) {
        Err(why) => {
            println!("BIGNUM conversion to binary failed: {}", why);
            return Err(false);
        }
        Ok(sn) => sn
    };

    let signing_key = match generate_signing_key(password, &salt) {
        Err(why) => {
            println!("Failed to generate signing key: {}", why);
            return Err(false);
        }
        Ok(key) => key
    };

    let now = match Asn1Time::days_from_now(0) {
        Err(why) => {
            println!("Failed to create ASN1Time: {}", why);
            return Err(false);
        }
        Ok(time) => time
    };

    match cert.not_before().compare(&now) {
        Err(why) => {
            println!("Failed to compare NotBefore and Now: {}", why);
            return Err(false);
        }
        Ok(result) => match result {
            Ordering::Greater => {
                println!("Cert is not yet valid!");
                return Err(false);
            }
            _ => {}
        }
    }

    match cert.not_after().compare(&now) {
        Err(why) => {
            println!("Failed to compare NotAfter and Now: {}", why);
            return Err(false);
        }
        Ok(result) => match result {
            Ordering::Less => {
                println!("Cert is no longer valid!");
                return Err(false);
            }
            _ => {}
        }

    }

    if !check_cert_name(cert.issuer_name(), cert_name, "Issuer") ||
        !check_cert_name(cert.subject_name(), cert_name, "Subject") {
        return Err(false);
    }

    match cert.verify(&signing_key) {
        Err(why) => {
            println!("Certificate signature validation failed: {}", why);
            Err(false)
        }
        Ok(valid) => {
            if valid {
                Ok(())
            } else {
                println!("The signature is invalid!");
                Err(false)
            }
        }
    }
}
