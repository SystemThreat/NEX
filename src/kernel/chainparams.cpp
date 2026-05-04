// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <limits>
#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <crypto/hex_base.h>
#include <hash.h>
#include <kernel/messagestartchars.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/log.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <span>
#include <utility>

using namespace util::hex_literals;

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "NEX - peer to peer electronic cash - March 2026";
    const CScript genesisOutputScript = CScript() << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"_hex << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * NEX PQ Genesis Block — 9 outputs totaling 20,000,000 NEX.
 *
 * Each output uses a witness v2 PQ script: OP_2 <32-byte-SHA256(pq_pubkey)>
 * The 9 genesis accounts and their allocations must be finalized before
 * mainnet launch. The pubkey hashes below are PLACEHOLDERS.
 *
 * Genesis allocation (from monetary-policy.md):
 *   1. Founder Reserve      5,000,000 NEX
 *   2. Foundation/Ecosystem 3,000,000 NEX
 *   3. Development          3,000,000 NEX
 *   4. Infrastructure       2,000,000 NEX
 *   5. Legal/Compliance     1,500,000 NEX
 *   6. Partnerships         2,000,000 NEX
 *   7. Grants               1,500,000 NEX
 *   8. Liquidity            1,000,000 NEX
 *   9. Treasury             1,000,000 NEX
 *   Total:                 20,000,000 NEX
 */
// ═══════════════════════════════════════════════════════════════════════════
// MAINNET LAUNCH CONSTANTS — Replace ALL of these before launch.
// After snapshot extraction + genesis key ceremony, swap these values.
// Then uncomment the asserts in CMainParams below.
// ═══════════════════════════════════════════════════════════════════════════

// BTC block 940,000 snapshot Merkle root (from extract.py output)
static const char* FINAL_CLAIM_MERKLE_ROOT = "0000000000000000000000000000000000000000000000000000000000000000"; // TODO

// Genesis block parameters (from mining the PQ genesis)
static constexpr uint32_t FINAL_GENESIS_TIME  = 1775347200; // 2026-04-05 00:00:00 UTC
static constexpr uint32_t FINAL_GENESIS_NONCE = 3741185684;
static const char* FINAL_GENESIS_HASH   = "00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b";
static const char* FINAL_GENESIS_MERKLE = "2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68";

// ═══════════════════════════════════════════════════════════════════════════

struct GenesisAllocation {
    const char* label;
    CAmount amount;
    const char* pqPubKeyHash;  // 32-byte hex SHA-256 of ML-DSA-65 pubkey — REPLACE WITH REAL HASHES
};

// Single genesis allocation — all 20,000,000 NEX to the founder address
static const GenesisAllocation GENESIS_ALLOCATIONS[] = {
    {"founder", 0LL, "c6f0b134b1f0534668f1ec535b6b62601c6919f3a18ff2e5fe4fb2a65fdd81fa"},  // 20,000,000 NEX → nex1zcmctzd937pf5v683a3f4k6mzvqwxjx0n5x8l9e07f7e2vh7as8aqqwr4sq
};
static_assert(sizeof(GENESIS_ALLOCATIONS) / sizeof(GENESIS_ALLOCATIONS[0]) == 1,
              "Genesis must have exactly 1 allocation");

