// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/moonlight/MoonlightCrypto.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <cstring>
#include <memory>

namespace dish::moonlight::crypto {

namespace {

using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using MdCtx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;

CipherCtx newCipherCtx() { return CipherCtx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free); }

void putU16Le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

void putU32Le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

std::uint16_t readU16Le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t readU32Le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// The 16-byte control-stream IV: all zero except byte[0] = low byte of seq. This
// matches Wolf's construction (iv_data[0] = native_to_little(seq)); the byte
// vectors in the test suite depend on it.
std::array<std::uint8_t, 16> controlIv(std::uint32_t seq) {
    std::array<std::uint8_t, 16> iv{};
    iv[0] = static_cast<std::uint8_t>(seq & 0xFF);
    return iv;
}

PkeyPtr loadPrivateKey(const std::string& pem) {
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), &BIO_free);
    if (!bio) { return {nullptr, &EVP_PKEY_free}; }
    EVP_PKEY* key = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
    return {key, &EVP_PKEY_free};
}

PkeyPtr loadPublicKey(const std::string& pem) {
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), &BIO_free);
    if (!bio) { return {nullptr, &EVP_PKEY_free}; }
    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr);
    return {key, &EVP_PKEY_free};
}

} // namespace

std::array<std::uint8_t, kSha256Len> sha256(const std::uint8_t* data, std::size_t len) {
    std::array<std::uint8_t, kSha256Len> out{};
    unsigned int mdLen = 0;
    MdCtx ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (ctx && EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) == 1 &&
        EVP_DigestUpdate(ctx.get(), data, len) == 1 &&
        EVP_DigestFinal_ex(ctx.get(), out.data(), &mdLen) == 1) {
        return out;
    }
    out.fill(0);
    return out;
}

std::array<std::uint8_t, kSha256Len> sha256(const Bytes& data) {
    return sha256(data.data(), data.size());
}

std::array<std::uint8_t, kAesKey128> genAesKey(const std::uint8_t* salt, std::size_t saltLen,
                                               const std::string& pin) {
    Bytes buf;
    buf.reserve(saltLen + pin.size());
    buf.insert(buf.end(), salt, salt + saltLen);
    buf.insert(buf.end(), pin.begin(), pin.end());
    const auto digest = sha256(buf);
    std::array<std::uint8_t, kAesKey128> key{};
    std::memcpy(key.data(), digest.data(), kAesKey128);
    return key;
}

std::optional<Bytes> aesEcbEncrypt(const std::array<std::uint8_t, kAesKey128>& key,
                                   const Bytes& data) {
    if (data.empty() || (data.size() % kAesBlock) != 0) { return std::nullopt; }
    CipherCtx ctx = newCipherCtx();
    if (!ctx) { return std::nullopt; }
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1) {
        return std::nullopt;
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);
    Bytes out(data.size() + kAesBlock);
    int outLen = 0;
    if (EVP_EncryptUpdate(ctx.get(), out.data(), &outLen, data.data(),
                          static_cast<int>(data.size())) != 1) {
        return std::nullopt;
    }
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), out.data() + outLen, &finalLen) != 1) {
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(outLen + finalLen));
    return out;
}

std::optional<Bytes> aesEcbDecrypt(const std::array<std::uint8_t, kAesKey128>& key,
                                   const Bytes& data) {
    if (data.empty() || (data.size() % kAesBlock) != 0) { return std::nullopt; }
    CipherCtx ctx = newCipherCtx();
    if (!ctx) { return std::nullopt; }
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1) {
        return std::nullopt;
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);
    Bytes out(data.size() + kAesBlock);
    int outLen = 0;
    if (EVP_DecryptUpdate(ctx.get(), out.data(), &outLen, data.data(),
                          static_cast<int>(data.size())) != 1) {
        return std::nullopt;
    }
    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), out.data() + outLen, &finalLen) != 1) {
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(outLen + finalLen));
    return out;
}

