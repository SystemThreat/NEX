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
| **Premine** | 33,000,000 NEX (33%) — 20M Bitcoin 1:1 snapshot + 13M team/treasury |
| **Mineable** | 67,000,000 NEX (67%) |
| **Block time** | 5 minutes |
| **Initial block reward** | 95 NEX |
| **Halving interval** | 360,000 blocks (~3.4 years) |
| **Number of halvings** | 7 |
| **Mining ends** | ~2050 |
| **Mining algorithm** | SHA-256d (Bitcoin-compatible hardware) |
| **Difficulty retarget** | Every 2,016 blocks (~7 days) |

### Conversion Ratio

**1 NEX = 1,000 UMX** (fixed, published)

- 1 UMX = 0.001 NEX
- At 8-decimal internal precision: 1 UMX = 100,000 raw NEX units
- Retail prices denominated in UMX (human-friendly sizing)
- Reserve balances denominated in NEX

### Emission Schedule

| Epoch | Years | Reward/Block | Mined | Cumulative | % of Mineable |
|---|---|---|---|---|---|
| 1 | 2026–2029 | 95 NEX | 34,200,000 | 34,200,000 | 51.0% |
| 2 | 2029–2033 | 47.5 NEX | 17,100,000 | 51,300,000 | 76.6% |
| 3 | 2033–2036 | 23.75 NEX | 8,550,000 | 59,850,000 | 89.3% |
| 4 | 2036–2040 | 11.875 NEX | 4,275,000 | 64,125,000 | 95.7% |
| 5 | 2040–2043 | 5.9375 NEX | 2,137,500 | 66,262,500 | 98.9% |
| 6 | 2043–2047 | 2.96875 NEX | 1,068,750 | 67,331,250 | 99.5% |
| 7 | 2047–2050 | 1.484375 NEX | 534,375 | 67,865,625 | 100% |

### Halving Calendar

```
Halving 1:  ~2029    95 → 47.5 NEX/block
Halving 2:  ~2033    47.5 → 23.75
Halving 3:  ~2036    23.75 → 11.875
Halving 4:  ~2040    11.875 → 5.9375
Halving 5:  ~2043    5.9375 → 2.96875
Halving 6:  ~2047    2.96875 → 1.484375
Halving 7:  ~2050    1.484375 → 0 (mining ends)
```

7 halvings. 7 supercycles. 77% distributed by 2033.

---

## Premine Allocation

33,000,000 NEX allocated transparently at genesis:

| Allocation | Amount | Purpose |
|---|---|---|
**Bitcoin UTXO Snapshot — 20,000,000 NEX**

Every BTC holder at fork height receives 1 NEX per 1 BTC. Claimable with existing Bitcoin private key + new post-quantum address. No action required until ready to claim.

**Team + Treasury — 13,000,000 NEX**

| Allocation | Amount | Purpose |
|---|---|---|
| Founder / Core Team | 4,000,000 NEX | 4-year linear vest, 1-year cliff |
| AI Development Partners | 3,000,000 NEX | Claude (Anthropic), OpenAI contributions |
| Protocol Treasury | 3,000,000 NEX | Governance-locked, multi-sig controlled |
| Bridge Reserve | 1,500,000 NEX | NEX-UMX bridge collateral |
| Ecosystem Fund | 1,000,000 NEX | Grants, liquidity, partnerships |
| Security Fund | 500,000 NEX | Audits, bug bounties, insurance |

All genesis addresses published. All vesting schedules enforced on-chain.

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
Max supply:          100,000,000
Block time:          300 seconds (5 minutes)
Halving interval:    360,000 blocks
Initial reward:      95 NEX
Mining algorithm:    SHA-256d (double SHA-256)
Difficulty adjust:   2,016 blocks (~7 days)
Default P2P port:    9333
Default RPC port:    9332
Address prefix:      N (mainnet), n (testnet)
Network magic:       0x4e, 0x45, 0x58, 0x01 ("NEX" + version)
```

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
- 100M fixed supply, 7 halvings, SHA-256d
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
- 7-day difficulty retarget
- Full node verification

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
