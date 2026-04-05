// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NEX_PQHD_H
#define NEX_PQHD_H

#include <pqkey.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/**
 * Post-Quantum HD Key Derivation for NEX.
 *
 * BIP-39 24-word mnemonic → master seed → PQ keypairs
 *
 * Derivation path:
 *   1. BIP-39 mnemonic → 64-byte master seed (PBKDF2-HMAC-SHA512)
 *   2. Master seed → SHAKE-256(seed || "NEX-PQ-MASTER") → 32-byte PQ master secret
 *   3. PQ master secret + index → SHAKE-256(master || uint32_le(index) || "NEX-PQ-CHILD")
 *      → 32-byte child seed → ML-DSA-65 keypair
 *
 * This is NOT BIP-32 (ECDSA-specific). ML-DSA-65 uses different math.
 * The mnemonic and PBKDF2 step is standard BIP-39; only the key derivation differs.
 */

/** Domain separator for PQ master key derivation. */
static const std::string PQ_MASTER_DOMAIN = "NEX-PQ-MASTER";

/** Domain separator for PQ child key derivation. */
static const std::string PQ_CHILD_DOMAIN = "NEX-PQ-CHILD";

/**
 * Derive a PQ master secret from a BIP-39 master seed.
 *
 * @param masterSeed   The 64-byte BIP-39 master seed (from PBKDF2)
 * @param masterSecret Output: 32-byte PQ master secret
 */
void DerivePQMasterSecret(std::span<const unsigned char> masterSeed,
                          std::array<unsigned char, 32>& masterSecret);

/**
 * Derive an ML-DSA-65 keypair at a given index from a PQ master secret.
 *
 * @param masterSecret  The 32-byte PQ master secret
 * @param index         Child key index (0, 1, 2, ...)
 * @param key           Output: the derived ML-DSA-65 secret key
 * @param pubkey        Output: the corresponding public key
 * @return true on success
 */
bool DerivePQChildKey(const std::array<unsigned char, 32>& masterSecret,
                      uint32_t index,
                      CPQKey& key,
                      CPQPubKey& pubkey);

/**
 * Convenience: derive a PQ keypair directly from a BIP-39 master seed + index.
 *
 * @param masterSeed  The 64-byte BIP-39 master seed
 * @param index       Child key index
 * @param key         Output: ML-DSA-65 secret key
 * @param pubkey      Output: ML-DSA-65 public key
 * @return true on success
 */
bool DerivePQKeyFromSeed(std::span<const unsigned char> masterSeed,
                         uint32_t index,
                         CPQKey& key,
                         CPQPubKey& pubkey);

#endif // NEX_PQHD_H
