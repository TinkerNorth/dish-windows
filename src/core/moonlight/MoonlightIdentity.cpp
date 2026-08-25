// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/moonlight/MoonlightIdentity.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>

namespace dish::moonlight {

namespace {

using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;

PkeyPtr generateRsaKey() {
    EVP_PKEY* pkey = nullptr;
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), &EVP_PKEY_CTX_free);
    if (!ctx) { return {nullptr, &EVP_PKEY_free}; }
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0) { return {nullptr, &EVP_PKEY_free}; }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) <= 0) {
        return {nullptr, &EVP_PKEY_free};
    }
    if (EVP_PKEY_keygen(ctx.get(), &pkey) <= 0) { return {nullptr, &EVP_PKEY_free}; }
    return {pkey, &EVP_PKEY_free};
}

std::string pemFromBio(BIO* bio) {
    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    if (mem == nullptr || mem->data == nullptr) { return {}; }
    return std::string(mem->data, mem->length);
}

X509Ptr parseCert(const std::string& pem) {
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), &BIO_free);
    if (!bio) { return {nullptr, &X509_free}; }
    X509* cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
    return {cert, &X509_free};
}

} // namespace

std::optional<Identity> generateIdentity() {
    PkeyPtr pkey = generateRsaKey();
    if (!pkey) { return std::nullopt; }

    X509Ptr cert(X509_new(), &X509_free);
    if (!cert) { return std::nullopt; }

    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
    X509_set_version(cert.get(), 2);
    // 20 years, matching the reference host implementations so a persisted
    // identity outlives any realistic install.
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 630720000L);
    X509_set_pubkey(cert.get(), pkey.get());

    X509_NAME* name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("dish"), -1, -1, 0);
    X509_set_issuer_name(cert.get(), name); // self-signed: issuer == subject

    if (X509_sign(cert.get(), pkey.get(), EVP_sha256()) == 0) { return std::nullopt; }

    BioPtr certBio(BIO_new(BIO_s_mem()), &BIO_free);
    BioPtr keyBio(BIO_new(BIO_s_mem()), &BIO_free);
    if (!certBio || !keyBio) { return std::nullopt; }
    if (PEM_write_bio_X509(certBio.get(), cert.get()) == 0) { return std::nullopt; }
    if (PEM_write_bio_PrivateKey(keyBio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr) ==
        0) {
        return std::nullopt;
    }

    Identity id;
    id.certPem = pemFromBio(certBio.get());
    id.privateKeyPem = pemFromBio(keyBio.get());
    if (id.certPem.empty() || id.privateKeyPem.empty()) { return std::nullopt; }
    return id;
}

std::optional<std::vector<std::uint8_t>> certSignature(const std::string& certPem) {
    X509Ptr cert = parseCert(certPem);
    if (!cert) { return std::nullopt; }
    const ASN1_BIT_STRING* sig = nullptr;
    X509_get0_signature(&sig, nullptr, cert.get());
    if (sig == nullptr || sig->data == nullptr) { return std::nullopt; }
    return std::vector<std::uint8_t>(sig->data, sig->data + sig->length);
}

std::optional<std::string> certPublicKeyPem(const std::string& certPem) {
    X509Ptr cert = parseCert(certPem);
    if (!cert) { return std::nullopt; }
    PkeyPtr pub(X509_get_pubkey(cert.get()), &EVP_PKEY_free);
    if (!pub) { return std::nullopt; }
    BioPtr bio(BIO_new(BIO_s_mem()), &BIO_free);
    if (!bio) { return std::nullopt; }
    if (PEM_write_bio_PUBKEY(bio.get(), pub.get()) == 0) { return std::nullopt; }
    std::string pem = pemFromBio(bio.get());
    if (pem.empty()) { return std::nullopt; }
    return pem;
}

} // namespace dish::moonlight
