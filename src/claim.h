// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NEX_CLAIM_H
#define NEX_CLAIM_H

#include <consensus/amount.h>
#include <consensus/params.h>
#include <crypto/sha256.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * NEX Claim System — Bitcoin snapshot-linked claim mechanism.
 *
 * Allows holders of >= 1 BTC at Bitcoin block 940,000 to claim exactly
 * 1 NEX by proving control of the corresponding Bitcoin private key.
 *
 * Claim transactions are identified by nVersion == TX_CLAIM_VERSION (3).
 * They have a special structure:
 *   - 1 input with null outpoint (like coinbase), scriptSig carries claim data
 *   - 1 output: exactly 1.00000000 NEX to a PQ destination address
 *
 * Consensus validation:
 *   1. Verify btc_scriptPubKey is in the snapshot Merkle tree
 *   2. Verify BTC signature proves control of that key
 *   3. Verify not already claimed (claim index)
 *   4. Verify block time < claim deadline (2028-09-12 UTC)
 *   5. Verify total claims + 1 <= 1,000,000 NEX cap
 *   6. Output is exactly 1.00000000 NEX
 */

/** Transaction version that identifies a claim transaction. */
static constexpr uint32_t TX_CLAIM_VERSION = 3;

/** Exact amount minted per claim: 5.00000000 NEX in satoshis.
 *  Ratio: NEX max supply (100M) / BTC max supply (21M) ≈ 5x */
static constexpr CAmount CLAIM_AMOUNT = 5 * COIN;

/** Maximum number of successful claims. */
static constexpr int64_t MAX_CLAIMS = 1'000'000;

/**
 * Parsed claim data extracted from a claim transaction's scriptSig.
 */
struct ClaimData {
    /** The Bitcoin scriptPubKey that was eligible at snapshot block 940,000. */
    std::vector<unsigned char> btcScriptPubKey;

    /** The BTC signature proving ownership of the eligible key.
     *  This is a Bitcoin message signature (compact 65-byte format). */
    std::vector<unsigned char> btcSignature;

    /** The claim message that was signed (binds chain, destination, snapshot block). */
    std::vector<unsigned char> claimMessage;

    /** Merkle proof: sibling hashes from leaf to root, proving inclusion
     *  of btcScriptPubKey in the snapshot Merkle tree. */
    std::vector<uint256> merkleProof;

    /** The aggregated BTC balance of this address at the snapshot (in satoshis).
     *  Used to reconstruct the leaf hash for Merkle verification. */
    int64_t snapshotBalanceSat{0};

    SERIALIZE_METHODS(ClaimData, obj)
    {
        READWRITE(obj.btcScriptPubKey, obj.btcSignature, obj.claimMessage,
                  obj.merkleProof, obj.snapshotBalanceSat);
    }
};

/** The claim marker outpoint: txid = SHA256d("NEX_CLAIM"), vout = 0xCLA1.
 *  This is NOT null, so IsCoinBase() returns false for claim transactions.
 *  It's a recognizable sentinel that cannot collide with real UTXOs. */
static const uint256 CLAIM_MARKER_TXID = [] {
    // Deterministic: SHA256d("NEX_CLAIM_MARKER_V1")
    // Precomputed to avoid runtime init
    uint256 h;
    const char* tag = "NEX_CLAIM_MARKER_V1";
    CSHA256().Write((const unsigned char*)tag, 19).Finalize(h.data());
    CSHA256().Write(h.data(), 32).Finalize(h.data());
    return h;
}();
static constexpr uint32_t CLAIM_MARKER_VOUT = 0x434C4131; // "CLA1" in ASCII

/**
 * Check if a transaction is a claim transaction (structural check only).
 * Claim txs have version=3, one input with the claim marker outpoint (NOT null),
 * and exactly one output.
 */
inline bool IsClaimTx(const CTransaction& tx)
{
    return tx.version == TX_CLAIM_VERSION
        && tx.vin.size() == 1
        && tx.vin[0].prevout.hash.ToUint256() == CLAIM_MARKER_TXID
        && tx.vin[0].prevout.n == CLAIM_MARKER_VOUT
        && tx.vout.size() == 1;
}

/**
 * Canonical parsed claim message — structured fields extracted from the
 * signed text. Used for consensus validation of claim destination binding.
 */
struct ParsedClaimMessage {
    int version{0};                //!< Must be 1
    int snapshotBlock{0};          //!< Must be 940000
    std::string claimTo;           //!< NEX destination address (nex1z...)
    std::string nonce;             //!< Deterministic nonce (64-char hex)
};

/**
 * Parse a claim message string into structured fields.
 * Format:
 *   NEX CLAIM v1
 *   Bitcoin Snapshot Block: 940000
 *   Claim To: nex1z...
 *   Nonce: <64-char hex>
 *
 * Returns nullopt if any field is missing or malformed.
 */
std::optional<ParsedClaimMessage> ParseClaimMessageCanonical(const std::vector<unsigned char>& msgBytes);

/**
 * Parse claim data from a claim transaction's scriptSig.
 * Returns false if the data is malformed.
 */
bool ParseClaimData(const CTransaction& tx, ClaimData& claim);

/**
 * Verify a Merkle proof against the hardcoded claim Merkle root.
 *
 * @param btcScriptPubKey   The BTC scriptPubKey being claimed
 * @param snapshotBalanceSat The balance at the snapshot (for leaf hash reconstruction)
 * @param proof             The sibling hashes from leaf to root
 * @param merkleRoot        The expected Merkle root (from chainparams)
 * @return true if the proof is valid
 */
bool VerifyClaimMerkleProof(const std::vector<unsigned char>& btcScriptPubKey,
                            int64_t snapshotBalanceSat,
                            const std::vector<uint256>& proof,
                            const uint256& merkleRoot);

/**
 * Verify the BTC ownership signature in a claim.
 * Uses secp256k1 (Bitcoin's signature scheme) to verify that the claimant
 * controls the private key for the eligible BTC address.
 *
 * @param btcScriptPubKey  The BTC scriptPubKey
 * @param btcSignature     The compact Bitcoin message signature (65 bytes)
 * @param claimMessage     The signed message
 * @return true if signature is valid for the scriptPubKey
 */
bool VerifyClaimBTCSignature(const std::vector<unsigned char>& btcScriptPubKey,
                             const std::vector<unsigned char>& btcSignature,
                             const std::vector<unsigned char>& claimMessage);

/**
 * Full consensus validation of a claim transaction.
 * Called from ConnectBlock() for every claim tx in a block.
 *
 * @param tx            The claim transaction
 * @param blockTime     The block's timestamp (for deadline check)
 * @param totalClaimed  Total NEX already claimed (for cap check)
 * @param merkleRoot    The snapshot Merkle root from chainparams
 * @param isAlreadyClaimed  Callback: returns true if btcScriptPubKey is already claimed
 * @return empty string on success, error description on failure
 */
std::string ValidateClaimTx(const CTransaction& tx,
                            int64_t blockTime,
                            int64_t totalClaimed,
                            const uint256& merkleRoot,
                            const std::function<bool(const std::vector<unsigned char>&)>& isAlreadyClaimed);

#endif // NEX_CLAIM_H
