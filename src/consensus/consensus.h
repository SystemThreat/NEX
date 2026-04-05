// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <cstdint>
#include <cstdlib>

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits).
 *  Increased from Bitcoin's 4MB to 8MB to accommodate PQ transactions.
 *  A single PQ transaction (1-in-1-out) is ~5.4 KB serialized. */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 8000000;
/** The maximum allowed weight for a block, see BIP 141 (network rule).
 *  Kept at 4M WU. A PQ tx weighs ~5,650 WU → ~707 txns/block at 5-min intervals.
 *  NEX is a settlement layer; UMX on CORE handles high-volume payments. */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed number of signature check operations in a block (network rule).
 *  ML-DSA-65 verification is ~1.5x slower than secp256k1 ECDSA, so each PQ sigcheck
 *  costs 2 sigops. With ~707 single-sig PQ txns/block: 707 * 2 = 1,414 sigops.
 *  80,000 limit is more than sufficient. */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;
/** Sigops cost for a single ML-DSA-65 signature verification. */
static const int64_t PQ_SIGOPS_COST = 2;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

static const int WITNESS_SCALE_FACTOR = 4;

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid serialized CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for the size of a serialized CTransaction

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);

/**
 * Maximum number of seconds that the timestamp of the first
 * block of a difficulty adjustment period is allowed to
 * be earlier than the last block of the previous period (BIP94).
 */
static constexpr int64_t MAX_TIMEWARP = 600;

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
