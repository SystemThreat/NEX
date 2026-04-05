// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <claim.h>

#include <consensus/amount.h>
#include <consensus/params.h>
#include <hash.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <uint256.h>

#include <streams.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <functional>
#include <vector>

namespace {

// ── Test Helpers ─────────────────────────────────────────────────────────────

uint256 TestSHA256d(const unsigned char* data, size_t len)
{
    uint256 tmp, result;
    CSHA256().Write(data, len).Finalize(tmp.data());
    CSHA256().Write(tmp.data(), 32).Finalize(result.data());
    return result;
}

uint256 TestSHA256d(const uint256& a, const uint256& b)
{
    unsigned char combined[64];
    std::memcpy(combined, a.data(), 32);
    std::memcpy(combined + 32, b.data(), 32);
    return TestSHA256d(combined, 64);
}

/** Build a leaf hash matching the snapshot extractor's format. */
uint256 TestLeafHash(const std::vector<unsigned char>& spk, int64_t balanceSat)
{
    std::vector<unsigned char> data(spk.begin(), spk.end());
    uint64_t bal = static_cast<uint64_t>(balanceSat);
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<unsigned char>(bal & 0xFF));
        bal >>= 8;
    }
    return TestSHA256d(data.data(), data.size());
}

/** Build a simple 2-leaf Merkle tree and return root + proof for leaf 0. */
struct SimpleMerkle {
    uint256 root;
    std::vector<uint256> proofForLeaf0;
};

SimpleMerkle BuildTwoLeafTree(const uint256& leaf0, const uint256& leaf1)
{
    SimpleMerkle result;
    if (leaf0 < leaf1) {
        result.root = TestSHA256d(leaf0, leaf1);
    } else {
        result.root = TestSHA256d(leaf1, leaf0);
    }
    result.proofForLeaf0 = {leaf1};
    return result;
}

/** Build a P2PKH scriptPubKey from a compressed pubkey. */
std::vector<unsigned char> MakeP2PKH(const CPubKey& pubkey)
{
    CKeyID keyID = pubkey.GetID();
    std::vector<unsigned char> spk;
    spk.push_back(0x76); // OP_DUP
    spk.push_back(0xa9); // OP_HASH160
    spk.push_back(0x14); // push 20 bytes
    spk.insert(spk.end(), keyID.begin(), keyID.end());
    spk.push_back(0x88); // OP_EQUALVERIFY
    spk.push_back(0xac); // OP_CHECKSIG
    return spk;
}

/** Build a P2WPKH scriptPubKey from a compressed pubkey. */
std::vector<unsigned char> MakeP2WPKH(const CPubKey& pubkey)
{
    CKeyID keyID = pubkey.GetID();
    std::vector<unsigned char> spk;
    spk.push_back(0x00); // OP_0
    spk.push_back(0x14); // push 20 bytes
    spk.insert(spk.end(), keyID.begin(), keyID.end());
    return spk;
}

/** Sign a Bitcoin message with a CKey. Returns 65-byte compact signature. */
std::vector<unsigned char> SignBitcoinMessage(const CKey& key, const std::string& message)
{
    static const std::string BTC_MSG_MAGIC = "\x18" "Bitcoin Signed Message:\n";

    HashWriter hasher{};
    hasher.write(MakeByteSpan(std::span<const char>{BTC_MSG_MAGIC.data(), BTC_MSG_MAGIC.size()}));
    uint8_t sz = static_cast<uint8_t>(message.size());
    hasher.write(MakeByteSpan(std::span<const uint8_t>{&sz, 1}));
    hasher.write(MakeByteSpan(std::span<const char>{message.data(), message.size()}));
    uint256 msgHash = hasher.GetHash();

    std::vector<unsigned char> sig;
    key.SignCompact(msgHash, sig);
    return sig;
}

/** Create a serialized ClaimData and pack it into a claim transaction. */
CTransaction MakeClaimTx(const ClaimData& claim, const CScript& destScript, CAmount amount = CLAIM_AMOUNT)
{
    CMutableTransaction mtx;
    mtx.version = TX_CLAIM_VERSION;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    // Serialize ClaimData into scriptSig
    DataStream ss{};
    ss << claim;
    std::vector<unsigned char> claimBytes(ss.size());
    std::memcpy(claimBytes.data(), ss.data(), ss.size());
    mtx.vin[0].scriptSig = CScript(claimBytes.begin(), claimBytes.end());

    mtx.vout.resize(1);
    mtx.vout[0].nValue = amount;
    mtx.vout[0].scriptPubKey = destScript;

    return CTransaction(mtx);
}

} // anonymous namespace


