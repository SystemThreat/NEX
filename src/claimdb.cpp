// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <claimdb.h>

#include <crypto/sha256.h>
#include <dbwrapper.h>
#include <logging.h>
#include <uint256.h>

#include <cassert>

uint256 CClaimIndex::MakeKey(const std::vector<unsigned char>& btcScriptPubKey) const
{
    uint256 hash;
    CSHA256().Write(btcScriptPubKey.data(), btcScriptPubKey.size()).Finalize(hash.data());
    return hash;
}

CClaimIndex::CClaimIndex(const fs::path& path, size_t cache_size)
{
    DBParams params{
        .path = path,
        .cache_bytes = cache_size,
        .memory_only = false,
    };
    m_db = std::make_unique<CDBWrapper>(params);
}

bool CClaimIndex::IsClaimed(const std::vector<unsigned char>& btcScriptPubKey) const
{
    uint256 key = MakeKey(btcScriptPubKey);
    return m_db->Exists(std::make_pair(DB_CLAIM_ENTRY, key));
}

bool CClaimIndex::GetClaim(const std::vector<unsigned char>& btcScriptPubKey,
                           ClaimIndexEntry& entry) const
{
    uint256 key = MakeKey(btcScriptPubKey);
    return m_db->Read(std::make_pair(DB_CLAIM_ENTRY, key), entry);
}

void CClaimIndex::WriteClaim(const std::vector<unsigned char>& btcScriptPubKey,
                             int32_t height, const uint256& txid)
{
    uint256 key = MakeKey(btcScriptPubKey);
    ClaimIndexEntry entry{height, txid};

    CDBBatch batch(*m_db);
    batch.Write(std::make_pair(DB_CLAIM_ENTRY, key), entry);

    CAmount totalClaimed = GetTotalClaimed() + CLAIM_AMOUNT;
    int64_t claimCount = GetClaimCount() + 1;
    batch.Write(DB_CLAIM_TOTAL, totalClaimed);
    batch.Write(DB_CLAIM_COUNT, claimCount);

    m_db->WriteBatch(batch);
    LogInfo("ClaimIndex: recorded claim at height %d, total=%lld sat, count=%lld\n",
            height, (long long)totalClaimed, (long long)claimCount);
}

void CClaimIndex::EraseClaim(const std::vector<unsigned char>& btcScriptPubKey)
{
    uint256 key = MakeKey(btcScriptPubKey);

    CDBBatch batch(*m_db);
    batch.Erase(std::make_pair(DB_CLAIM_ENTRY, key));

    CAmount totalClaimed = GetTotalClaimed() - CLAIM_AMOUNT;
    int64_t claimCount = GetClaimCount() - 1;
    if (totalClaimed < 0) totalClaimed = 0;
    if (claimCount < 0) claimCount = 0;
    batch.Write(DB_CLAIM_TOTAL, totalClaimed);
    batch.Write(DB_CLAIM_COUNT, claimCount);

    m_db->WriteBatch(batch);
    LogInfo("ClaimIndex: erased claim (reorg), total=%lld sat, count=%lld\n",
            (long long)totalClaimed, (long long)claimCount);
}

CAmount CClaimIndex::GetTotalClaimed() const
{
    CAmount total = 0;
    m_db->Read(DB_CLAIM_TOTAL, total);
    return total;
}

int64_t CClaimIndex::GetClaimCount() const
{
    int64_t count = 0;
    m_db->Read(DB_CLAIM_COUNT, count);
    return count;
}

void CClaimIndex::WriteTotals(CAmount totalClaimed, int64_t claimCount)
{
    CDBBatch batch(*m_db);
    batch.Write(DB_CLAIM_TOTAL, totalClaimed);
    batch.Write(DB_CLAIM_COUNT, claimCount);
    m_db->WriteBatch(batch);
}

static constexpr uint8_t DB_CLAIM_BEST_BLOCK{'B'};

bool CClaimIndex::NeedsRebuild() const
{
    // Missing or null best-block marker → definitely needs rebuild
    uint256 bestBlock;
    if (!m_db->Read(DB_CLAIM_BEST_BLOCK, bestBlock)) {
        return true;
    }
    return bestBlock.IsNull();
}

bool CClaimIndex::NeedsRebuild(const uint256& activeTipHash) const
{
    // Case 1: No best-block marker
    uint256 bestBlock;
    if (!m_db->Read(DB_CLAIM_BEST_BLOCK, bestBlock) || bestBlock.IsNull()) {
        return true;
    }
    // Case 2: Best-block doesn't match active chain tip
    // This catches: corrupted DB, partial writes, stale state after unclean shutdown
    if (bestBlock != activeTipHash) {
        return true;
    }
    // Case 3: Totals must be non-negative and internally consistent.
    // Each claim mints exactly CLAIM_AMOUNT, so total must equal count * CLAIM_AMOUNT.
    // This catches: count==0 but total>0, partial writes, corruption.
    CAmount total = GetTotalClaimed();
    int64_t count = GetClaimCount();
    if (total < 0 || count < 0) return true;
    if (total != count * CLAIM_AMOUNT) return true;
    return false;
}

void CClaimIndex::Clear()
{
    // Delete all entries by creating a fresh DB
    // (CDBWrapper doesn't support iteration-delete easily)
    // Reset totals to zero
    CDBBatch batch(*m_db);
    batch.Write(DB_CLAIM_TOTAL, CAmount{0});
    batch.Write(DB_CLAIM_COUNT, int64_t{0});
    batch.Erase(DB_CLAIM_BEST_BLOCK);
    m_db->WriteBatch(batch);
}

void CClaimIndex::SetBestBlock(const uint256& blockHash)
{
    m_db->Write(DB_CLAIM_BEST_BLOCK, blockHash);
}

uint256 CClaimIndex::GetBestBlock() const
{
    uint256 h;
    m_db->Read(DB_CLAIM_BEST_BLOCK, h);
    return h;
}
