// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <claim.h>

#include <addresstype.h>
#include <consensus/amount.h>
#include <consensus/params.h>
#include <hash.h>
#include <key_io.h>
#include <pubkey.h>
#include <util/strencodings.h>
#include <script/script.h>
#include <streams.h>
#include <uint256.h>

#include <sstream>

#include <cstring>
#include <functional>
#include <string>

// ── Canonical Claim Message Parser ───────────────────────────────────────────

std::optional<ParsedClaimMessage> ParseClaimMessageCanonical(const std::vector<unsigned char>& msgBytes)
{
    std::string msg(msgBytes.begin(), msgBytes.end());
    std::istringstream stream(msg);
    std::string line;
    ParsedClaimMessage parsed;

    // Line 1: "NEX CLAIM v1"
    if (!std::getline(stream, line)) return std::nullopt;
    if (line == "NEX CLAIM v1") {
        parsed.version = 1;
    } else {
        return std::nullopt;
    }

    // Line 2: "Bitcoin Snapshot Block: 940000"
    if (!std::getline(stream, line)) return std::nullopt;
    const std::string blockPrefix = "Bitcoin Snapshot Block: ";
    if (line.substr(0, blockPrefix.size()) != blockPrefix) return std::nullopt;
    try {
        parsed.snapshotBlock = std::stoi(line.substr(blockPrefix.size()));
    } catch (...) {
        return std::nullopt;
    }

    // Line 3: "Claim To: nex1z..."
    if (!std::getline(stream, line)) return std::nullopt;
    const std::string destPrefix = "Claim To: ";
    if (line.substr(0, destPrefix.size()) != destPrefix) return std::nullopt;
    parsed.claimTo = line.substr(destPrefix.size());
    if (parsed.claimTo.empty()) return std::nullopt;

    // Line 4: "Nonce: <hex>"
    if (!std::getline(stream, line)) return std::nullopt;
    const std::string noncePrefix = "Nonce: ";
    if (line.substr(0, noncePrefix.size()) != noncePrefix) return std::nullopt;
    parsed.nonce = line.substr(noncePrefix.size());
    if (parsed.nonce.size() != 64) return std::nullopt;
    for (char c : parsed.nonce) {
        if (!HexDigit(c) && c != '\0') return std::nullopt;
    }

    return parsed;
}


// ── Merkle proof helpers ─────────────────────────────────────────────────────

static uint256 SHA256d(const unsigned char* data, size_t len)
{
    uint256 result;
    CSHA256 sha;
    uint256 tmp;
    sha.Write(data, len).Finalize(tmp.data());
    CSHA256().Write(tmp.data(), 32).Finalize(result.data());
    return result;
}

static uint256 SHA256d(const uint256& a, const uint256& b)
{
    unsigned char combined[64];
    std::memcpy(combined, a.data(), 32);
    std::memcpy(combined + 32, b.data(), 32);
    return SHA256d(combined, 64);
}

/**
 * Reconstruct the leaf hash for a snapshot Merkle tree entry.
 * Matches the Python extract.py: SHA256d(scriptPubKey || uint64_le(balance_sat))
 */
static uint256 ClaimLeafHash(const std::vector<unsigned char>& btcScriptPubKey,
                             int64_t snapshotBalanceSat)
{
    std::vector<unsigned char> data;
    data.insert(data.end(), btcScriptPubKey.begin(), btcScriptPubKey.end());
    // Append balance as uint64 little-endian
    uint64_t bal = static_cast<uint64_t>(snapshotBalanceSat);
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<unsigned char>(bal & 0xFF));
        bal >>= 8;
    }
    return SHA256d(data.data(), data.size());
}


// ── Parse claim data from transaction ────────────────────────────────────────

