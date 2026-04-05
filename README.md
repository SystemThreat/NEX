# NEX

## A peer-to-peer electronic cash system with a fast transaction rail

NEX is a Bitcoin-derived base chain paired with **Lumero**, a DAG-based instant payment layer.

**NEX** holds value — finite supply, transparent issuance, auditable rules.
**UMX** is wrapped NEX in motion — instant transfers, merchant settlement, mobile-native payments.
**KnexPay** orchestrates both — one wallet, dual balances, reserve-first design.

---

## Monetary Hierarchy

```
NEX  ₿  — reserve, settlement, gravity
 |
 |  1 NEX = 1,000 UMX (fixed ratio)
 |
UMX  Ł  — spend, payments, velocity
```

**NEX is gravity. UMX is NEX in motion.**

Value rests in NEX, moves through UMX (wrapped NEX on the fast layer), and returns to NEX. This is not two competing currencies. It is one monetary system with two functional states. Every UMX in existence has a NEX locked behind it.

| Role | NEX | UMX |
|---|---|---|
| Identity | Reserve asset | **Wrapped NEX on Lumero/CORE** |
| Purpose | Reserve, settlement, savings | Payments, spend, merchant liquidity |
| Layer | NEX base chain (PoW) | Lumero / CORE (DAG) |
| Speed | 5-minute blocks | ~200ms finality |
| Symbol | ₿ | Ł (Lumero) |
| Display | 8 decimal places | Up to 3 decimals (8 internal) |
| Bias | Long-term hold | Short-term spend |
| Issuance | PoW mining + premine | **Bridge-only — locked NEX backs every UMX** |

---

## Monetary Policy

### Supply

| Parameter | Value |
|---|---|
| **Total supply** | 100,000,000 NEX |
| **Premine** | 20,000,000 NEX (20%) — delivered via block 1 coinbase bonus |
| **Claim pool cap** | 5,000,000 NEX (5%) — Bitcoin UTXO snapshot claims |
| **Mineable** | 75,000,000 NEX (75%) |
| **Block time** | 5 minutes (300 seconds) |
| **Starting block reward** | 100 NEX (plus 20M premine on block 1 only) |
| **Halving interval** | 375,000 blocks (~3.57 years) |
| **Number of eras** | 8 (6 halvings + 1 tail era at era 8) |
| **Mining ends** | Block 3,000,000 (~November 2054) |
| **Signature scheme** | ML-DSA-65 (FIPS 204, post-quantum) |
| **Mining algorithm** | SHA-256d (Bitcoin-compatible hardware) |
| **Difficulty retarget** | Every 50 blocks (~4.2 hours at 5-min block target) |

### Conversion Ratio

**1 NEX = 1,000 UMX** (fixed, published)

- 1 UMX = 0.001 NEX
- At 8-decimal internal precision: 1 UMX = 100,000 raw NEX units
- Retail prices denominated in UMX (human-friendly sizing)
- Reserve balances denominated in NEX

### Emission Schedule

**Starts at 100 NEX/block. Halves every 375,000 blocks. 8 eras. Ends at block 3,000,000.**

5-minute blocks · 375,000 blocks per era (~3.57 years) · halving each era (era 8 is a flat tail)

| Era | Start | Block range | Reward/Block | Era Mined | Cumulative |
|---|---|---|---|---|---|
| 1 | 2026-04-05 | 0 – 374,999 | 100 NEX | 37,500,000 | 37,500,000 |
| 2 | ~2029-11 | 375,000 – 749,999 | 50 NEX | 18,750,000 | 56,250,000 |
| 3 | ~2033-05 | 750,000 – 1,124,999 | 25 NEX | 9,375,000 | 65,625,000 |
| 4 | ~2036-12 | 1,125,000 – 1,499,999 | 12.5 NEX | 4,687,500 | 70,312,500 |
| 5 | ~2040-07 | 1,500,000 – 1,874,999 | 6.25 NEX | 2,343,750 | 72,656,250 |
| 6 | ~2044-02 | 1,875,000 – 2,249,999 | 3.125 NEX | 1,171,875 | 73,828,125 |
| 7 | ~2047-09 | 2,250,000 – 2,624,999 | 1.5625 NEX | 585,937.5 | 74,414,062.5 |
| 8 | ~2051-04 | 2,625,000 – 2,999,999 | 1.5625 NEX | 585,937.5 | 75,000,000 |

