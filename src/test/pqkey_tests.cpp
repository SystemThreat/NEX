// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pqkey.h>

#include <addresstype.h>
#include <hash.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pqkey_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(pqkey_generate)
{
    // Generate a fresh ML-DSA-65 keypair
    CPQKey key;
    CPQPubKey pubkey = key.MakeNewKey();

    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(pubkey.IsValid());
    BOOST_CHECK_EQUAL(pubkey.size(), PQ_PUBKEY_SIZE);
    BOOST_CHECK_EQUAL(key.size(), PQ_SECRETKEY_SIZE);
}

BOOST_AUTO_TEST_CASE(pqkey_sign_verify)
{
    CPQKey key;
    CPQPubKey pubkey = key.MakeNewKey();
    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(pubkey.IsValid());

    // Create a test hash
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>("test"), 4).Finalize(hash.data());

    std::vector<unsigned char> sig;
    BOOST_CHECK(key.Sign(hash, sig));
    BOOST_CHECK(!sig.empty());
    BOOST_CHECK(sig.size() <= PQ_SIGNATURE_SIZE);

    // Verify the signature
    BOOST_CHECK(pubkey.Verify(hash, sig));

    // Verification with wrong hash must fail
    uint256 wrong_hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>("wrong"), 5).Finalize(wrong_hash.data());
    BOOST_CHECK(!pubkey.Verify(wrong_hash, sig));

    // Verification with truncated sig must fail
    std::vector<unsigned char> bad_sig(sig.begin(), sig.begin() + sig.size() / 2);
    BOOST_CHECK(!pubkey.Verify(hash, bad_sig));

    // Verification with empty sig must fail
    std::vector<unsigned char> empty_sig;
    BOOST_CHECK(!pubkey.Verify(hash, empty_sig));
}

BOOST_AUTO_TEST_CASE(pqkey_sign_message)
{
    CPQKey key;
    CPQPubKey pubkey = key.MakeNewKey();

    // Sign a variable-length message
    std::string msg_str = "NEX post-quantum test message for ML-DSA-65";
    std::vector<unsigned char> msg(msg_str.begin(), msg_str.end());
    std::vector<unsigned char> sig;
    BOOST_CHECK(key.SignMessage(msg, sig));

    // Verify
    BOOST_CHECK(pubkey.VerifyMessage(msg, sig));

    // Tampered message must fail
    msg.push_back(0x00);
    BOOST_CHECK(!pubkey.VerifyMessage(msg, sig));
}

BOOST_AUTO_TEST_CASE(pqkey_verify_pubkey)
{
    CPQKey key;
    CPQPubKey pubkey = key.MakeNewKey();

    // Key should verify against its own pubkey
    BOOST_CHECK(key.VerifyPubKey(pubkey));

    // Should NOT verify against a different pubkey
    CPQKey key2;
    CPQPubKey pubkey2 = key2.MakeNewKey();
    BOOST_CHECK(!key.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey));
}

BOOST_AUTO_TEST_CASE(pqkey_deterministic_pubkey_hash)
{
    // Two keypairs must produce different IDs
    CPQKey key1, key2;
    CPQPubKey pub1 = key1.MakeNewKey();
    CPQPubKey pub2 = key2.MakeNewKey();

    CPQKeyID id1 = pub1.GetID();
    CPQKeyID id2 = pub2.GetID();

    BOOST_CHECK(id1 != id2);

    // Same pubkey must produce same ID
    CPQKeyID id1_again = pub1.GetID();
    BOOST_CHECK(id1 == id1_again);
}

BOOST_AUTO_TEST_CASE(pqkey_invalid_default)
{
    // Default-constructed keys are invalid
    CPQKey key;
    BOOST_CHECK(!key.IsValid());
    BOOST_CHECK_EQUAL(key.size(), 0u);

    CPQPubKey pubkey;
    BOOST_CHECK(!pubkey.IsValid());
    BOOST_CHECK_EQUAL(pubkey.size(), 0u);

    // Signing with invalid key must fail
    uint256 hash;
    std::vector<unsigned char> sig;
    BOOST_CHECK(!key.Sign(hash, sig));
    BOOST_CHECK(sig.empty());

    // Verifying with invalid pubkey must fail
    BOOST_CHECK(!pubkey.Verify(hash, sig));
}

BOOST_AUTO_TEST_CASE(pqkey_pubkey_serialization)
{
    CPQKey key;
    CPQPubKey pubkey = key.MakeNewKey();

    // Construct a new pubkey from the raw bytes
    CPQPubKey pubkey2(pubkey.begin(), pubkey.end());
    BOOST_CHECK(pubkey2.IsValid());
    BOOST_CHECK(pubkey == pubkey2);
    BOOST_CHECK(pubkey.GetID() == pubkey2.GetID());

    // Wrong-sized bytes must produce invalid pubkey
    std::vector<unsigned char> bad(100, 0x42);
    CPQPubKey pubkey3(bad.data(), bad.data() + bad.size());
    BOOST_CHECK(!pubkey3.IsValid());
}

BOOST_AUTO_TEST_CASE(pqkey_copy_move)
{
    CPQKey key1;
    CPQPubKey pub1 = key1.MakeNewKey();

    // Copy
    CPQKey key2 = key1;
    BOOST_CHECK(key2.IsValid());
    BOOST_CHECK(key1 == key2);

    // Move
    CPQKey key3 = std::move(key2);
    BOOST_CHECK(key3.IsValid());
    BOOST_CHECK(key1 == key3);
}

BOOST_AUTO_TEST_CASE(pqkey_multiple_signatures)
{
    CPQKey key;
    CPQPubKey pubkey = key.MakeNewKey();

    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>("multi"), 5).Finalize(hash.data());

    std::vector<unsigned char> sig1, sig2;
    BOOST_CHECK(key.Sign(hash, sig1));
    BOOST_CHECK(key.Sign(hash, sig2));

    // Both must verify
    BOOST_CHECK(pubkey.Verify(hash, sig1));
    BOOST_CHECK(pubkey.Verify(hash, sig2));
}

BOOST_AUTO_TEST_CASE(pq_address_witness_v2)
{
    CPQKey key;
    CPQPubKey pubkey = key.MakeNewKey();
    CPQKeyID id = pubkey.GetID();

    // Create a WitnessV2PQ destination
    WitnessV2PQ dest(pubkey);

    // Generate scriptPubKey: OP_2 <32-byte-hash>
    CTxDestination txdest{dest};
    CScript script = GetScriptForDestination(txdest);
    BOOST_CHECK_EQUAL(script.size(), 34u); // OP_2 (1) + push32 (1) + hash (32)
    BOOST_CHECK_EQUAL(script[0], 0x52); // OP_2
    BOOST_CHECK_EQUAL(script[1], 0x20); // push 32 bytes

    // Verify destination is valid
    BOOST_CHECK(IsValidDestination(txdest));
}

BOOST_AUTO_TEST_CASE(pq_cross_key_verification_fails)
{
    CPQKey keyA, keyB;
    CPQPubKey pubA = keyA.MakeNewKey();
    CPQPubKey pubB = keyB.MakeNewKey();

    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>("cross"), 5).Finalize(hash.data());

    std::vector<unsigned char> sigA;
    BOOST_CHECK(keyA.Sign(hash, sigA));

    BOOST_CHECK(pubA.Verify(hash, sigA));
    BOOST_CHECK(!pubB.Verify(hash, sigA));
}

BOOST_AUTO_TEST_SUITE_END()