bool ParseClaimData(const CTransaction& tx, ClaimData& claim)
{
    if (!IsClaimTx(tx)) return false;

    // The claim data is serialized in the scriptSig of the first (and only) input.
    const CScript& scriptSig = tx.vin[0].scriptSig;
    if (scriptSig.empty()) return false;

    try {
        DataStream ss(scriptSig);
        ss >> claim;
    } catch (const std::exception&) {
        return false;
    }

    // Basic structural validation
    if (claim.btcScriptPubKey.empty()) return false;
    if (claim.btcSignature.empty()) return false;
    if (claim.claimMessage.empty()) return false;
    if (claim.snapshotBalanceSat < 100'000'000) return false;  // must be >= 1 BTC

    return true;
}


// ── Merkle proof verification ────────────────────────────────────────────────

bool VerifyClaimMerkleProof(const std::vector<unsigned char>& btcScriptPubKey,
                            int64_t snapshotBalanceSat,
                            const std::vector<uint256>& proof,
                            const uint256& merkleRoot)
{
    if (merkleRoot.IsNull()) return false;

    // Reconstruct the leaf hash
    uint256 current = ClaimLeafHash(btcScriptPubKey, snapshotBalanceSat);

    // Walk the proof from leaf to root.
    // Each proof element is a sibling hash. The position bit (left/right)
    // is encoded in the lowest bit of the current hash at each level:
    //   if current < sibling: current is left child
    //   else: current is right child
    for (const uint256& sibling : proof) {
        if (current < sibling) {
            current = SHA256d(current, sibling);
        } else {
            current = SHA256d(sibling, current);
        }
    }

    return current == merkleRoot;
}


// ── BTC signature verification (secp256k1) ──────────────────────────────────

bool VerifyClaimBTCSignature(const std::vector<unsigned char>& btcScriptPubKey,
                             const std::vector<unsigned char>& btcSignature,
                             const std::vector<unsigned char>& claimMessage)
{
    // The claim signature is a Bitcoin message signature (compact, 65 bytes).
    // We need to recover the public key from the signature and verify it
    // matches the btcScriptPubKey.

    if (btcSignature.size() != 65) return false;
    if (claimMessage.empty()) return false;

    // Compute Bitcoin message hash: SHA256d(prefix + varint(len) + msg)
    // The prefix is: byte 0x18 (length 24) followed by "Bitcoin Signed Message:\n"
    static const std::string BTC_MSG_MAGIC = "\x18" "Bitcoin Signed Message:\n";

    HashWriter hasher{};
    hasher.write(MakeByteSpan(std::span<const char>{BTC_MSG_MAGIC.data(), BTC_MSG_MAGIC.size()}));
    // Write message length as varint then message
    if (claimMessage.size() < 253) {
        uint8_t sz = static_cast<uint8_t>(claimMessage.size());
        hasher.write(MakeByteSpan(std::span<const uint8_t>{&sz, 1}));
    } else {
        uint8_t marker = 0xFD;
        hasher.write(MakeByteSpan(std::span<const uint8_t>{&marker, 1}));
        uint16_t sz = static_cast<uint16_t>(claimMessage.size());
        hasher.write(MakeByteSpan(std::span<const uint16_t>{&sz, 1}));
    }
    hasher.write(MakeByteSpan(claimMessage));
    uint256 msgHash = hasher.GetHash();

    // Recover the public key from the compact signature
    CPubKey recoveredPubKey;
    if (!recoveredPubKey.RecoverCompact(msgHash, btcSignature)) {
        return false;
    }

    // Now verify that the recovered public key corresponds to the btcScriptPubKey.
    // Supported BTC script types:
    //
    // P2PKH: OP_DUP OP_HASH160 <20-byte-hash> OP_EQUALVERIFY OP_CHECKSIG
    //   → hash160(recoveredPubKey) must match the 20-byte hash
    //
    // P2WPKH: OP_0 <20-byte-hash>
    //   → hash160(recoveredPubKey) must match the 20-byte hash
    //
    // P2PK: <33-or-65-byte-pubkey> OP_CHECKSIG
    //   → recoveredPubKey must match directly

    const size_t spkSize = btcScriptPubKey.size();
    const unsigned char* spk = btcScriptPubKey.data();

    // P2PKH (25 bytes: 76 a9 14 <20> 88 ac)
    if (spkSize == 25 && spk[0] == 0x76 && spk[1] == 0xa9 && spk[2] == 0x14
        && spk[23] == 0x88 && spk[24] == 0xac)
    {
        CKeyID keyID = recoveredPubKey.GetID();
        return std::memcmp(keyID.data(), spk + 3, 20) == 0;
    }

    // P2WPKH (22 bytes: 00 14 <20>)
    if (spkSize == 22 && spk[0] == 0x00 && spk[1] == 0x14)
    {
        CKeyID keyID = recoveredPubKey.GetID();
        return std::memcmp(keyID.data(), spk + 2, 20) == 0;
    }

    // P2PK compressed (35 bytes: 21 <33-byte-pubkey> ac)
    if (spkSize == 35 && spk[0] == 0x21 && spk[34] == 0xac)
    {
        return recoveredPubKey.IsCompressed() &&
               std::memcmp(recoveredPubKey.data(), spk + 1, 33) == 0;
    }

    // Unsupported script type
    return false;
}


