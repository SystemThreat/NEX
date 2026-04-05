# NEX — Post-Quantum Bitcoin Fork

Full node (`nexd`) and tools for the NEX blockchain. Bitcoin Core fork with consensus-enforced ML-DSA-65 (FIPS 204) signatures. Every output on the chain is post-quantum by rule.

## What's different from Bitcoin

| | Bitcoin | NEX |
|---|---|---|
| Signature scheme | ECDSA / Schnorr (secp256k1) | **ML-DSA-65** (FIPS 204) |
| Address format | bech32m P2WPKH / P2TR | bech32m `nex1z` (witness v2) |
| Output script | `OP_0 <20-byte hash>` (P2WPKH) | `OP_2 <32-byte hash>` (WITNESS_V2_PQ) |
| Block time | 10 minutes | **5 minutes** |
| Retarget interval | 2016 blocks | **50 blocks** |
| Block subsidy | 50 → halving every 210K blocks | **100 → 8 eras of 375K blocks** |
| Halvings | infinite | **8 eras, ends at block 3,000,000** |
| Legacy scripts (ECDSA) | allowed | **rejected at consensus** |
| Max supply | 21M BTC | **100M NEX** |

## Emission schedule

| Era | Block range | Subsidy | Era total |
|---|---|---|---|
| 1 | 0 – 374,999 | 100 NEX | 37,500,000 NEX |
| 2 | 375,000 – 749,999 | 50 NEX | 18,750,000 NEX |
| 3 | 750,000 – 1,124,999 | 25 NEX | 9,375,000 NEX |
| 4 | 1,125,000 – 1,499,999 | 12.5 NEX | 4,687,500 NEX |
| 5 | 1,500,000 – 1,874,999 | 6.25 NEX | 2,343,750 NEX |
| 6 | 1,875,000 – 2,249,999 | 3.125 NEX | 1,171,875 NEX |
| 7 | 2,250,000 – 2,624,999 | 1.5625 NEX | 585,937.5 NEX |
| 8 | 2,625,000 – 2,999,999 | 1.5625 NEX | 585,937.5 NEX |

Plus a **20M NEX premine** delivered via block 1's coinbase subsidy bonus, and 5M reserved for claims. Total: 100,000,000 NEX.

## Build

Requires CMake, Boost, libevent, OpenSSL, Python 3.

```bash
git clone https://github.com/SystemThreat/NEX.git nex-core
cd nex-core
cmake -B build -DBUILD_GUI=OFF -DBUILD_TESTING=OFF
cmake --build build -j$(nproc) --target bitcoind
```

Binary will be at `build/bin/nexd`.

## Run a node

```bash
mkdir -p ~/.nex
cat > ~/.nex/nex.conf <<EOF
server=1
rpcuser=nex
rpcpassword=$(openssl rand -hex 16)
rpcallowip=127.0.0.1
rpcbind=127.0.0.1
listen=1
port=9333
rpcport=9332
dbcache=4096
txindex=1
EOF

./build/bin/nexd -datadir=~/.nex -daemon
```

Check sync:
```bash
./build/bin/nex-cli -datadir=~/.nex getblockchaininfo
```

## Mining

NEX uses SHA-256d PoW (same as Bitcoin), so any Bitcoin-compatible miner works. Point a Stratum v1 miner at a NEX pool with your `nex1z...` address as the worker name.

Example with cpuminer:
```bash
./minerd -o stratum+tcp://<pool-ip>:3333 \
         -u nex1z... \
         -p x -a sha256d
```

## Consensus rule: PQ-only outputs

Every transaction must have outputs of type `WITNESS_V2_PQ` or `NULL_DATA` (OP_RETURN). Legacy Bitcoin script types are rejected at consensus:

```cpp
// src/consensus/tx_check.cpp
for (const auto& txout : tx.vout) {
    TxoutType whichType = Solver(txout.scriptPubKey, vSolutions);
    if (whichType != TxoutType::WITNESS_V2_PQ && whichType != TxoutType::NULL_DATA) {
        return state.Invalid(..., "bad-txout-not-pq");
    }
}
```

## Genesis (mainnet)

- **Timestamp**: 2026-04-05 00:00:00 UTC (`1775347200`)
- **Hash**: `00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b`
- **Merkle root**: `2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68`
- **Nonce**: `3741185684`
- **Bits**: `0x1d00ffff` (difficulty 1)
- **Genesis coinbase**: 0-value OP_2 (no burn, no spendable output)
- **Block 1 subsidy**: 20,000,100 NEX (20M premine + 100 NEX era-1)

## License

MIT — inherited from Bitcoin Core.