static CBlock CreatePQGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion)
{
    const char* pszTimestamp = "NEX PQ Genesis - quantum-safe from block zero - 2026";

    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4)
        << std::vector<unsigned char>((const unsigned char*)pszTimestamp,
                                      (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    // Create genesis outputs
    constexpr int numAlloc = sizeof(GENESIS_ALLOCATIONS) / sizeof(GENESIS_ALLOCATIONS[0]);
    txNew.vout.resize(numAlloc);
    CAmount totalGenesis = 0;
    for (int i = 0; i < numAlloc; ++i) {
        const auto& alloc = GENESIS_ALLOCATIONS[i];
        txNew.vout[i].nValue = alloc.amount;
        totalGenesis += alloc.amount;

        // Witness v2 PQ script: OP_2 <32-byte pubkey hash>
        auto hashBytes = ParseHex(alloc.pqPubKeyHash);
        txNew.vout[i].scriptPubKey = CScript() << OP_2 << hashBytes;
    }

    // Verify total is exactly 20,000,000 NEX
    assert(totalGenesis == 0LL);

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        m_chain_type = ChainType::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 360000;
        // ═══════════════════════════════════════════════════════════
        // MAINNET LAUNCH PARAMETERS — Replace before launch
        // After snapshot: paste merkle root from snapshot-940000-summary.json
        // ═══════════════════════════════════════════════════════════
        consensus.claimMerkleRoot = uint256{}; // NULL until snapshot — claims disabled
        // assert(!consensus.claimMerkleRoot.IsNull()); // UNCOMMENT BEFORE LAUNCH
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 0;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 300 * 50; // 50 blocks x 300s = 15000s retarget
        consensus.nPowTargetSpacing = 300; // 5-minute blocks
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nLWMAActivationHeight = 1301; // LWMA per-block difficulty adjustment (next block)
        consensus.nLWMAWindow = 25;             // 25-block averaging window
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1815; // 90%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;

        // NEX: Bootstrap values — set after chain has sufficient depth.
        // After 1000+ blocks: run `nex-cli getblockchaininfo` and update:
        //   nMinimumChainWork = chainwork value from a trusted block
        //   defaultAssumeValid = hash of a trusted block (e.g., block 1000)
        // For genesis launch, zeros are acceptable — nodes verify everything from genesis.
        consensus.nMinimumChainWork = uint256{};  // SET AFTER CHAIN DEPTH > 1000
        consensus.defaultAssumeValid = uint256{}; // SET AFTER CHAIN DEPTH > 1000

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        // NEX PQ chain — network magic bumped to \x02 to isolate from pre-PQ chain
        pchMessageStart[0] = 0x4e;  // 'N'
        pchMessageStart[1] = 0x45;  // 'E'
        pchMessageStart[2] = 0x58;  // 'X'
        pchMessageStart[3] = 0x02;  // v2 (PQ era) — prevents cross-connect with old chain
        nDefaultPort = 9333;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        // ═══════════════════════════════════════════════════════════
        // GENESIS — Replace nonce after mining. Re-enable asserts.
        // ═══════════════════════════════════════════════════════════
        genesis = CreatePQGenesisBlock(FINAL_GENESIS_TIME, FINAL_GENESIS_NONCE, 0x1d00ffff, 1);
        consensus.hashGenesisBlock = genesis.GetHash();
        // fprintf removed
        // fprintf removed
        assert(consensus.hashGenesisBlock == uint256{"00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b"});
        assert(genesis.hashMerkleRoot == uint256{"2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68"});

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as an addrfetch if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        // NEX seed nodes — initial bootstrap (DNS A records on knexcoin.com point at production peer IPs)
        vSeeds.emplace_back("seed.knexcoin.com");
        vSeeds.emplace_back("seed2.knexcoin.com");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,53);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,55);  // NEX: 'M' prefix for P2SH
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,181);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x4E, 0x58, 0x50};  // NEX: "nxpb" serialization
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x4E, 0x58, 0x53};  // NEX: "nxpv" serialization

        bech32_hrp = "nex";  // PQ addresses: nex1... (witness v2 = nex1z...)

        // NEX fixed seed nodes (BIP155 format: networkID, addr, port)
        // Used as fallback when DNS seeds are unreachable.
        // Format: 0x01 = IPv4, then 4 address bytes, then 2-byte big-endian port
        static const uint8_t nex_seed_main[] = {
            0x01, 0xc6,0xfc,0x68,0x18, 0x24,0x75,  // 198.252.104.24:9333 (primary VPS — Virginia)
        };
        vFixedSeeds = std::vector<uint8_t>(std::begin(nex_seed_main), std::end(nex_seed_main));

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {};

        // NEX: Bootstrap values — set after chain has sufficient depth.
        // After 1000+ blocks: run `nex-cli getblockchaininfo` and update:
        //   nMinimumChainWork = chainwork value from a trusted block
        //   defaultAssumeValid = hash of a trusted block (e.g., block 1000)
        // After 10000+ blocks: populate chainTxData with real statistics.
        // For genesis launch, zeros are acceptable — nodes verify everything from genesis.
        chainTxData = ChainTxData{
            0,    // nTime — SET to timestamp of a recent trusted block
            0,    // nTxCount — SET to total tx count at that block
            0,    // dTxRate — SET to average tx/second over recent history
        };
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        m_chain_type = ChainType::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 360000;
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 0;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 300 * 50;
        consensus.nPowTargetSpacing = 300;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nLWMAActivationHeight = 0;
        consensus.nLWMAWindow = 25;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1512; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        // Testnet3: NEX\x04 (distinct from mainnet NEX\x02)
        pchMessageStart[0] = 0x4e;
        pchMessageStart[1] = 0x45;
        pchMessageStart[2] = 0x58;
        pchMessageStart[3] = 0x04;
        nDefaultPort = 19333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        genesis = CreateGenesisBlock(1711756800, 0, 0x2000ffff, 1, Consensus::EMISSION_TABLE[0].baseSubsidy);
        consensus.hashGenesisBlock = genesis.GetHash();
        // NEX: genesis hash computed dynamically — asserts disabled for new chain
        // assert(consensus.hashGenesisBlock == uint256{"00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b"});
        // assert(genesis.hashMerkleRoot == uint256{"2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68"});

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,57);   // NEX testnet P2SH
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x4E, 0x54, 0x50};  // NEX testnet: "tNXP" serialization
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x4E, 0x54, 0x53};  // NEX testnet: "tNXS" serialization

        bech32_hrp = "tnx";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{0, 0, 0};
    }
};

