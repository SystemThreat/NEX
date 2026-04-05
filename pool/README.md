# NEX Solo Mining Pool

A single-file asyncio stratum v1 pool for the NEX blockchain. No external dependencies beyond Python 3.8+.

Pays **100% of every block reward** to the configured payout address (no pool fee). Designed for solo mining — every valid block found goes directly to the miner who found it.

## Features

- **Stratum v1** with `mining.configure` version-rolling (BIP310, ASICBoost)
- **Vardiff** — per-miner adaptive difficulty (floor 0.001, ceiling 1M, targets 1 share per 10s)
- **PQ-aware coinbase** — emits `OP_2 <32-byte pubkey hash>` outputs to satisfy NEX's `WITNESS_V2_PQ` consensus rule
- **Fresh work rotation** — broadcasts updated `mining.notify` every 30s even when tip unchanged (prevents miners from grinding stale work)
- **HTTP stats endpoint** on port 8081 — JSON per-miner stats for explorers/dashboards
- **Zombie connection detection** — drops idle sessions after 3 minutes
- **Job history cache** — accepts slightly-stale submits during tip-change races
- **BIP310 share validation** — version-rolled shares fully validated against block target
- **Custom `mining.block_found` notification** — tells the miner which share was a real block

## Quick start

```bash
# Configure payout address (edit CONFIG at top of file)
vim nex_pool.py
#   payout_pubkey_hash = bytes.fromhex('<32-byte hex from your nex1z address>')
#   rpc_password       = '<your nexd rpcpassword>'

# Run
python3 nex_pool.py
```

Pool will:
- Connect to nexd on `127.0.0.1:9332` (RPC)
- Listen for stratum connections on `0.0.0.0:3333`
- Serve stats JSON on `0.0.0.0:8081`

## Point a miner at it

```bash
# Bitaxe / cpuminer / NMMiner / MMM / any SHA-256d stratum miner
-o stratum+tcp://<pool-host>:3333 -u nex1z<your-address> -p x
```

## Stats endpoint

```bash
curl http://<pool-host>:8081/stats
```

Returns JSON with: `miner_count`, `tip_height`, `total_hashrate`, `blocks_found_session`, and per-miner detail (`user_agent`, `difficulty`, `hashrate`, `shares_accepted`, `blocks_found`, `last_share_seconds_ago`, etc).

## Config reference

| Key | Default | Purpose |
|---|---|---|
| `stratum_port` | 3333 | Miner connection port |
| `stats_port` | 8081 | HTTP stats endpoint |
| `rpc_host` | 127.0.0.1 | nexd RPC host |
| `rpc_port` | 9332 | nexd RPC port |
| `payout_pubkey_hash` | (32 bytes) | Where block rewards go — OP_2 script target |
| `initial_difficulty` | 16 | Starting pool diff (vardiff adjusts from here) |
| `vardiff_target_seconds` | 10 | Aim for 1 share per N seconds |
| `vardiff_min` | 0.001 | Floor diff (for slow CPU miners) |
| `vardiff_max` | 1,000,000 | Ceiling diff (for ASICs) |
| `vardiff_no_share_timeout` | 45 | Drop diff 4x if no shares this long |
| `job_refresh_sec` | 30 | How often to push fresh `mining.notify` |

## License

MIT — matching the NEX chain itself.