BOOST_FIXTURE_TEST_SUITE(claim_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(claim_is_claim_tx)
{
    // A claim tx has version=3, 1 null input, 1 output
    CMutableTransaction mtx;
    mtx.version = TX_CLAIM_VERSION;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vout.resize(1);
    mtx.vout[0].nValue = CLAIM_AMOUNT;
    CTransaction tx(mtx);
    BOOST_CHECK(IsClaimTx(tx));

    // Wrong version
    mtx.version = 2;
    BOOST_CHECK(!IsClaimTx(CTransaction(mtx)));

    // Multiple outputs
    mtx.version = TX_CLAIM_VERSION;
    mtx.vout.resize(2);
    BOOST_CHECK(!IsClaimTx(CTransaction(mtx)));
}

BOOST_AUTO_TEST_CASE(claim_merkle_proof_two_leaves)
{
    // Build a 2-leaf Merkle tree and verify proof
    std::vector<unsigned char> spk1 = {0x76, 0xa9, 0x14, 0x01, 0x02, 0x03, 0x04, 0x05,
                                        0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                                        0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x88, 0xac};
    std::vector<unsigned char> spk2 = {0x76, 0xa9, 0x14, 0x21, 0x22, 0x23, 0x24, 0x25,
                                        0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
                                        0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x88, 0xac};

    int64_t bal1 = 200'000'000; // 2 BTC
    int64_t bal2 = 500'000'000; // 5 BTC

    uint256 leaf1 = TestLeafHash(spk1, bal1);
    uint256 leaf2 = TestLeafHash(spk2, bal2);

    auto tree = BuildTwoLeafTree(leaf1, leaf2);

    // Proof for spk1 should verify
    BOOST_CHECK(VerifyClaimMerkleProof(spk1, bal1, tree.proofForLeaf0, tree.root));

    // Wrong balance should fail
    BOOST_CHECK(!VerifyClaimMerkleProof(spk1, 300'000'000, tree.proofForLeaf0, tree.root));

    // Wrong scriptPubKey should fail
    BOOST_CHECK(!VerifyClaimMerkleProof(spk2, bal1, tree.proofForLeaf0, tree.root));

    // Null root should fail
    BOOST_CHECK(!VerifyClaimMerkleProof(spk1, bal1, tree.proofForLeaf0, uint256{}));
}

BOOST_AUTO_TEST_CASE(claim_btc_signature_p2pkh)
{
    // Generate a BTC key and sign a claim message
    CKey btcKey = GenerateRandomKey(true);
    CPubKey btcPub = btcKey.GetPubKey();
    std::vector<unsigned char> spk = MakeP2PKH(btcPub);

    std::string claimMsg = "NEX CLAIM v1\nBitcoin Snapshot Block: 940000\nClaim To: nex1pqtest\nNonce: abc123";
    std::vector<unsigned char> sig = SignBitcoinMessage(btcKey, claimMsg);
    std::vector<unsigned char> msgBytes(claimMsg.begin(), claimMsg.end());

    BOOST_CHECK(VerifyClaimBTCSignature(spk, sig, msgBytes));

    // Wrong message should fail
    std::string wrongMsg = "WRONG MESSAGE";
    std::vector<unsigned char> wrongBytes(wrongMsg.begin(), wrongMsg.end());
    BOOST_CHECK(!VerifyClaimBTCSignature(spk, sig, wrongBytes));

    // Different key's spk should fail
    CKey otherKey = GenerateRandomKey(true);
    std::vector<unsigned char> otherSpk = MakeP2PKH(otherKey.GetPubKey());
    BOOST_CHECK(!VerifyClaimBTCSignature(otherSpk, sig, msgBytes));
}

BOOST_AUTO_TEST_CASE(claim_btc_signature_p2wpkh)
{
    CKey btcKey = GenerateRandomKey(true);
    CPubKey btcPub = btcKey.GetPubKey();
    std::vector<unsigned char> spk = MakeP2WPKH(btcPub);

    std::string claimMsg = "NEX CLAIM v1\nBitcoin Snapshot Block: 940000\nClaim To: nex1pqtest\nNonce: def456";
    std::vector<unsigned char> sig = SignBitcoinMessage(btcKey, claimMsg);
    std::vector<unsigned char> msgBytes(claimMsg.begin(), claimMsg.end());

    BOOST_CHECK(VerifyClaimBTCSignature(spk, sig, msgBytes));
}

BOOST_AUTO_TEST_CASE(claim_validate_full_happy_path)
{
    // Full claim validation: generate BTC key, build Merkle tree, create claim tx, validate
    CKey btcKey = GenerateRandomKey(true);
    CPubKey btcPub = btcKey.GetPubKey();
    std::vector<unsigned char> spk = MakeP2PKH(btcPub);
    int64_t balance = 150'000'000; // 1.5 BTC

    // Build a simple 2-leaf Merkle tree
    std::vector<unsigned char> spk2 = {0x00, 0x14, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
                                        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd};
    uint256 leaf1 = TestLeafHash(spk, balance);
    uint256 leaf2 = TestLeafHash(spk2, 500'000'000);
    auto tree = BuildTwoLeafTree(leaf1, leaf2);

    // Sign the claim message
    std::string claimMsg = "NEX CLAIM v1\nBitcoin Snapshot Block: 940000\nClaim To: nex1pqtest\nNonce: test123";
    std::vector<unsigned char> sig = SignBitcoinMessage(btcKey, claimMsg);

    // Build claim data
    ClaimData claim;
    claim.btcScriptPubKey = spk;
    claim.btcSignature = sig;
    claim.claimMessage = std::vector<unsigned char>(claimMsg.begin(), claimMsg.end());
    claim.merkleProof = tree.proofForLeaf0;
    claim.snapshotBalanceSat = balance;

    // Build a PQ destination script (OP_2 <32 bytes>)
    std::vector<unsigned char> pqHash(32, 0x42);
    CScript destScript = CScript() << OP_2 << pqHash;

    CTransaction tx = MakeClaimTx(claim, destScript);
    BOOST_CHECK(IsClaimTx(tx));

    // Validate — should pass
    int64_t blockTime = 1700000000; // well before deadline
    int64_t totalClaimed = 0;
    auto notClaimed = [](const std::vector<unsigned char>&) { return false; };

    std::string err = ValidateClaimTx(tx, blockTime, totalClaimed, tree.root, notClaimed);
    BOOST_CHECK_MESSAGE(err.empty(), "Expected valid claim, got: " + err);
}

BOOST_AUTO_TEST_CASE(claim_validate_wrong_amount)
{
    ClaimData claim;
    claim.btcScriptPubKey = {0x76, 0xa9, 0x14};
    claim.btcScriptPubKey.resize(25, 0x01);
    claim.btcSignature.resize(65, 0x00);
    claim.claimMessage = {'t', 'e', 's', 't'};
    claim.snapshotBalanceSat = 100'000'000;

    CScript dest = CScript() << OP_2 << std::vector<unsigned char>(32, 0x42);
    CTransaction tx = MakeClaimTx(claim, dest, 2 * COIN); // wrong: 2 NEX instead of 5

    std::string err = ValidateClaimTx(tx, 1700000000, 0, uint256{}, [](auto&) { return false; });
    BOOST_CHECK_EQUAL(err, "claim-wrong-amount");
}

BOOST_AUTO_TEST_CASE(claim_validate_expired)
{
    ClaimData claim;
    claim.btcScriptPubKey = {0x76, 0xa9, 0x14};
    claim.btcScriptPubKey.resize(25, 0x01);
    claim.btcSignature.resize(65, 0x00);
    claim.claimMessage = {'t', 'e', 's', 't'};
    claim.snapshotBalanceSat = 100'000'000;

    CScript dest = CScript() << OP_2 << std::vector<unsigned char>(32, 0x42);
    CTransaction tx = MakeClaimTx(claim, dest);

    // Block time past deadline
    std::string err = ValidateClaimTx(tx, Consensus::CLAIM_DEADLINE_TIMESTAMP + 1, 0, uint256{},
                                       [](auto&) { return false; });
    BOOST_CHECK_EQUAL(err, "claim-expired");
}

BOOST_AUTO_TEST_CASE(claim_validate_already_claimed)
{
    ClaimData claim;
    claim.btcScriptPubKey = {0x76, 0xa9, 0x14};
    claim.btcScriptPubKey.resize(25, 0x01);
    claim.btcSignature.resize(65, 0x00);
    claim.claimMessage = {'t', 'e', 's', 't'};
    claim.snapshotBalanceSat = 100'000'000;

    CScript dest = CScript() << OP_2 << std::vector<unsigned char>(32, 0x42);
    CTransaction tx = MakeClaimTx(claim, dest);

    // Already claimed
    std::string err = ValidateClaimTx(tx, 1700000000, 0, uint256{},
                                       [](auto&) { return true; });
    BOOST_CHECK_EQUAL(err, "claim-already-claimed");
}

BOOST_AUTO_TEST_CASE(claim_validate_pool_exhausted)
{
    ClaimData claim;
    claim.btcScriptPubKey = {0x76, 0xa9, 0x14};
    claim.btcScriptPubKey.resize(25, 0x01);
    claim.btcSignature.resize(65, 0x00);
    claim.claimMessage = {'t', 'e', 's', 't'};
    claim.snapshotBalanceSat = 100'000'000;

    CScript dest = CScript() << OP_2 << std::vector<unsigned char>(32, 0x42);
    CTransaction tx = MakeClaimTx(claim, dest);

    // Pool full
    std::string err = ValidateClaimTx(tx, 1700000000, Consensus::CLAIM_POOL_CAP_SAT, uint256{},
                                       [](auto&) { return false; });
    BOOST_CHECK_EQUAL(err, "claim-pool-exhausted");
}

BOOST_AUTO_TEST_SUITE_END()