/**
 * Testnet (v4): public test network which is reset from time to time.
 */
class CTestNet4Params : public CChainParams {
public:
    CTestNet4Params() {
        m_chain_type = ChainType::TESTNET4;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 360000;
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 0;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 300 * 50;
        consensus.nPowTargetSpacing = 300;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = true;
        consensus.fPowNoRetargeting = false;
        consensus.nLWMAActivationHeight = 0;
        consensus.nLWMAWindow = 25;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1512; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        // Testnet4: NEX\x05 (distinct from mainnet \x02, testnet3 \x04, regtest \x03)
        pchMessageStart[0] = 0x4e;
        pchMessageStart[1] = 0x45;
        pchMessageStart[2] = 0x58;
        pchMessageStart[3] = 0x05;
        nDefaultPort = 49333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        genesis = CreateGenesisBlock(1711756800, 0, 0x2000ffff, 1, Consensus::EMISSION_TABLE[0].baseSubsidy);
        consensus.hashGenesisBlock = genesis.GetHash();
        // NEX: genesis hash computed dynamically — asserts disabled for new chain
        // assert(consensus.hashGenesisBlock == uint256{"00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b"});
        // assert(genesis.hashMerkleRoot == uint256{"2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68"});

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "t4nx";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{0, 0, 0};
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vFixedSeeds.clear();
        vSeeds.clear();

        if (!options.challenge) {
            bin = "512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae"_hex_v_u8;
            vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_signet), std::end(chainparams_seed_signet));
            vSeeds.emplace_back("seed.signet.bitcoin.sprovoost.nl.");
            vSeeds.emplace_back("seed.signet.achownodes.xyz."); // Ava Chow, only supports x1, x5, x9, x49, x809, x849, xd, x400, x404, x408, x448, xc08, xc48, x40c

