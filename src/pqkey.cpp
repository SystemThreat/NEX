// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pqkey.h>

#include <hash.h>
#include <random.h>

#include <cstring>

// PQClean ML-DSA-65 C API
extern "C" {
#include <pqcrypto/ml-dsa-65/api.h>
}

// Verify our constants match PQClean's
static_assert(PQ_PUBKEY_SIZE == PQCLEAN_MLDSA65_CLEAN_CRYPTO_PUBLICKEYBYTES,
              "PQ_PUBKEY_SIZE mismatch with PQClean");
static_assert(PQ_SECRETKEY_SIZE == PQCLEAN_MLDSA65_CLEAN_CRYPTO_SECRETKEYBYTES,
              "PQ_SECRETKEY_SIZE mismatch with PQClean");
static_assert(PQ_SIGNATURE_SIZE == PQCLEAN_MLDSA65_CLEAN_CRYPTO_BYTES,
              "PQ_SIGNATURE_SIZE mismatch with PQClean");


// ── CPQPubKey ────────────────────────────────────────────────────────────────

bool CPQPubKey::Verify(const uint256& hash, const std::vector<unsigned char>& vchSig) const
{
    if (!m_valid) return false;
    if (vchSig.empty() || vchSig.size() > PQ_SIGNATURE_SIZE) return false;

    // ML-DSA-65 verifies messages, not hashes. We pass the 32-byte hash as the message.
    return PQCLEAN_MLDSA65_CLEAN_crypto_sign_verify(
        vchSig.data(), vchSig.size(),
        hash.data(), hash.size(),
        m_data.data()
    ) == 0;
}

bool CPQPubKey::VerifyMessage(std::span<const unsigned char> msg,
                              std::span<const unsigned char> sig) const
{
    if (!m_valid) return false;
    if (sig.empty() || sig.size() > PQ_SIGNATURE_SIZE) return false;

    return PQCLEAN_MLDSA65_CLEAN_crypto_sign_verify(
        sig.data(), sig.size(),
        msg.data(), msg.size(),
        m_data.data()
    ) == 0;
}


// ── CPQKey ───────────────────────────────────────────────────────────────────

CPQPubKey CPQKey::MakeNewKey()
{
    MakeKeyData();

    std::array<unsigned char, PQ_PUBKEY_SIZE> pk;
    int ret = PQCLEAN_MLDSA65_CLEAN_crypto_sign_keypair(pk.data(), m_keydata->data());
    if (ret != 0) {
        ClearKeyData();
        return CPQPubKey();
    }

    return CPQPubKey(pk.data(), pk.data() + pk.size());
}

CPQPubKey CPQKey::GetPubKey() const
{
    if (!IsValid()) return CPQPubKey();

    // In ML-DSA-65, the public key is embedded in the secret key.
    // The secret key format (from PQClean/Dilithium) is:
    //   rho (32) || K (32) || tr (64) || s1 (packed) || s2 (packed) || t0 (packed)
    // And the public key is:
    //   rho (32) || t1 (packed)
    //
    // We can't trivially extract the pubkey from the secret key without
    // reimplementing the packing logic. Instead, we store/regenerate it.
    //
    // For now: re-derive by signing a test message and... no.
    // Better approach: the secret key bytes 0..31 contain rho, which is also
    // the first 32 bytes of the public key. But the rest differs.
    //
    // The cleanest solution: generate the keypair at creation time and cache
    // the public key alongside the secret key. Since CPQKey::MakeNewKey()
    // already returns the public key, callers should store it.
    //
    // For the case where we loaded a secret key from disk without the pubkey,
    // we need PQClean to provide a sk_to_pk function. PQClean doesn't expose
    // this directly, so we'll add a minimal extraction.
    //
    // WORKAROUND: Sign a known message with the secret key, then verify
    // against candidate public keys... No, that's circular.
    //
    // ACTUAL FIX: ML-DSA secret keys DO contain all info needed to reconstruct
    // the public key. In Dilithium, sk = (rho, K, tr, s1, s2, t0) and
    // pk = (rho, t1). We need to compute t1 = NTT^-1(A*NTT(s1) + NTT(s2))
    // rounded. This requires calling into the PQClean internals.
    //
    // For v1: We require that the public key always be stored alongside the
    // secret key. GetPubKey() on a loaded-from-bytes key returns invalid.
    // Callers must use the keypair generation path.

    // TODO(phase0): Add sk_to_pk extraction using PQClean internals
    return CPQPubKey();
}

bool CPQKey::Sign(const uint256& hash, std::vector<unsigned char>& vchSig) const
{
    if (!IsValid()) return false;

    vchSig.resize(PQ_SIGNATURE_SIZE);
    size_t siglen = 0;

    // ML-DSA-65 signs messages directly. We pass the 32-byte hash as the message.
    int ret = PQCLEAN_MLDSA65_CLEAN_crypto_sign_signature(
        vchSig.data(), &siglen,
        hash.data(), hash.size(),
        m_keydata->data()
    );

    if (ret != 0) {
        vchSig.clear();
        return false;
    }

    vchSig.resize(siglen);
    return true;
}

bool CPQKey::SignMessage(std::span<const unsigned char> msg,
                         std::vector<unsigned char>& vchSig) const
{
    if (!IsValid()) return false;

    vchSig.resize(PQ_SIGNATURE_SIZE);
    size_t siglen = 0;

    int ret = PQCLEAN_MLDSA65_CLEAN_crypto_sign_signature(
        vchSig.data(), &siglen,
        msg.data(), msg.size(),
        m_keydata->data()
    );

    if (ret != 0) {
        vchSig.clear();
        return false;
    }

    vchSig.resize(siglen);
    return true;
}

bool CPQKey::VerifyPubKey(const CPQPubKey& pubkey) const
{
    if (!IsValid() || !pubkey.IsValid()) return false;

    // Sign a test message with the secret key, verify with the public key.
    static const unsigned char test_msg[] = "NEX PQ key verification test";
    std::vector<unsigned char> sig;
    sig.resize(PQ_SIGNATURE_SIZE);
    size_t siglen = 0;

    int ret = PQCLEAN_MLDSA65_CLEAN_crypto_sign_signature(
        sig.data(), &siglen,
        test_msg, sizeof(test_msg),
        m_keydata->data()
    );
    if (ret != 0) return false;

    return PQCLEAN_MLDSA65_CLEAN_crypto_sign_verify(
        sig.data(), siglen,
        test_msg, sizeof(test_msg),
        pubkey.data()
    ) == 0;
}


// ── Free functions ───────────────────────────────────────────────────────────

CPQKey GenerateRandomPQKey() noexcept
{
    CPQKey key;
    key.MakeNewKey();
    return key;
}