std::optional<Bytes> sealControl(const std::array<std::uint8_t, kAesKey128>& key, std::uint32_t seq,
                                 const std::uint8_t* plaintext, std::size_t len) {
    CipherCtx ctx = newCipherCtx();
    if (!ctx) { return std::nullopt; }
    const auto iv = controlIv(seq);
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
        return std::nullopt;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()),
                            nullptr) != 1) {
        return std::nullopt;
    }
    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) {
        return std::nullopt;
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);

    Bytes cipher(len);
    int outLen = 0;
    if (len > 0 && EVP_EncryptUpdate(ctx.get(), cipher.data(), &outLen, plaintext,
                                     static_cast<int>(len)) != 1) {
        return std::nullopt;
    }
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), cipher.data() + outLen, &finalLen) != 1) {
        return std::nullopt;
    }
    cipher.resize(static_cast<std::size_t>(outLen + finalLen));

    std::array<std::uint8_t, kGcmTagSize> tag{};
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()),
                            tag.data()) != 1) {
        return std::nullopt;
    }

    // [type LE][len LE][seq LE][tag][ciphertext]. len = seq(4) + tag(16) + ct.
    const auto pktLen = static_cast<std::uint16_t>(4 + kGcmTagSize + cipher.size());
    Bytes out(4 + 4 + kGcmTagSize + cipher.size());
    putU16Le(out.data() + 0, 0x0001);
    putU16Le(out.data() + 2, pktLen);
    putU32Le(out.data() + 4, seq);
    std::memcpy(out.data() + 8, tag.data(), kGcmTagSize);
    if (!cipher.empty()) {
        std::memcpy(out.data() + 8 + kGcmTagSize, cipher.data(), cipher.size());
    }
    return out;
}

std::optional<Bytes> openControl(const std::array<std::uint8_t, kAesKey128>& key,
                                 const std::uint8_t* packet, std::size_t len) {
    // Minimum: type(2)+len(2)+seq(4)+tag(16) = 24.
    if (packet == nullptr || len < 24) { return std::nullopt; }
    if (readU16Le(packet) != 0x0001) { return std::nullopt; }
    const std::uint16_t pktLen = readU16Le(packet + 2);
    if (static_cast<std::size_t>(pktLen) + 4 > len) { return std::nullopt; }
    if (pktLen < 4 + kGcmTagSize) { return std::nullopt; }
    const std::uint32_t seq = readU32Le(packet + 4);
    const std::uint8_t* tag = packet + 8;
    const std::uint8_t* cipher = packet + 8 + kGcmTagSize;
    const std::size_t cipherLen = pktLen - 4 - kGcmTagSize;

    CipherCtx ctx = newCipherCtx();
    if (!ctx) { return std::nullopt; }
    const auto iv = controlIv(seq);
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
        return std::nullopt;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()),
                            nullptr) != 1) {
        return std::nullopt;
    }
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) {
        return std::nullopt;
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);

    Bytes plain(cipherLen);
    int outLen = 0;
    if (cipherLen > 0 && EVP_DecryptUpdate(ctx.get(), plain.data(), &outLen, cipher,
                                           static_cast<int>(cipherLen)) != 1) {
        return std::nullopt;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kGcmTagSize),
                            const_cast<std::uint8_t*>(tag)) != 1) {
        return std::nullopt;
    }
    int finalLen = 0;
    // Non-1 here means the tag did not verify: tampered or wrong key.
    if (EVP_DecryptFinal_ex(ctx.get(), plain.data() + outLen, &finalLen) != 1) {
        return std::nullopt;
    }
    plain.resize(static_cast<std::size_t>(outLen + finalLen));
    return plain;
}

ControlSealer::ControlSealer(const std::array<std::uint8_t, kAesKey128>& key) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) { return; }
    const auto iv = controlIv(0);
    // Cipher + IV length + key are configured ONCE; per-packet seals only swap
    // the IV, so the AES key schedule is built a single time.
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) !=
            1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return;
    }
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    ctx_ = ctx;
    // Sized once for the largest control payload we ever send (input packets are
    // tens of bytes); seal() never grows it, so the hot path never allocates.
    buffer_.resize(8 + kGcmTagSize + 256);
}