// ── Full claim transaction validation ────────────────────────────────────────

std::string ValidateClaimTx(const CTransaction& tx,
                            int64_t blockTime,
                            int64_t totalClaimed,
                            const uint256& merkleRoot,
                            const std::function<bool(const std::vector<unsigned char>&)>& isAlreadyClaimed)
{
    // 1. Structural check
    if (!IsClaimTx(tx)) {
        return "claim-not-valid-structure";
    }

    // 2. Output must be exactly 1 NEX
    if (tx.vout[0].nValue != CLAIM_AMOUNT) {
        return "claim-wrong-amount";
    }

    // 3. Parse claim data
    ClaimData claim;
    if (!ParseClaimData(tx, claim)) {
        return "claim-data-parse-failed";
    }

    // 4. Check deadline
    if (blockTime >= Consensus::CLAIM_DEADLINE_TIMESTAMP) {
        return "claim-expired";
    }

    // 5. Check global claim cap
    if (totalClaimed + CLAIM_AMOUNT > Consensus::CLAIM_POOL_CAP_SAT) {
        return "claim-pool-exhausted";
    }

    // 6. Check not already claimed
    if (isAlreadyClaimed(claim.btcScriptPubKey)) {
        return "claim-already-claimed";
    }

    // 7. Verify Merkle proof (snapshot inclusion)
    if (!VerifyClaimMerkleProof(claim.btcScriptPubKey, claim.snapshotBalanceSat,
                                claim.merkleProof, merkleRoot)) {
        return "claim-merkle-proof-invalid";
    }

    // 8. Verify BTC ownership signature (secp256k1)
    if (!VerifyClaimBTCSignature(claim.btcScriptPubKey, claim.btcSignature,
                                 claim.claimMessage)) {
        return "claim-btc-signature-invalid";
    }

    // 9. Parse claim message canonically and bind destination to actual output.
    {
        auto parsed = ParseClaimMessageCanonical(claim.claimMessage);
        if (!parsed) {
            return "claim-message-parse-failed";
        }

        // Enforce exact field values
        if (parsed->version != 1) {
            return "claim-message-wrong-version";
        }
        if (parsed->snapshotBlock != 940000) {
            return "claim-message-wrong-snapshot";
        }

        // Decode the signed destination address to a scriptPubKey
        std::string decodeErr;
        CTxDestination signedDest = DecodeDestination(parsed->claimTo, decodeErr);
        if (!IsValidDestination(signedDest)) {
            return "claim-message-destination-invalid";
        }

        CScript expectedScript = GetScriptForDestination(signedDest);

        // CRITICAL: signed destination must match the actual tx output script EXACTLY
        if (expectedScript != tx.vout[0].scriptPubKey) {
            return "claim-destination-mismatch";
        }

        // Destination must be a PQ witness v2 address
        if (tx.vout[0].scriptPubKey.size() != 34 ||
            tx.vout[0].scriptPubKey[0] != 0x52 ||
            tx.vout[0].scriptPubKey[1] != 0x20) {
            return "claim-destination-not-pq";
        }
    }

    // All checks passed
    return "";
}
