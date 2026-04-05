// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NEX_CLAIMDB_H
#define NEX_CLAIMDB_H

#include <claim.h>
#include <consensus/amount.h>
#include <dbwrapper.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * NEX Claim Index — LevelDB-backed database tracking Bitcoin snapshot claims.
 *
 * Stored in <datadir>/claimindex/ alongside the chainstate.
 *
 * Keys:   DB_CLAIM_ENTRY ('X') + SHA256(btcScriptPubKey)
 * Values: ClaimRecord (block height + claim txid)
 *
 * Also tracks:
 *   DB_CLAIM_TOTAL ('T') → total NEX claimed so far (int64_t)
 *   DB_CLAIM_COUNT ('N') → number of successful claims (int64_t)
 */

/** Claim record stored in LevelDB for each claimed BTC scriptPubKey. */
struct ClaimIndexEntry {
    int32_t claimHeight;     //!< Block height where the claim was confirmed
    uint256 claimTxid;       //!< Transaction ID of the claim

    SERIALIZE_METHODS(ClaimIndexEntry, obj)
    {
        READWRITE(obj.claimHeight, obj.claimTxid);
    }
};

class CClaimIndex
{
private:
    std::unique_ptr<CDBWrapper> m_db;

    static constexpr uint8_t DB_CLAIM_ENTRY{'X'};
    static constexpr uint8_t DB_CLAIM_TOTAL{'T'};
    static constexpr uint8_t DB_CLAIM_COUNT{'N'};

    //! Key for a claim entry: hash of the BTC scriptPubKey
    uint256 MakeKey(const std::vector<unsigned char>& btcScriptPubKey) const;

public:
    explicit CClaimIndex(const fs::path& path, size_t cache_size = 1 << 20 /* 1 MB */);

    //! Check if a BTC scriptPubKey has already been claimed.
    bool IsClaimed(const std::vector<unsigned char>& btcScriptPubKey) const;

    //! Get the claim record for a BTC scriptPubKey (if claimed).
    bool GetClaim(const std::vector<unsigned char>& btcScriptPubKey, ClaimIndexEntry& entry) const;

    //! Record a successful claim. Called from ConnectBlock.
    void WriteClaim(const std::vector<unsigned char>& btcScriptPubKey,
                    int32_t height, const uint256& txid);

    //! Remove a claim record. Called from DisconnectBlock (reorg).
    void EraseClaim(const std::vector<unsigned char>& btcScriptPubKey);

    //! Get total NEX claimed so far.
    CAmount GetTotalClaimed() const;

    //! Get number of successful claims.
    int64_t GetClaimCount() const;

    //! Update the running totals. Called after WriteClaim/EraseClaim.
    void WriteTotals(CAmount totalClaimed, int64_t claimCount);

    //! Check if the index needs rebuilding (empty DB with non-genesis chain tip).
    bool NeedsRebuild() const;

    //! Stronger check: also detects best-block mismatch with active chain tip,
    //! negative totals, and count/total inconsistency.
    bool NeedsRebuild(const uint256& activeTipHash) const;

    //! Clear ALL entries from the index. Used before rebuild.
    void Clear();

    //! Mark rebuild complete by storing the chain tip hash.
    void SetBestBlock(const uint256& blockHash);

    //! Get the block hash that the index was last synced to.
    uint256 GetBestBlock() const;
};

#endif // NEX_CLAIMDB_H