ControlSealer::~ControlSealer() {
    if (ctx_ != nullptr) { EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(ctx_)); }
}

const std::uint8_t* ControlSealer::seal(std::uint32_t seq, const std::uint8_t* plaintext,
                                        std::size_t len, std::size_t* outLen) noexcept {
    if (ctx_ == nullptr || outLen == nullptr || len + 8 + kGcmTagSize > buffer_.size()) {
        return nullptr;
    }
    auto* ctx = static_cast<EVP_CIPHER_CTX*>(ctx_);
    const auto iv = controlIv(seq);
    // Re-arm with the new IV only; cipher and key are retained from the ctor.
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, nullptr, iv.data()) != 1) { return nullptr; }

    std::uint8_t* cipherOut = buffer_.data() + 8 + kGcmTagSize;
    int outBytes = 0;
    if (len > 0 &&
        EVP_EncryptUpdate(ctx, cipherOut, &outBytes, plaintext, static_cast<int>(len)) != 1) {
        return nullptr;
    }
    int finalBytes = 0;
    if (EVP_EncryptFinal_ex(ctx, cipherOut + outBytes, &finalBytes) != 1) { return nullptr; }
    const auto cipherLen = static_cast<std::size_t>(outBytes + finalBytes);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kGcmTagSize),
                            buffer_.data() + 8) != 1) {
        return nullptr;
    }
    putU16Le(buffer_.data() + 0, 0x0001);
    putU16Le(buffer_.data() + 2, static_cast<std::uint16_t>(4 + kGcmTagSize + cipherLen));
    putU32Le(buffer_.data() + 4, seq);
    *outLen = 8 + kGcmTagSize + cipherLen;
    return buffer_.data();
}

std::optional<Bytes> rsaSign(const std::string& privateKeyPem, const std::uint8_t* msg,
                             std::size_t len) {
    PkeyPtr key = loadPrivateKey(privateKeyPem);
    if (!key) { return std::nullopt; }
    MdCtx ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx) { return std::nullopt; }
    if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1) {
        return std::nullopt;
    }
    if (EVP_DigestSignUpdate(ctx.get(), msg, len) != 1) { return std::nullopt; }
    std::size_t sigLen = 0;
    if (EVP_DigestSignFinal(ctx.get(), nullptr, &sigLen) != 1) { return std::nullopt; }
    Bytes sig(sigLen);
    if (EVP_DigestSignFinal(ctx.get(), sig.data(), &sigLen) != 1) { return std::nullopt; }
    sig.resize(sigLen);
    return sig;
}

bool rsaVerify(const std::string& publicKeyPem, const std::uint8_t* msg, std::size_t msgLen,
               const std::uint8_t* sig, std::size_t sigLen) {
    PkeyPtr key = loadPublicKey(publicKeyPem);
    if (!key) { return false; }
    MdCtx ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx) { return false; }
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1) {
        return false;
    }
    if (EVP_DigestVerifyUpdate(ctx.get(), msg, msgLen) != 1) { return false; }
    return EVP_DigestVerifyFinal(ctx.get(), sig, sigLen) == 1;
}

Bytes randomBytes(std::size_t n) {
    Bytes out(n);
    if (n > 0 && RAND_bytes(out.data(), static_cast<int>(n)) != 1) {
        out.assign(n, 0); // never hand back partially-initialised randomness
    }
    return out;
}

std::string hexEncode(const std::uint8_t* data, std::size_t len) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0xF]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

std::string hexEncode(const Bytes& data) { return hexEncode(data.data(), data.size()); }

std::optional<Bytes> hexDecode(const std::string& hex) {
    if ((hex.size() % 2) != 0) { return std::nullopt; }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    };
    Bytes out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) { return std::nullopt; }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

} // namespace dish::moonlight::crypto
