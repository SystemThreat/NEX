# NEX

## A peer-to-peer electronic cash system with a fast transaction rail

NEX is a Bitcoin-derived base chain paired with **Lumero**, a DAG-based instant payment layer.

**NEX** carries the reserve asset — finite supply, transparent issuance, auditable rules.
**Lumero** carries the movement — instant transfers, merchant settlement, mobile-native payments.
**KnexPay** is the consumer wallet — one interface for both layers.

---

## Monetary Policy

| Parameter | Value |
|---|---|
| **Total supply** | 100,000,000 NEX |
| **Premine** | 22,000,000 NEX (22%) |
| **Mineable** | 78,000,000 NEX (78%) |
| **Block time** | 5 minutes |
| **Initial block reward** | 110 NEX |
| **Halving interval** | 360,000 blocks (~3.4 years) |
| **Number of halvings** | 7 |
| **Mining ends** | ~2050 |
| **Mining algorithm** | SHA-256d (Bitcoin-compatible hardware) |
| **Difficulty retarget** | Every 2,016 blocks (~7 days) |

### Emission Schedule

| Epoch | Years | Reward/Block | Mined | Cumulative | % of Mineable |
|---|---|---|---|---|---|
| 1 | 2026–2029 | 110 NEX | 39,600,000 | 39,600,000 | 50.8% |
| 2 | 2029–2033 | 55 NEX | 19,800,000 | 59,400,000 | 76.2% |
| 3 | 2033–2036 | 27.5 NEX | 9,900,000 | 69,300,000 | 88.8% |
| 4 | 2036–2040 | 13.75 NEX | 4,950,000 | 74,250,000 | 95.2% |
| 5 | 2040–2043 | 6.875 NEX | 2,475,000 | 76,725,000 | 98.4% |
| 6 | 2043–2047 | 3.4375 NEX | 1,237,500 | 77,962,500 | 99.9% |
| 7 | 2047–2050 | 1.71875 NEX | 618,750 | 78,581,250 | 100% |

### Halving Calendar

```
Halving 1:  ~2029    110 → 55 NEX/block
Halving 2:  ~2033    55 → 27.5
Halving 3:  ~2036    27.5 → 13.75
Halving 4:  ~2040    13.75 → 6.875
Halving 5:  ~2043    6.875 → 3.4375
Halving 6:  ~2047    3.4375 → 1.71875
Halving 7:  ~2050    1.71875 → 0 (mining ends)
```

76% of all mineable NEX is distributed by 2033. Early miners are rewarded. Scarcity accelerates with each halving.

---

## Premine Allocation

22,000,000 NEX allocated transparently at genesis:

| Allocation | Amount | Purpose |
|---|---|---|
| Protocol Treasury | 10,000,000 NEX | Governance-locked, multi-sig controlled |
| Ecosystem Fund | 4,000,000 NEX | Grants, liquidity, partnerships |
| Team | 3,000,000 NEX | 4-year linear vest, 1-year cliff |
| Bridge Reserve | 2,000,000 NEX | NEX-Lumero bridge collateral |
| Mining Incentive | 1,500,000 NEX | Early miner bonuses, testnet rewards |
| Security Fund | 1,000,000 NEX | Audits, bug bounties, insurance |
| Community Airdrop | 500,000 NEX | Early adopters and contributors |

All genesis addresses published. All vesting schedules enforced on-chain. No hidden issuance.

---

## Chain Parameters

```
Network name:        NEX
Ticker:              NEX
Max supply:          100,000,000
Block time:          300 seconds (5 minutes)
Halving interval:    360,000 blocks
Initial reward:      110 NEX
Mining algorithm:    SHA-256d (double SHA-256)
Difficulty adjust:   2,016 blocks (~7 days)
Default P2P port:    9333
Default RPC port:    9332
Address prefix:      N (mainnet), n (testnet)
Network magic:       0x4e, 0x45, 0x58, 0x01 ("NEX" + version)
```

### Why SHA-256d

Existing Bitcoin mining hardware works on NEX from day one. No new ASICs needed. Miners can point hashrate at whichever chain is more profitable. This gives NEX immediate security from the global SHA-256 mining ecosystem.

### Why 5-minute blocks

NEX is a settlement chain, not a speed layer — that's Lumero's job. But 5-minute blocks are fast enough for on-chain confirmation while maintaining security margins for proof-of-work.

---

## Architecture

```
NEX (base chain)
 |
 |  ← permissioned bridge (lock/mint, burn/release)
 |
Lumero (DAG payment layer)
 |
 |  ← WebSocket events, instant settlement
 |
KnexPay (iOS wallet)
```

### NEX — Settlement Layer

- Bitcoin-derived PoW chain
- 100M fixed supply
- Final ownership and reserve asset
- Conservative, auditable, simple

### Lumero — Payment Layer

- Block-lattice DAG (each account has its own chain)
- Post-quantum cryptography (ML-DSA-65 / FIPS 204)
- 2/3 BFT validator finalization (~100-300ms)
- Theoretical throughput: ~100,000 TPS
- NFC tap-to-pay with SUN/SDM CMAC verification
- Transaction tax: 0.07% (7 basis points)

### KnexPay — Consumer Wallet

- iOS-native with WKWebView hybrid architecture
- Wallet keys in iOS Keychain (biometric-protected)
- NFC card self-provisioning on iPhone
- Real-time balance updates via WebSocket
- FoLR (Fold, Lock, Recover) key backup to NTAG 424 DNA chips

---

## Bridge Model

The NEX-Lumero bridge is **permissioned first** — operated by a known signer set with multi-sig controls.

**Deposit flow:** Lock NEX on base chain → Bridge observes → Mint equivalent Lumero value
**Withdrawal flow:** Burn Lumero → Bridge observes → Release NEX from lock

Controls:
- Multi-sig threshold signing
- Delayed large withdrawals
- Reserve accounting published on-chain
- Audit logging

The bridge trust model is documented separately and will evolve toward decentralization as the network matures.

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
| Bridge architecture | Design phase |

---

## Why Fork Bitcoin

Bitcoin solved money issuance. A fork acknowledges that breakthrough while pursuing a different path.

NEX uses that freedom to:
- Define a new monetary policy (100M supply, 7 halvings, 2050 end)
- Pair a PoW settlement chain with a quantum-safe payment layer
- Ship a real consumer wallet with NFC tap-to-pay
- Build a system designed around usable finality, not just scarcity

A fork is not a rejection. It is a continuation.

---

## Design Principles

1. **Simplicity at the base layer.** The monetary chain should be conservative. Complexity belongs at the edges.
2. **Speed where it matters.** Users should not wait an arbitrary time to make an ordinary payment.
3. **Self-custody first.** A system dependent on custodians recreates the old model under new branding.
4. **Verifiability over slogans.** Claims should be inspectable in code, supply, reserves, and settlement behavior.
5. **No confusion between money and transport.** NEX is the monetary base. Lumero is the payment rail. The distinction stays clear.

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
  qt/                   # Qt GUI (optional)
doc/                    # Documentation
test/                   # Functional and unit tests
contrib/                # Build helpers, packaging
```

---

## Contributing

Contributions should favor clarity over spectacle.

Useful contributions include:
- Chain parameter review
- Genesis block verification
- Wallet and node testing
- Security analysis
- Documentation
- Bridge architecture design

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

## Closing

A monetary system should not only be scarce. It should also be usable.

NEX exists to continue that work.
