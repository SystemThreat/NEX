// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // After LWMA activation height, use per-block LWMA difficulty adjustment
    if (pindexLast->nHeight + 1 >= params.nLWMAActivationHeight) {
        return LwmaGetNextWorkRequired(pindexLast, pblock, params);
    }

    // --- Pre-LWMA: classic Bitcoin-style retarget every DifficultyAdjustmentInterval() blocks ---

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

// LWMA: Linearly Weighted Moving Average difficulty adjustment.
// Adjusts every block using a weighted average of the last N block solve times.
// Based on zawy12's LWMA algorithm used by Monero, Ravencoin, and others.
unsigned int LwmaGetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    const int N = params.nLWMAWindow;
    const int64_t T = params.nPowTargetSpacing;

    // ── Emergency Difficulty Adjustment (EDA) ──
    // Deterministic: uses ONLY on-chain data (previous block solve time).
    // If the last block took longer than 2*T, halve difficulty for each 2*T interval.
    // This uses pindexLast timestamps — identical result whether called from
    // getblocktemplate or block validation.
    if (pindexLast->pprev != nullptr) {
        int64_t lastSolveTime = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();
        if (lastSolveTime > 2 * T) {
            arith_uint256 bnEDA;
            bnEDA.SetCompact(pindexLast->nBits);
            int halvings = (int)(lastSolveTime / (2 * T));
            for (int i = 0; i < halvings && i < 64; i++) {
                bnEDA *= 2;
            }
            if (bnEDA > bnPowLimit) bnEDA = bnPowLimit;
            return bnEDA.GetCompact();
        }
    }

    // Not enough blocks for a full LWMA window — return powLimit
    if (pindexLast->nHeight < N)
        return bnPowLimit.GetCompact();

    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(pindexLast->nHeight - N);
    if (pindexFirst == nullptr)
        return bnPowLimit.GetCompact();

    // ── LWMA: Linearly Weighted Moving Average ──
    const int64_t maxSolvetime = 6 * T;
    int64_t weightedSolvetimes = 0;

    for (int i = 1; i <= N; i++) {
        const CBlockIndex* pCur = pindexLast->GetAncestor(pindexFirst->nHeight + i);
        const CBlockIndex* pPrev = pindexLast->GetAncestor(pindexFirst->nHeight + i - 1);

        int64_t solvetime = pCur->GetBlockTime() - pPrev->GetBlockTime();
        if (solvetime < 0) solvetime = 0;
        if (solvetime > maxSolvetime) solvetime = maxSolvetime;

        weightedSolvetimes += (int64_t)i * solvetime;
    }

    const int64_t weightSum = (int64_t)N * (N + 1) / 2;

    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= (uint32_t)weightedSolvetimes;
    bnNew /= (uint32_t)(weightSum * T);

    // Per-block ±4x clamp
    arith_uint256 bnPrev;
    bnPrev.SetCompact(pindexLast->nBits);

    arith_uint256 bnMax = bnPrev * 4;
    arith_uint256 bnMin = bnPrev / 4;

    if (bnNew > bnMax) bnNew = bnMax;
    if (bnNew < bnMin) bnNew = bnMin;

    if (bnNew > bnPowLimit) bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    const arith_uint256 pow_limit = UintToArith256(params.powLimit);

    // After LWMA activation: difficulty can change every block, within ±4x
    if (height >= params.nLWMAActivationHeight) {
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);
        if (observed_new_target > pow_limit) return false;

        arith_uint256 old_target;
        old_target.SetCompact(old_nbits);

        arith_uint256 max_target = old_target * 4;
        if (max_target > pow_limit) max_target = pow_limit;

        arith_uint256 min_target = old_target / 4;

        arith_uint256 maximum_new;
        maximum_new.SetCompact(max_target.GetCompact());
        if (maximum_new < observed_new_target) return false;

        arith_uint256 minimum_new;
        minimum_new.SetCompact(min_target.GetCompact());
        if (minimum_new > observed_new_target) return false;

        return true;
    }

    // Pre-LWMA: classic Bitcoin logic
    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;
        if (largest_difficulty_target > pow_limit) largest_difficulty_target = pow_limit;

        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;
        if (smallest_difficulty_target > pow_limit) smallest_difficulty_target = pow_limit;

        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