Mining ends at block 3,000,000 (approximately November 2054).

### Halving Calendar

```
Era 1→2:  ~2029-11   100 → 50    NEX/block
Era 2→3:  ~2033-05    50 → 25
Era 3→4:  ~2036-12    25 → 12.5
Era 4→5:  ~2040-07   12.5 → 6.25
Era 5→6:  ~2044-02   6.25 → 3.125
Era 6→7:  ~2047-09   3.125 → 1.5625
Era 7→8:  ~2051-04   1.5625 → 1.5625  (no halve — same rate, tail)
End:      ~2054-11   1.5625 → 0 (mining ends at block 3,000,000)
```

6 halvings + 1 tail era. 83% of mining distributed by 2033.

### Supply identity

```
75,000,000 NEX  (total era subsidies)
+ 20,000,000 NEX  (premine, delivered via block 1 coinbase bonus)
+  5,000,000 NEX  (claim pool cap)
─────────────────
100,000,000 NEX  maximum supply
```

---

## Premine Allocation

**20,000,000 NEX** is premined to build new-world infrastructure — post-quantum custody, point-of-sale rails, merchant integrations, developer ecosystem, and the bridge reserve that backs the UMX payment layer. It is not speculation capital. It is operating capital for the system.

The 20M is delivered via block 1's coinbase bonus (not in the genesis block — Bitcoin-style genesis coinbases are unspendable). Block 1 is mined to a single bootstrap address, from which the founder distributes to 9 genesis accounts transparently.

*Placeholder distribution — finalized addresses published prior to mainnet launch:*

| # | Allocation | Amount | Share | Purpose |
|---|---|---|---|---|
| 1 | Founder / Lead Developer | 4,000,000 NEX | 20.0% | 4-year linear vest, 1-year cliff |
| 2 | Core Contributors | 3,000,000 NEX | 15.0% | Early engineers, reviewers, protocol designers |
| 3 | Protocol Treasury | 2,500,000 NEX | 12.5% | Governance-locked, multi-sig controlled |
| 4 | AI Development Partners | 2,000,000 NEX | 10.0% | Anthropic, OpenAI infrastructure contributions |
| 5 | Ecosystem Fund | 2,000,000 NEX | 10.0% | Grants, liquidity, partnerships |
| 6 | Marketing & BD | 2,000,000 NEX | 10.0% | Exchanges, launch campaigns, content |
| 7 | Bridge Reserve | 2,000,000 NEX | 10.0% | NEX-UMX collateral float |
| 8 | Security & Audits | 1,500,000 NEX | 7.5% | External audits, bug bounties, incident insurance |
| 9 | Community Airdrop | 1,000,000 NEX | 5.0% | Early supporters, claim pool seed |
| | **Total** | **20,000,000 NEX** | **100%** | |

**Separately**, 5,000,000 NEX is reserved as a **claim pool cap** — not part of the 20M premine. Claims require a valid Bitcoin-UTXO-snapshot merkle proof (consensus parameter `FINAL_CLAIM_MERKLE_ROOT`, activated after the snapshot is finalized).

Supply identity:
```
20,000,000  premine (9 accounts via block 1 coinbase)
+  5,000,000  claim pool cap
+ 75,000,000  mining emission (8 eras)
─────────────
100,000,000  NEX maximum supply
```

All 9 premine addresses, their post-quantum public keys, and vesting terms will be published on-chain in the genesis-block commitment OP_RETURN prior to launch.

---

## Reserve Invariant

The bridge maintains a public, non-negotiable monetary invariant:

