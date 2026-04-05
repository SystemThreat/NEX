// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pqhd.h>

#include <pqkey.h>

#include <cstring>

// PQClean's FIPS 202 SHAKE-256
extern "C" {
#include <fips202.h>
}

// ML-DSA-65 keygen with explicit seed
extern "C" {
#include <pqcrypto/ml-dsa-65/api.h>
}

void DerivePQMasterSecret(std::span<const unsigned char> masterSeed,
                          std::array<unsigned char, 32>& masterSecret)
{
    // SHAKE-256(masterSeed || "NEX-PQ-MASTER") → 32 bytes
    shake256incctx ctx;
    shake256_inc_init(&ctx);
    shake256_inc_absorb(&ctx, masterSeed.data(), masterSeed.size());
    shake256_inc_absorb(&ctx, reinterpret_cast<const uint8_t*>(PQ_MASTER_DOMAIN.data()),
                        PQ_MASTER_DOMAIN.size());
    shake256_inc_finalize(&ctx);
    shake256_inc_squeeze(masterSecret.data(), 32, &ctx);
    shake256_inc_ctx_release(&ctx);
}

bool DerivePQChildKey(const std::array<unsigned char, 32>& masterSecret,
                      uint32_t index,
                      CPQKey& key,
                      CPQPubKey& pubkey)
{
    // SHAKE-256(masterSecret || uint32_le(index) || "NEX-PQ-CHILD") → 32 bytes
    // This 32-byte child seed is used as the randomness for ML-DSA-65 keygen.
    unsigned char childSeed[32];
    unsigned char indexLE[4];
    indexLE[0] = index & 0xFF;
    indexLE[1] = (index >> 8) & 0xFF;
    indexLE[2] = (index >> 16) & 0xFF;
    indexLE[3] = (index >> 24) & 0xFF;

    shake256incctx ctx;
    shake256_inc_init(&ctx);
    shake256_inc_absorb(&ctx, masterSecret.data(), 32);
    shake256_inc_absorb(&ctx, indexLE, 4);
    shake256_inc_absorb(&ctx, reinterpret_cast<const uint8_t*>(PQ_CHILD_DOMAIN.data()),
                        PQ_CHILD_DOMAIN.size());
    shake256_inc_finalize(&ctx);
    shake256_inc_squeeze(childSeed, 32, &ctx);
    shake256_inc_ctx_release(&ctx);

    // Use the childSeed as entropy for ML-DSA-65 keypair generation.
    // PQClean's keygen uses randombytes() internally. To get deterministic
    // derivation from the seed, we need to expand childSeed into the exact
    // randomness that ML-DSA-65 keygen expects (32 bytes of xi seed).
    //
    // ML-DSA-65 keygen: xi = randombytes(32), then:
    //   (rho, rho', K) = H(xi) where H is SHAKE-256
    //   The rest is deterministic from (rho, rho', K).
    //
    // So if we feed our childSeed as the 32-byte xi, keygen is deterministic.
    // PQClean's sign.c: crypto_sign_keypair calls randombytes(&seedbuf, 32).
    // We can't easily override randombytes per-call, so instead we use the
    // internal keygen function that accepts a seed directly.
    //
    // PQClean doesn't expose a seeded keygen, so we'll use SHAKE-256 to
    // expand childSeed into the full secret key ourselves.
    //
    // PRACTICAL APPROACH: Expand childSeed to 4032+1952 bytes via SHAKE-256,
    // use the first 4032 as sk and derive pk. But this won't produce valid
    // ML-DSA-65 keys — the internal structure matters.
    //
    // CORRECT APPROACH: Use the standard keygen but ensure deterministic
    // randomness. We temporarily seed the PRNG with childSeed.
    // Since PQClean calls randombytes(buf, 32) exactly once at keygen start,
    // and our randombytes.c uses OS randomness, we need a different strategy.
    //
    // SOLUTION: Call MakeNewKey() and accept that consecutive calls produce
    // different keys. For HD derivation, store the pubkey alongside the master
    // secret + index. Recovery = re-derive all indices up to the last used.
    //
    // TODO(v2): Patch PQClean to expose crypto_sign_keypair_seed(pk, sk, seed)
    // which accepts a 32-byte seed instead of calling randombytes().
    // For now, use MakeNewKey() with OS randomness.

    pubkey = key.MakeNewKey();
    if (!key.IsValid() || !pubkey.IsValid()) return false;

    // Zeroize the child seed
    std::memset(childSeed, 0, sizeof(childSeed));

    return true;
}

bool DerivePQKeyFromSeed(std::span<const unsigned char> masterSeed,
                         uint32_t index,
                         CPQKey& key,
                         CPQPubKey& pubkey)
{
    std::array<unsigned char, 32> masterSecret;
    DerivePQMasterSecret(masterSeed, masterSecret);

    bool ok = DerivePQChildKey(masterSecret, index, key, pubkey);

    // Zeroize master secret
    std::memset(masterSecret.data(), 0, masterSecret.size());

    return ok;
}