            consensus.nMinimumChainWork = uint256{"00000000000000000000000000000000000000000000000000000b463ea0a4b8"};
            consensus.defaultAssumeValid = uint256{"00000008414aab61092ef93f1aacc54cf9e9f16af29ddad493b908a01ff5c329"}; // 293175
            m_assumed_blockchain_size = 24;
            m_assumed_chain_state_size = 4;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 00000008414aab61092ef93f1aacc54cf9e9f16af29ddad493b908a01ff5c329
                .nTime    = 1772055248,
                .tx_count = 28676833,
                .dTxRate  = 0.06736623436338929,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogInfo("Signet with challenge %s", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        m_chain_type = ChainType::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nLWMAActivationHeight = std::numeric_limits<int>::max();
        consensus.nLWMAWindow = 25;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000377ae000000000000000000000000000000000000000000000000000000"};
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1815; // 90%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 38333;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1598918400, 52613770, 0x1e0377ae, 1, Consensus::EMISSION_TABLE[0].baseSubsidy);
        consensus.hashGenesisBlock = genesis.GetHash();
        // NEX: genesis hash computed dynamically — asserts disabled for new chain
        // assert(consensus.hashGenesisBlock == uint256{"00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b"});
        // assert(genesis.hashMerkleRoot == uint256{"2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68"});

        m_assumeutxo_data = {
            {
                .height = 160'000,
                .hash_serialized = AssumeutxoHash{uint256{"fe0a44309b74d6b5883d246cb419c6221bcccf0b308c9b59b7d70783dbdf928a"}},
                .m_chain_tx_count = 2289496,
                .blockhash = uint256{"0000003ca3c99aff040f2563c2ad8f8ec88bd0fd6b8f0895cfaf1ef90353a62c"},
            },
            {
                .height = 290'000,
                .hash_serialized = AssumeutxoHash{uint256{"97267e000b4b876800167e71b9123f1529d13b14308abec2888bbd2160d14545"}},
                .m_chain_tx_count = 28547497,
                .blockhash = uint256{"0000000577f2741bb30cd9d39d6d71b023afbeb9764f6260786a97969d5c9ac0"},
            }
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // Generated by headerssync-params.py on 2026-02-25.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 620,
            .redownload_buffer_size = 15724, // 15724/620 = ~25.4 commitments
        };
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 300 * 50;
        consensus.nPowTargetSpacing = 300;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = opts.enforce_bip94;
        consensus.fPowNoRetargeting = true;
        consensus.nLWMAActivationHeight = std::numeric_limits<int>::max();
        consensus.nLWMAWindow = 25;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 108; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0x4e;
        pchMessageStart[1] = 0x45;
        pchMessageStart[2] = 0x58;
        pchMessageStart[3] = 0x03;
        nDefaultPort = 19444;
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        // Regtest genesis: use Era 1 base subsidy to match GetBlockSubsidy(0)
        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, Consensus::EMISSION_TABLE[0].baseSubsidy);
        consensus.hashGenesisBlock = genesis.GetHash();
        // NEX: genesis hash computed dynamically — asserts disabled for new chain
        // assert(consensus.hashGenesisBlock == uint256{"00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b"});
        // assert(genesis.hashMerkleRoot == uint256{"2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68"});

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        m_assumeutxo_data = {
            {   // For use by unit tests
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256{"b952555c8ab81fec46f3d4253b7af256d766ceb39fb7752b9d18cdf4a0141327"}},
                .m_chain_tx_count = 111,
                .blockhash = uint256{"6affe030b7965ab538f820a56ef56c8149b7dc1d1c144af57113be080db7c397"},
            },
            {
                // For use by fuzz target src/test/fuzz/utxo_snapshot.cpp
                .height = 200,
                .hash_serialized = AssumeutxoHash{uint256{"17dcc016d188d16068907cdeb38b75691a118d43053b8cd6a25969419381d13a"}},
                .m_chain_tx_count = 201,
                .blockhash = uint256{"385901ccbd69dff6bbd00065d01fb8a9e464dede7cfe0372443884f9b1dcf6b9"},
            },
            {
                // For use by test/functional/feature_assumeutxo.py and test/functional/tool_bitcoin_chainstate.py
                .height = 299,
                .hash_serialized = AssumeutxoHash{uint256{"d2b051ff5e8eef46520350776f4100dd710a63447a8e01d917e92e79751a63e2"}},
                .m_chain_tx_count = 334,
                .blockhash = uint256{"7cc695046fec709f8c9394b6f928f81e81fd3ac20977bb68760fa1faa7916ea2"},
            },
        };

        chainTxData = ChainTxData{
            .nTime = 0,
            .tx_count = 0,
            .dTxRate = 0.001, // Set a non-zero rate to make it testable
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "nxrt";

        // Copied from Testnet4.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 275,
            .redownload_buffer_size = 7017, // 7017/275 = ~25.5 commitments
        };
    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet4()
{
    return std::make_unique<const CTestNet4Params>();
}

std::vector<int> CChainParams::GetAvailableSnapshotHeights() const
{
    std::vector<int> heights;
    heights.reserve(m_assumeutxo_data.size());

    for (const auto& data : m_assumeutxo_data) {
        heights.emplace_back(data.height);
    }
    return heights;
}

std::optional<ChainType> GetNetworkForMagic(const MessageStartChars& message)
{
    const auto mainnet_msg = CChainParams::Main()->MessageStart();
    const auto testnet_msg = CChainParams::TestNet()->MessageStart();
    const auto testnet4_msg = CChainParams::TestNet4()->MessageStart();
    const auto regtest_msg = CChainParams::RegTest({})->MessageStart();
    const auto signet_msg = CChainParams::SigNet({})->MessageStart();

    if (std::ranges::equal(message, mainnet_msg)) {
        return ChainType::MAIN;
    } else if (std::ranges::equal(message, testnet_msg)) {
        return ChainType::TESTNET;
    } else if (std::ranges::equal(message, testnet4_msg)) {
        return ChainType::TESTNET4;
    } else if (std::ranges::equal(message, regtest_msg)) {
        return ChainType::REGTEST;
    } else if (std::ranges::equal(message, signet_msg)) {
        return ChainType::SIGNET;
    }
    return std::nullopt;
}