> **Locked NEX on the reserve side must equal issued UMX value on the transaction side, at the 1:1000 conversion ratio.**

- Every 1 NEX locked in reserve authorizes issuance of 1,000 UMX on Lumero
- Every 1,000 UMX redeemed destroys that amount and releases 1 NEX back to reserve
- Reserve data is published: lock addresses, total bridged NEX, total issued UMX, all mint/burn events

### Non-Negotiable Backing Rule

UMX may only be created through one of these paths:
- **Lock of NEX into the bridge reserve** (production)
- **Explicit genesis/testnet issuance** (marked as non-production, auditable)

UMX must not be issued as an unrelated floating asset. No unbacked minting. No fractional reserve. The bridge is a 1:1 lock/mint mechanism, not a lending facility.

---

## Bridge Model

### NEX → UMX (Convert Into Velocity)

Fast, low-friction, encouraged. This wraps NEX into spendable form.

```
1. User chooses to move value from reserve into spend
2. NEX is locked on the NEX chain to a published bridge address
3. Bridge verifies required confirmations (6 blocks = 30 min)
4. 1,000 UMX per NEX is minted (wrapped) on Lumero (CORE)
5. User's KnexPay spend balance increases
```

### UMX → NEX (Return to Gravity)

Always available, more disciplined. This unwraps UMX back into NEX.

```
1. User chooses to move value from spend into reserve
2. UMX is burned (unwrapped) on Lumero (CORE)
3. Bridge verifies finality
4. Equivalent NEX released from reserve (UMX / 1000)
5. User's KnexPay reserve balance increases
```

### Bridge Controls

- Permissioned signer set with multi-sig threshold
- Delayed large withdrawals
- Reserve accounting published on both chains
- Audit logging for every mint/burn event
- UMX minting restricted to bridge contract only (no controller expansion)
- Evolves toward decentralization as network matures

---

## Wallet Policy (KnexPay)

KnexPay presents the monetary system as two coordinated balances:

```
┌─────────────────────────┐
│  Reserve Balance (NEX)  │ ← primary, gravity
│  ₿ 1,250.00            │
├─────────────────────────┤
│  Spend Balance (UMX)    │ ← secondary, velocity
│  Ł 150.000             │
└─────────────────────────┘
```

### Automatic Spend Float

Users define a UMX operating float:

| Control | Example | Behavior |
|---|---|---|
| Minimum spend threshold | 50 UMX | Top up from reserve when below |
| Target spend balance | 150 UMX | Auto-refill target |
| Maximum spend float | 300 UMX | Sweep excess back to NEX |

This makes UMX behave like a checking account fed by a savings account.

### Default Wallet Bias

- Portfolio summary emphasizes NEX reserve
- Net worth displayed in NEX
- UMX presented as available spending balance
- Sweep-to-reserve enabled by default
- Prices shown in UMX for daily use, NEX equivalent visible

---

## Merchant Policy

Merchants are the strongest enforcement of monetary gravity.

- Accept UMX instantly at point of sale
- Hold configurable UMX working float (e.g., 2,000 UMX)
- Excess automatically swept to NEX on schedule (hourly, 6h, daily)
- Treasury reports in NEX

**UMX = cash register. NEX = vault.**

---

## Chain Parameters

```
Network name:        NEX
Ticker:              NEX
Max supply:          100,000,000 NEX
Block time:          300 seconds (5 minutes)
Era length:          375,000 blocks
Eras:                8 (mining ends at block 3,000,000)
Era 1 reward:        100 NEX
Signature scheme:    ML-DSA-65 (FIPS 204)
Mining algorithm:    SHA-256d (double SHA-256)
Difficulty adjust:   50 blocks (~4.2 hours)
Default P2P port:    9333
Default RPC port:    9332
Address format:      bech32m, HRP "nex" (mainnet) / "tnx" (testnet)
                     mainnet addresses begin with "nex1z"
Network magic:       0x4e, 0x45, 0x58, 0x02 ("NEX" + v2 PQ era)
```

