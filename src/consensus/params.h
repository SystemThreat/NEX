// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <script/verify_flags.h>
#include <uint256.h>

#include <array>
#include <chrono>
#include <limits>
#include <map>
#include <vector>

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 * Consensus changes for which the new rules are enforced from genesis are not listed here.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    // SCRIPT_VERIFY_WITNESS is enforced from genesis, but the check for downloading
    // missing witness data is not. BIP 147 also relies on hardcoded activation height.
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    // Removing an entry may require bumping MinBIP9WarningHeight.
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};
    /** Period of blocks to check signalling in (usually retarget period, ie params.DifficultyAdjustmentInterval()) */
    uint32_t period{2016};
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t threshold{1916};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * NEX Emission Era: defines the block reward for a range of block heights.
 * See monetary-policy.md for the full specification.
 */
struct EmissionEra {
    int startHeight;          //!< First block of this era (inclusive)
    int endHeight;            //!< Last block of this era (inclusive)
    int64_t baseSubsidy;      //!< Per-block reward in satoshis (exact, no remainder)
};

/** Number of emission eras in the NEX monetary policy. */
static constexpr int NUM_EMISSION_ERAS = 8;

/** Blocks per emission era. */
static constexpr int BLOCKS_PER_ERA = 375'000;

/** Total emission across all eras: 75,000,000 NEX in satoshis. */
static constexpr int64_t TOTAL_EMISSION_SAT = 7'500'000'000'000'000LL;

/** Genesis allocation: 20,000,000 NEX in satoshis. */
static constexpr int64_t GENESIS_ALLOCATION_SAT = 2'000'000'000'000'000LL;

/** Claim pool cap: 5,000,000 NEX in satoshis (5 NEX × 1M eligible addresses). */
static constexpr int64_t CLAIM_POOL_CAP_SAT = 500'000'000'000'000LL;

/** Maximum possible supply: 100,000,000 NEX in satoshis. */
static constexpr int64_t MAX_SUPPLY_SAT = 10'000'000'000'000'000LL;

/** Last block that produces a subsidy (inclusive). Block 2,999,999. */
static constexpr int FINAL_SUBSIDY_HEIGHT = NUM_EMISSION_ERAS * BLOCKS_PER_ERA - 1;

/** Claim deadline: 2028-09-12 00:00:00 UTC as Unix timestamp. */
static constexpr int64_t CLAIM_DEADLINE_TIMESTAMP = 1852329600LL;

/**
 * The NEX emission table.
 *
 * 100 NEX/block × 375,000 blocks/era × 8 eras with clean halving.
 * Every era divides exactly — zero remainder, no last-block bonus needed.
 *
 * Era allocations (NEX):
 *   Era 1: 37,500,000    Era 2: 18,750,000    Era 3: 9,375,000
 *   Era 4:  4,687,500    Era 5:  2,343,750    Era 6: 1,171,875
 *   Era 7:    585,937.5  Era 8:    585,937.5
 *   Total: 75,000,000.00000000 NEX
 *
 * Supply identity: 20M genesis + 5M claims + 75M emission = 100M max
 *
 * Timeline (at 5-min blocks, launch April 1, 2026):
 *   Era 1: Apr 2026 – Oct 2029    Era 5: Jul 2040 – Jan 2044
 *   Era 2: Oct 2029 – May 2033    Era 6: Jan 2044 – Aug 2047
 *   Era 3: May 2033 – Dec 2036    Era 7: Aug 2047 – Mar 2051
 *   Era 4: Dec 2036 – Jul 2040    Era 8: Mar 2051 – Oct 2054
 *   Last NEX mined: October 7, 2054
 */
static constexpr EmissionEra EMISSION_TABLE[NUM_EMISSION_ERAS] = {
    //  startHeight    endHeight   baseSubsidy(sat)
    {         0,        374999,   10'000'000'000LL },  // Era 1: 100.00 NEX/block  37,500,000 NEX
    {    375000,        749999,    5'000'000'000LL },  // Era 2:  50.00 NEX/block  18,750,000 NEX
    {    750000,       1124999,    2'500'000'000LL },  // Era 3:  25.00 NEX/block   9,375,000 NEX
    {   1125000,       1499999,    1'250'000'000LL },  // Era 4:  12.50 NEX/block   4,687,500 NEX
    {   1500000,       1874999,      625'000'000LL },  // Era 5:   6.25 NEX/block   2,343,750 NEX
    {   1875000,       2249999,      312'500'000LL },  // Era 6:   3.125 NEX/block  1,171,875 NEX
    {   2250000,       2624999,      156'250'000LL },  // Era 7:   1.5625 NEX/block   585,937.5 NEX
    {   2625000,       2999999,      156'250'000LL },  // Era 8:   1.5625 NEX/block   585,937.5 NEX
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;

    /** Merkle root of the BTC snapshot eligible-address set.
     *  Computed from the Bitcoin block 940,000 UTXO snapshot.
     *  Set to all-zeros until the snapshot is taken. */
    uint256 claimMerkleRoot;
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, script_verify_flags> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV, segwit and taproot activations. */
    int MinBIP9WarningHeight;
    std::array<BIP9Deployment,MAX_VERSION_BITS_DEPLOYMENTS> vDeployments;
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    /**
      * Enforce BIP94 timewarp attack mitigation. On testnet4 this also enforces
      * the block storm mitigation.
      */
    bool enforce_BIP94;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    std::chrono::seconds PowTargetSpacing() const
    {
        return std::chrono::seconds{nPowTargetSpacing};
    }
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
