// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Moonlight crypto vectors. The GCM control-packet vectors are Wolf's exact
// testControl.cpp outputs, which prove byte-for-byte wire interoperability; the
// rest are round-trip and tamper-rejection tests.

#include "core/moonlight/MoonlightCrypto.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace c = dish::moonlight::crypto;

namespace {

std::array<std::uint8_t, 16> key16(const std::string& hex) {
    const auto b = *c::hexDecode(hex);
    std::array<std::uint8_t, 16> k{};
    for (std::size_t i = 0; i < 16 && i < b.size(); ++i) { k[i] = b[i]; }
    return k;
}

// Seal `payloadHex` under `keyHex`/`seq` and return the full packet as hex.
std::string seal(const std::string& keyHex, std::uint32_t seq, const std::string& payloadHex) {
    const auto key = key16(keyHex);
    const auto pt = *c::hexDecode(payloadHex);
    const auto out = c::sealControl(key, seq, pt.data(), pt.size());
    REQUIRE(out.has_value());
    return c::hexEncode(*out);
}

} // namespace

TEST_CASE("sealControl reproduces Wolf's control packet vectors", "[moonlight][crypto][gcm]") {
    const std::string key = "EDF04A215C4FBEA20934120C8480D855";
    REQUIRE(seal(key, 0, "020302000000") ==
            "01001A0000000000BF0EB6DA10E47C702EC8644EB87D9CF7B6FAC9FF75CA");
    REQUIRE(seal(key, 1, "0703010000") ==
            "010019000100000021DBB8DC0590AF3A2B20BCE5A347DE31D366E5B9C5");
    REQUIRE(seal(key, 2, "000208000400000000000000") ==
            "0100200002000000220722FBADED58A03F2E8898F0F1DCB7C93F6235590618E4186AD990");
    REQUIRE(seal(key, 6, "060212000000000E05000000033400C00000059F0329") ==
            "01002A00060000005A4D999FB2542F85BDD39D99F77EB825254569D2C04E21241B5CEC01BD3F93129718EC"
            "C1F153");
}

TEST_CASE("openControl round-trips and evolves with seq", "[moonlight][crypto][gcm]") {
    const auto key = key16("EDF04A215C4FBEA20934120C8480D855");
    const auto payload = *c::hexDecode("060212000000000E05000000033400C00000059F0329");
    for (std::uint32_t seq : {0u, 1u, 2u, 6u, 42u, 255u}) {
        const auto sealed = c::sealControl(key, seq, payload.data(), payload.size());
        REQUIRE(sealed.has_value());
        const auto opened = c::openControl(key, sealed->data(), sealed->size());
        REQUIRE(opened.has_value());
        REQUIRE(*opened == payload);
    }
}

TEST_CASE("openControl rejects a tampered tag and a wrong key", "[moonlight][crypto][gcm]") {
    const auto key = key16("EDF04A215C4FBEA20934120C8480D855");
    const auto payload = *c::hexDecode("020302000000");
    auto sealed = *c::sealControl(key, 3, payload.data(), payload.size());

    // Flip a byte in the ciphertext region -> tag mismatch -> nullopt.
    auto tampered = sealed;
    tampered.back() ^= 0x01;
    REQUIRE_FALSE(c::openControl(key, tampered.data(), tampered.size()).has_value());

    // Flip a byte in the tag -> nullopt.
    auto tag = sealed;
    tag[8] ^= 0x80;
    REQUIRE_FALSE(c::openControl(key, tag.data(), tag.size()).has_value());

    // A different key does not open it.
    const auto other = key16("00000000000000000000000000000000");
    REQUIRE_FALSE(c::openControl(other, sealed.data(), sealed.size()).has_value());

    // A truncated packet is rejected, not read out of bounds.
    REQUIRE_FALSE(c::openControl(key, sealed.data(), 10).has_value());
}

TEST_CASE("ControlSealer matches sealControl byte for byte", "[moonlight][crypto][gcm]") {
    const auto key = key16("EDF04A215C4FBEA20934120C8480D855");
    c::ControlSealer sealer(key);
    REQUIRE(sealer.ok());
    const auto payload = *c::hexDecode("060212000000000E05000000033400C00000059F0329");
    // A reused context must produce identical output across an evolving seq,
    // including a re-used low IV byte (256 aliases 0 in the IV construction).
    for (std::uint32_t seq : {0u, 1u, 6u, 200u, 255u, 256u, 1000u}) {
        std::size_t n = 0;
        const std::uint8_t* pkt = sealer.seal(seq, payload.data(), payload.size(), &n);
        REQUIRE(pkt != nullptr);
        const auto expected = c::sealControl(key, seq, payload.data(), payload.size());
        REQUIRE(expected.has_value());
        REQUIRE(c::Bytes(pkt, pkt + n) == *expected);
        // And the receiver opens it.
        const auto opened = c::openControl(key, pkt, n);
        REQUIRE(opened.has_value());
        REQUIRE(*opened == payload);
    }
}

TEST_CASE("genAesKey is the first 16 bytes of SHA256(salt||pin)", "[moonlight][crypto]") {
    // Deterministic vector: SHA256("" salt + "1234") first 16 bytes.
    const std::vector<std::uint8_t> salt; // empty salt
    const auto key = c::genAesKey(salt.data(), salt.size(), "1234");
    // SHA256("1234") = 03ac6742...; first 16 bytes:
    const auto full = c::sha256(std::vector<std::uint8_t>{'1', '2', '3', '4'});
    for (std::size_t i = 0; i < 16; ++i) { REQUIRE(key[i] == full[i]); }
}

TEST_CASE("AES-128-ECB round-trips, no padding", "[moonlight][crypto][ecb]") {
    const auto key = key16("000102030405060708090A0B0C0D0E0F");
    const auto pt = *c::hexDecode("00112233445566778899AABBCCDDEEFF"); // one block
    const auto ct = c::aesEcbEncrypt(key, pt);
    REQUIRE(ct.has_value());
    REQUIRE(ct->size() == 16);
    const auto back = c::aesEcbDecrypt(key, *ct);
    REQUIRE(back.has_value());
    REQUIRE(*back == pt);

    // A non-block-multiple length is rejected.
    const auto odd = *c::hexDecode("00112233");
    REQUIRE_FALSE(c::aesEcbEncrypt(key, odd).has_value());
}

TEST_CASE("hexEncode/hexDecode round-trip and reject bad input", "[moonlight][crypto][hex]") {
    const std::vector<std::uint8_t> b = {0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0xFF};
    REQUIRE(c::hexEncode(b) == "00DEADBEEFFF");
    REQUIRE(*c::hexDecode("00deadBEEFff") == b);
    REQUIRE_FALSE(c::hexDecode("ABC").has_value()); // odd length
    REQUIRE_FALSE(c::hexDecode("AZ").has_value());  // non-hex
}