### Genesis block (mainnet)

```
Timestamp:    2026-04-05 00:00:00 UTC  (1775347200)
Bits:         0x1d00ffff  (difficulty 1)
Nonce:        3741185684
Hash:         00000000ca679662c87c40693490f00e297a2bd357e59cab6f7814d12145d66b
Merkle root:  2c62d38151df668e842eb913e9dd0b872a0f1d6dbf5998b6803e0be382e75f68
Coinbase:     0-value OP_2 (genesis coinbase is unspendable per Bitcoin Core design)
```

**Block 1** mints **20,000,100 NEX** (100 NEX era-1 subsidy + 20,000,000 NEX premine bonus) to the miner who finds it. This is how the 20M premine is distributed into the UTXO set.

### UMX Parameters (on Lumero/CORE)

```
Token ID:            1001
Symbol:              Ł (U+0141)
Ticker:              UMX
Ledger precision:    8 decimal places
Display precision:   3 decimal places (up to)
Conversion:          1 NEX = 1,000 UMX
Tax:                 0.50% (50 basis points) on UMX transactions
```

---

## Architecture

```
NEX (base chain — PoW settlement)
 │
 │  ← permissioned bridge (lock NEX / mint UMX, burn UMX / release NEX)
 │     1 NEX = 1,000 UMX (fixed ratio)
 │     reserve invariant enforced
 │
Lumero / CORE (DAG payment layer)
 │  ├── UMX token (ID 1001) — spend unit
 │  ├── KNEX native — validator staking, governance
 │  ├── NFC tap-to-pay (CMAC verified)
 │  └── WebSocket realtime events
 │
KnexPay (iOS wallet)
    ├── Reserve balance (NEX)
    ├── Spend balance (UMX)
    ├── Auto top-up / auto sweep
    └── Dual-balance display
```

### NEX — Settlement Layer

- Bitcoin-derived PoW chain
- 100M fixed supply, 8 eras (6 halvings + 1 tail), SHA-256d
- **ML-DSA-65 (FIPS 204)** post-quantum signatures — enforced at consensus
- Legacy Bitcoin script types (ECDSA/Schnorr) rejected at consensus
- Final ownership and reserve asset
- Conservative, auditable, simple

### Lumero — Payment Layer

- Block-lattice DAG (each account has its own chain)
- Post-quantum cryptography (ML-DSA-65 / FIPS 204)
- 2/3 BFT validator finalization (~100-300ms)
- Theoretical throughput: ~100,000 TPS
- NFC tap-to-pay with SUN/SDM CMAC verification
- UMX transaction tax: 0.50% (50 BPS)

### KnexPay — Consumer Wallet

- iOS-native with WKWebView hybrid architecture
- Wallet keys in iOS Keychain (biometric-protected)
- NFC card self-provisioning on iPhone
- Real-time balance updates via WebSocket
- Dual-balance UX (reserve NEX + spend UMX)
- Auto top-up and auto sweep between layers

---

## Security

### NEX (base chain)
- SHA-256d proof-of-work (same security model as Bitcoin)
- 50-block difficulty retarget (~4.2 hours)
- Full node verification
- **ML-DSA-65 (FIPS 204)** signatures on every transaction
- Consensus rule: every output must be `WITNESS_V2_PQ` or `NULL_DATA` (OP_RETURN) — legacy ECDSA/Schnorr script types are rejected

### Post-Quantum Enforcement

Every output on the NEX chain is one of two types:
1. `WITNESS_V2_PQ` — `OP_2 <32-byte pubkey hash>` spent via ML-DSA-65 signature
2. `NULL_DATA` — `OP_RETURN` data (witness commitment, anchors, no value)

Any attempt to create a P2PKH, P2WPKH, P2TR, P2SH, or bare multisig output is rejected at consensus. This is enforced in `src/consensus/tx_check.cpp`:

```cpp
for (const auto& txout : tx.vout) {
    TxoutType whichType = Solver(txout.scriptPubKey, vSolutions);
    if (whichType != TxoutType::WITNESS_V2_PQ && whichType != TxoutType::NULL_DATA) {
        return state.Invalid(..., "bad-txout-not-pq");
    }
}
```

Result: NEX is 100% post-quantum from genesis. No ECDSA-secured coins can exist on the chain.

### Lumero (payment layer)
- ML-DSA-65 post-quantum signatures on all transactions
- ML-DSA-65 post-quantum P2P message signing
- 2/3 supermajority block finalization
- Validator whitelist with signed configuration
- Atomic database writes (RocksDB WriteBatch)
- Mempool bounds (16 per account, 4096 total)
- IP-locked P2P network (fortress mode)
- HSM-compatible signing interface

### KnexPay (wallet)
- Native bridge locked to packaged app origin only
- AES-256-GCM encrypted storage
- Keychain-only secret key storage
- NFC card CMAC verification (server-side)
- Restricted native fetch (API host allowlist)

---

## Development Status

| Component | Status |
|---|---|
| NEX base chain | Genesis and chain params in progress |
| Lumero DAG ledger | Implemented — 2,899 tests passing |
| Fortress hardening | Complete — atomic writes, validator whitelist, 2/3 BFT |
| Post-quantum crypto | Complete — ML-DSA-65 on all signatures |
| KnexPay iOS wallet | Implemented — provisioning, tap-to-pay, realtime settle |
| NFC CMAC verification | Complete — end-to-end SDM key flow |
| NEX-UMX bridge | Design complete, implementation next |
| Monetary policy | Defined — wrapped NEX model, 1:1000 ratio, non-negotiable backing |

---

## Rollout

### Phase 1 — Foundation
- Launch NEX testnet (regtest)
- Launch CORE testnet
- Manual convert-in / convert-out
- Visible dual balances in KnexPay

### Phase 2 — Automation
- Auto top-up from NEX to UMX
- Auto sweep from UMX to NEX
- Merchant reserve settlement policies

### Phase 3 — Maturity
- Treasury dashboards
- Reserve proofs
- Public invariant reporting
- Optimized wallet defaults around reserve discipline

---

## Design Principles

1. **Simplicity at the base layer.** The monetary chain should be conservative. Complexity belongs at the edges.
2. **Speed where it matters.** Users should not wait an arbitrary time to make an ordinary payment.
3. **Self-custody first.** A system dependent on custodians recreates the old model under new branding.
4. **Verifiability over slogans.** Claims should be inspectable in code, supply, reserves, and settlement behavior.
5. **No confusion between money and transport.** NEX is the monetary base. UMX is the payment rail. The distinction stays clear.

---

## Repository Structure

```
src/                    # Bitcoin Core C++ source (NEX-modified)
  consensus/            # Consensus rules, validation
  kernel/               # Chain parameters, genesis
  crypto/               # SHA-256d, key derivation
  net/                  # P2P networking
  wallet/               # Wallet functionality
  rpc/                  # JSON-RPC interface
doc/                    # Documentation
test/                   # Functional and unit tests
```

---

## Contributing

Contributions should favor clarity over spectacle.

Useful contributions include:
- Chain parameter review
- Genesis block verification
- Wallet and node testing
- Security analysis
- Bridge architecture design
- Monetary policy review

---

## Contributors

- **David Otero** ([@SystemThreat](https://github.com/SystemThreat)) — Creator, lead architect
- **Claude** ([Anthropic](https://anthropic.com)) — Lumero protocol, fortress hardening, test suite, KnexPay upgrades
- **OpenAI** ([OpenAI](https://openai.com)) — Advisory contributions

---

## License

This project inherits from Bitcoin Core's open-source MIT license.

```
Copyright (c) 2009-2026 The Bitcoin Core developers
Copyright (c) 2026 NEX contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
```

---

## Final Monetary Rule

> **Value rests in NEX, moves through wrapped NEX as UMX, and returns to NEX.**

NEX is gravity. UMX is NEX in motion. KnexPay orchestrates both.
