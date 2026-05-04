#!/usr/bin/env python3
"""
NEX Solo Mining Pool - Stratum v1 for Bitaxe
Pays 100% of block rewards to configured address (no pool fee).
"""
import asyncio
import json
import hashlib
import os
import struct
import time
import urllib.request
import urllib.error
import base64
import logging
import sys
from collections import deque

# ═══════════════════════════════════════════════════════════════════════════
# Per-instance secrets + identity (supplied via env, never committed)
# ═══════════════════════════════════════════════════════════════════════════
_RPC_PASSWORD = os.environ.get('NEX_RPC_PASSWORD')
if not _RPC_PASSWORD:
    sys.exit("FATAL: NEX_RPC_PASSWORD env var is required")
_POOL_TAG = os.environ.get('NEX_POOL_TAG', '/nex/').encode()
# Optional fallback payout hash, used ONLY if a miner connects but never
# sends mining.authorize with their own NEX address. Real Stratum miners
# always authorize before requesting a job, so this path is dead in normal
# operation — the per-session payout always comes from the miner's login,
# not from here. Leaving NEX_PAYOUT_PUBKEY_HASH unset is the safe default
# for a public pool: a misconfigured miner gets a clean error instead of
# accidentally crediting blocks to the operator. Set it only if you
# explicitly want to catch unauthenticated miners with your own address.
_PAYOUT_HASH_HEX = os.environ.get('NEX_PAYOUT_PUBKEY_HASH', '').strip()
if _PAYOUT_HASH_HEX:
    try:
        _PAYOUT_HASH = bytes.fromhex(_PAYOUT_HASH_HEX)
        assert len(_PAYOUT_HASH) == 32, "must decode to 32 bytes"
    except Exception as _e:
        sys.exit(f"FATAL: NEX_PAYOUT_PUBKEY_HASH invalid ({_e})")
else:
    _PAYOUT_HASH = None  # miners MUST authorize with their own address

# ═══════════════════════════════════════════════════════════════════════════
# CONFIG
# ═══════════════════════════════════════════════════════════════════════════
CONFIG = {
    'stratum_host': '0.0.0.0',
    'pplns_port': 3333,              # PPLNS mode — connect here for shared rewards
    'solo_port':  7777,              # SOLO mode — connect here to keep 100% of any block you find
    # Legacy config name still read below if ever needed; new deployments should use the two above.
    'rpc_host': '127.0.0.1',
    'rpc_port': 9332,
    'rpc_user': 'nex',
    'rpc_password': _RPC_PASSWORD,
    # Payout pubkey hash — sourced from NEX_PAYOUT_PUBKEY_HASH env var.
    # Each pool operator sets their own; no address is committed to source.
    'payout_pubkey_hash': _PAYOUT_HASH,
    'job_refresh_sec': 30,
    'extranonce1_size': 4,
    'extranonce2_size': 4,
    # Vardiff — per-miner adaptive difficulty
    # Each session has its OWN difficulty; ASIC sessions and CPU sessions coexist.
    # Converges via direct hashrate estimation (not 2x ping-pong) — typically in ~15s.
    'initial_difficulty': 16,        # fallback when user-agent gives no hint
    'vardiff_target_seconds': 10,    # aim for 1 share every ~10 seconds per miner
    'vardiff_min': 0.001,            # floor for CPU miners (MMM on low-end Macs)
    'vardiff_max': 10_000_000,       # ceiling for high-end ASICs (S21 Pro class)
    'vardiff_window': 3,             # shares in trailing average (small → react fast)
    'vardiff_retune_seconds': 5,     # min interval between diff changes
    'vardiff_max_step_up': 4.0,      # clamp per-retune multiplier (prevents overshoot)
    'vardiff_max_step_down': 0.25,   # clamp per-retune multiplier
    'vardiff_deadband': 0.25,        # skip retune when ratio is within ±25% of target
    'vardiff_no_share_timeout': 45,  # reserved — sweeper task not yet implemented
    # User-agent → starting difficulty heuristic. Substring match, case-insensitive,
    # first match wins. Picks a diff where the very first share lands within target_seconds,
    # so vardiff has signal to work with immediately. Falls back to initial_difficulty.
    'ua_starting_diff': [
        ('macmetalminer', 0.1),      # MMM Mac Metal Miner (~50 MH/s – 1 GH/s depending on model)
        ('cpuminer',      0.001),
        ('minerd',        0.001),
        ('xmrig',         0.001),
        ('bfgminer',      50),
        ('cgminer',       200),
        ('bitaxe',        1000),     # Bitaxe Gamma ~1 TH/s
        ('s9',            30_000),   # Antminer S9 ~13 TH/s
        ('s17',           150_000),  # S17 ~60 TH/s
        ('s19',           300_000),  # S19 ~100 TH/s
        ('s21',           500_000),  # S21 ~200 TH/s
        ('avalon',        100_000),
        ('whatsminer',    300_000),
        ('antminer',      100_000),  # generic fallback for Antminer-ish UAs
    ],
    'stats_port': 8081,              # internal HTTP endpoint for explorer to fetch stats
    # PPLNS — pay-per-last-N-shares reward model. Opted into via ".pplns" worker suffix.
    # Miners whose worker name starts with "pplns" mine against a SHARED coinbase with
    # multi-vout payouts weighted by recent share contribution. Solo miners (default)
    # keep the existing single-vout coinbase paying only themselves.
    'pplns_window_size': 500,        # last N accepted PPLNS shares considered
    'pplns_min_share_pct': 0.0,      # 0 = no dust floor; max_vouts caps spam instead.
                                     # (Previously 0.5 — would drop MMMs paired with ASICs.)
    'pplns_max_vouts': 20,           # cap vouts in coinbase (keeps tx size sane)
}

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
log = logging.getLogger('nexpool')
# ═══════════════════════════════════════════════════════════════════════════
# TELEMETRY — report block wins to MacMetalMiner.com
# ═══════════════════════════════════════════════════════════════════════════
def report_block_to_website(address, block_hash, height, reward):
    """Report a block win to the MacMetalMiner website API."""
    try:
        payload = json.dumps({
            'action': 'block_won',
            'address': address,
            'reward': reward,
            'block_height': height,
            'block_hash': block_hash,
            'machine': 'Pool (nex_pool.py)',
            'via': 'pool'
        }).encode()
        req = urllib.request.Request(
            'http://52.22.9.39/api/heartbeat.php',
            data=payload,
            headers={'Content-Type': 'application/json', 'Host': 'macmetalminer.com'}
        )
        urllib.request.urlopen(req, timeout=10)
        log.info(f"📡 Block reported to MacMetalMiner.com")
    except Exception as e:
        log.warning(f"📡 Failed to report block to website: {e}")


# ═══════════════════════════════════════════════════════════════════════════
# RPC
# ═══════════════════════════════════════════════════════════════════════════
def rpc_call(method, params=None):
    payload = json.dumps({
        'jsonrpc': '1.0',
        'id': 'nexpool',
        'method': method,
        'params': params or []
    }).encode()
    url = f"http://{CONFIG['rpc_host']}:{CONFIG['rpc_port']}/"
    req = urllib.request.Request(url, data=payload)
    auth = base64.b64encode(f"{CONFIG['rpc_user']}:{CONFIG['rpc_password']}".encode()).decode()
    req.add_header('Authorization', f'Basic {auth}')
    req.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read())
            if data.get('error'):
                log.error(f"RPC error {method}: {data['error']}")
                return None
            return data.get('result')
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors='replace')
        log.error(f"RPC HTTP {e.code} on {method}: {body[:200]}")
        return None
    except Exception as e:
        log.error(f"RPC {method} failed: {e}")
        return None

# ═══════════════════════════════════════════════════════════════════════════
# Serialization helpers
# ═══════════════════════════════════════════════════════════════════════════
def dsha256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def encode_varint(n):
    if n < 0xfd: return bytes([n])
    if n <= 0xffff: return b'\xfd' + n.to_bytes(2, 'little')
    if n <= 0xffffffff: return b'\xfe' + n.to_bytes(4, 'little')
    return b'\xff' + n.to_bytes(8, 'little')

def encode_script_pushbytes(data):
    n = len(data)
    if n < 0x4c: return bytes([n]) + data
    if n <= 0xff: return b'\x4c' + bytes([n]) + data
    if n <= 0xffff: return b'\x4d' + n.to_bytes(2, 'little') + data
    return b'\x4e' + n.to_bytes(4, 'little') + data

# ═══════════════════════════════════════════════════════════════════════════
# Address decoding (bech32m) — so miners can authorize with their nex1… address
# and the pool pays that miner directly instead of a hardcoded pool address.
# ═══════════════════════════════════════════════════════════════════════════
_BECH32_CHARSET = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'
_BECH32_GEN = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3]

def _bech32_polymod(values):
    chk = 1
    for v in values:
        b = chk >> 25
        chk = ((chk & 0x1ffffff) << 5) ^ v
        for i in range(5):
            chk ^= _BECH32_GEN[i] if ((b >> i) & 1) else 0
    return chk

def bech32m_decode(addr):
    """Decode nex1… address → 32-byte pubkey hash (witness v2).
    Returns None if the string is not a valid nex1 v2 bech32m address."""
    try:
        if not isinstance(addr, str):
            return None
        addr = addr.lower()
        if '1' not in addr or len(addr) > 120:
            return None
        pos = addr.rfind('1')
        hrp = addr[:pos]
        if hrp != 'nex':
            return None
        data_part = addr[pos+1:]
        try:
            data = [_BECH32_CHARSET.index(c) for c in data_part]
        except ValueError:
            return None
        if len(data) < 7:
            return None
        hrp_exp = [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp]
        if _bech32_polymod(hrp_exp + data) != 0x2bc830a3:
            return None
        wit_ver = data[0]
        acc, bits, program = 0, 0, []
        for v in data[1:-6]:
            acc = (acc << 5) | v
            bits += 5
            while bits >= 8:
                bits -= 8
                program.append((acc >> bits) & 0xff)
        if wit_ver != 2 or len(program) != 32:
            return None
        return bytes(program)
    except Exception:
        return None

def build_coinbase(height, reward, extranonce_placeholder_size, witness_commitment_hex=None,
                   payout_pubkey_hash=None, payouts=None):
    """
    Returns (coinbase_part1, coinbase_part2) where the full coinbase tx =
    part1 + extranonce1 + extranonce2 + part2
    The extranonce placeholder gives miner space to fiddle.

    Two payout modes:
      SOLO (default):   pass payout_pubkey_hash → single vout paying `reward` to that hash.
      PPLNS:            pass payouts=[(pkh_bytes, sats), ...] → one vout per entry.
                        Sum of sats MUST equal `reward` (checked); any residual goes to the
                        caller to fix — this function does not redistribute silently.

    Output N+ (optional): witness commitment (OP_RETURN) appended after payout outputs.
    """
    # BIP34 height push — matches Bitcoin's CScript() << nHeight behavior:
    # - 0              → OP_0  (0x00)
    # - 1..16          → OP_1..OP_16  (0x51..0x60)
    # - everything else → minimal CScriptNum data push
    if height == 0:
        height_push = bytes([0x00])
    elif 1 <= height <= 16:
        height_push = bytes([0x50 + height])  # OP_1=0x51, OP_16=0x60
    else:
        h_bytes = height.to_bytes((height.bit_length() + 7) // 8 or 1, 'little')
        if h_bytes and h_bytes[-1] >= 0x80:
            h_bytes += b'\x00'  # keep sign bit clear (CScriptNum is signed)
        height_push = encode_script_pushbytes(h_bytes)

    # Coinbase scriptSig: height_push + <extranonce_placeholder_size bytes of random>
    # Stratum will insert extranonce1+extranonce2 INTO this slot.
    # We emit: height_push || /nex-solo/ tag || [reserved space for extranonce]
    tag = _POOL_TAG
    pre_en = height_push + encode_script_pushbytes(tag)
    # extranonce slot comes after this prefix
    script_sig_total_len = len(pre_en) + extranonce_placeholder_size

    # Build tx
    version = (1).to_bytes(4, 'little')
    # No witness in coinbase input for now (simpler). Witness commitment goes in vout.
    in_count = encode_varint(1)
    prev_out = b'\x00' * 32 + b'\xff\xff\xff\xff'
    script_sig_len = encode_varint(script_sig_total_len)
    # Build: version + in_count + prev_out + script_sig_len + pre_en  ||  [extranonce slot]  ||  sequence + outputs + locktime
    part1 = version + in_count + prev_out + script_sig_len + pre_en
    sequence = b'\xff\xff\xff\xff'

    # Outputs — either a single payout (solo) or a weighted multi-payout (PPLNS).
    # Each payout vout uses witness v2 style: OP_2 <32-byte pubkey hash>.
    outs = b''
    out_count = 0
    if payouts is not None:
        # PPLNS: multiple reward outputs. Validate sum == reward so we never silently
        # mint or burn satoshis.
        total = sum(sats for _, sats in payouts)
        if total != reward:
            raise ValueError(f"PPLNS payouts sum {total} != reward {reward}")
        for pkh, sats in payouts:
            script = bytes([0x52, 0x20]) + pkh
            outs += sats.to_bytes(8, 'little') + encode_varint(len(script)) + script
            out_count += 1
    else:
        # Solo: single reward output to the miner's (or pool fallback) address.
        if payout_pubkey_hash is None:
            payout_pubkey_hash = CONFIG['payout_pubkey_hash']
        if payout_pubkey_hash is None:
            raise ValueError(
                "build_coinbase: solo mode requires either a per-miner "
                "payout_pubkey_hash (set via mining.authorize) or a "
                "NEX_PAYOUT_PUBKEY_HASH env-var fallback. Neither is set."
            )
        script = bytes([0x52, 0x20]) + payout_pubkey_hash
        outs += reward.to_bytes(8, 'little') + encode_varint(len(script)) + script
        out_count = 1
    if witness_commitment_hex:
        wc_script = bytes.fromhex(witness_commitment_hex)
        out_wc = (0).to_bytes(8, 'little') + encode_varint(len(wc_script)) + wc_script
        outs += out_wc
        out_count += 1

    locktime = (0).to_bytes(4, 'little')
    part2 = sequence + encode_varint(out_count) + outs + locktime
    return part1, part2

def merkle_root_from_hashes(coinbase_hash, tx_hashes):
    """Standard Bitcoin merkle root from coinbase + tx hashes (all 32-byte LE)."""
    hashes = [coinbase_hash] + tx_hashes
    while len(hashes) > 1:
        if len(hashes) % 2:
            hashes.append(hashes[-1])
        hashes = [dsha256(hashes[i] + hashes[i+1]) for i in range(0, len(hashes), 2)]
    return hashes[0]

def merkle_branch(tx_hashes):
    """Compute merkle branch for coinbase (index 0) — list of sibling hashes."""
    if not tx_hashes:
        return []
    branch = []
    hashes = [b'\x00'*32] + tx_hashes  # placeholder at index 0
    while len(hashes) > 1:
        if len(hashes) % 2:
            hashes.append(hashes[-1])
        # sibling of index 0 is hashes[1]
        branch.append(hashes[1])
        # next level
        new_hashes = [dsha256(hashes[i] + hashes[i+1]) for i in range(0, len(hashes), 2)]
        hashes = new_hashes
    return branch

def bits_to_target(bits_hex):
    bits = int(bits_hex, 16)
    exp = bits >> 24
    mant = bits & 0xFFFFFF
    return mant * (1 << (8 * (exp - 3)))

def diff_to_target(difficulty):
    """Compute 256-bit share target from floating-point difficulty. Supports diff < 1."""
    max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    d = max(1e-6, float(difficulty))
    # Scale: target = max_target / d, computed via integer math to avoid float loss at 256 bits
    # Use a 64-bit scaling factor so fractional diffs round correctly
    scale = 10**9
    return (max_target * scale) // int(d * scale)

# ═══════════════════════════════════════════════════════════════════════════
# Job Manager
# ═══════════════════════════════════════════════════════════════════════════
class JobManager:
    """Holds the current block template + derived metadata that's shared across all miners.

    Per-miner jobs (with each miner's own coinbase/payout) are built by the StratumSession
    using session._build_job() — see that method for the per-miner logic.
    """
    def __init__(self):
        self.current_template = None  # raw getblocktemplate result + _tx_hashes_le / _merkle_branch_* / _target
        self.template_seq = 0  # bumps on every tip change so sessions know work invalidated
        self.subscribers = []  # list of StratumSession
        # PPLNS ledger — every accepted share from a .pplns session is appended as
        # (pkh_hex, weight=share_diff, timestamp). Rolling window is bounded by maxlen
        # so memory use is O(pplns_window_size) regardless of pool uptime.
        self.pplns_shares = deque(maxlen=CONFIG['pplns_window_size'])

    def record_pplns_share(self, pkh_hex, weight):
        """Append an accepted PPLNS share's work contribution to the rolling ledger."""
        self.pplns_shares.append((pkh_hex, float(weight), time.time()))

    def compute_pplns_distribution(self, total_reward):
        """
        Compute the coinbase payout list from the current PPLNS window.
        Returns list of (pkh_bytes, sats) whose sats sum exactly to total_reward,
        or None if the window is empty (caller should fall back to solo payout).

        Pipeline:
          1. Aggregate share weights by pkh.
          2. Drop entries below pplns_min_share_pct to avoid dust; their weight is
             implicitly redistributed via renormalization over the kept set.
          3. Cap the number of vouts at pplns_max_vouts (keep largest).
          4. Convert proportions to integer satoshis, giving any rounding remainder
             to the LARGEST recipient so the total is exact.
        """
        if not self.pplns_shares:
            return None
        weights = {}
        for pkh_hex, w, _ts in self.pplns_shares:
            weights[pkh_hex] = weights.get(pkh_hex, 0.0) + w
        total_weight = sum(weights.values())
        if total_weight <= 0:
            return None
        min_frac = CONFIG['pplns_min_share_pct'] / 100.0
        min_weight = total_weight * min_frac
        kept = {k: w for k, w in weights.items() if w >= min_weight}
        if not kept:
            # All contributions are below the threshold — keep the single largest.
            top = max(weights.items(), key=lambda x: x[1])
            kept = {top[0]: top[1]}
        max_vouts = CONFIG['pplns_max_vouts']
        if len(kept) > max_vouts:
            kept = dict(sorted(kept.items(), key=lambda x: x[1], reverse=True)[:max_vouts])
        total_kept = sum(kept.values())
        # Largest first so remainder lands on the most-deserving recipient.
        ordered = sorted(kept.items(), key=lambda x: x[1], reverse=True)
        payouts = []
        distributed = 0
        for pkh_hex, w in ordered[1:]:
            sats = int(total_reward * w / total_kept)
            payouts.append((bytes.fromhex(pkh_hex), sats))
            distributed += sats
        # Largest gets total_reward - distributed (absorbs rounding; always >= its own share).
        top_pkh, _ = ordered[0]
        payouts.insert(0, (bytes.fromhex(top_pkh), total_reward - distributed))
        return payouts

    def refresh(self):
        """Fetch a fresh template from nexd. Returns (ok, tip_changed)."""
        tmpl = rpc_call('getblocktemplate', [{'rules': ['segwit']}])
        if not tmpl:
            return False, False
        same_tip = (
            self.current_template is not None and
            self.current_template.get('previousblockhash') == tmpl.get('previousblockhash') and
            self.current_template.get('coinbasevalue') == tmpl.get('coinbasevalue') and
            (self.current_template.get('transactions') or []) == (tmpl.get('transactions') or [])
        )
        if not same_tip:
            self.template_seq += 1
            # Precompute the merkle branch — same for all miners (depends only on non-coinbase txs)
            tx_hashes_le = []
            for tx in tmpl.get('transactions', []):
                txid_hex = tx.get('txid') or tx.get('hash')
                tx_hashes_le.append(bytes.fromhex(txid_hex)[::-1])
            tmpl['_tx_hashes_le'] = tx_hashes_le
            tmpl['_merkle_branch_le'] = merkle_branch(tx_hashes_le)
            tmpl['_merkle_branch_hex'] = [b.hex() for b in tmpl['_merkle_branch_le']]
            tmpl['_target'] = bits_to_target(tmpl['bits'])
            tmpl['_template_seq'] = self.template_seq
        else:
            # Keep precomputed fields across ntime-only refreshes
            for k in ('_tx_hashes_le', '_merkle_branch_le', '_merkle_branch_hex', '_target', '_template_seq'):
                if k in self.current_template:
                    tmpl[k] = self.current_template[k]
        self.current_template = tmpl
        return True, (not same_tip)

# ═══════════════════════════════════════════════════════════════════════════
# Stratum server
# ═══════════════════════════════════════════════════════════════════════════
class StratumSession:
    _extranonce1_counter = 0

    def __init__(self, reader, writer, job_mgr, default_pplns=False):
        self.reader = reader
        self.writer = writer
        self.job_mgr = job_mgr
        self.peer = writer.get_extra_info('peername')
        # Which port did this session land on? Port determines reward model up-front;
        # no username-suffix magic is needed. See main() — two listeners pass the flag.
        self._default_pplns = default_pplns
        StratumSession._extranonce1_counter += 1
        self.extranonce1 = StratumSession._extranonce1_counter.to_bytes(CONFIG['extranonce1_size'], 'big')
        self.difficulty = CONFIG['initial_difficulty']
        self.worker_name = None
        self.subscribed = False
        self.shares_accepted = 0
        self.shares_rejected = 0
        self.blocks_found = 0
        self.user_agent = ''
        # Vardiff state: track recent share submission times to tune per-miner difficulty
        self.share_times = []  # deque of epoch seconds, last N accepted shares
        self.last_diff_change = time.time()
        self.connected_at = time.time()
        self.best_diff = 0
        self.last_message_at = time.time()
        # Per-miner payout: set from mining.authorize if they pass a valid nex1… address.
        # Defaults to the pool operator's configured hash as a safety net.
        self.payout_pubkey_hash = CONFIG['payout_pubkey_hash']
        self.payout_address = None  # display string
        # Reward model: False = solo (single-vout to self). True = PPLNS (shared
        # coinbase distributes across recent contributors). Set at connect time
        # based on which port the client dialed — see main().
        self.is_pplns = default_pplns
        # Per-miner job cache (each miner gets their own coinbase → different job_id)
        self.current_job = None
        self.jobs_by_id = {}
        self.job_counter = 0
        self.job_history_cap = 16

    def _build_job(self, clean):
        """Build a new job using THIS session's payout_pubkey_hash. Caches it on the session."""
        if self.payout_pubkey_hash is None:
            # Miner subscribed but never authorized with a NEX address (and the
            # operator hasn't set NEX_PAYOUT_PUBKEY_HASH as a fallback). Without
            # an address we can't build a coinbase. Decline silently — real
            # Stratum miners always authorize before requesting jobs.
            return None
        t = self.job_mgr.current_template
        if not t:
            return None
        self.job_counter += 1
        # Namespace job_id by the session's extranonce1 so ids are unique across miners
        job_id = f'{self.extranonce1.hex()}-{self.job_counter:x}'

        wc = t.get('default_witness_commitment')
        extranonce_total = CONFIG['extranonce1_size'] + CONFIG['extranonce2_size']
        if self.is_pplns:
            # PPLNS: build coinbase with the rolling share-weighted distribution.
            # Bootstrap: if the ledger is still empty (e.g. first PPLNS miner ever),
            # fall back to paying just this miner so work can start immediately —
            # as they submit shares, the ledger fills and subsequent job refreshes
            # will widen the distribution.
            payouts = self.job_mgr.compute_pplns_distribution(t['coinbasevalue'])
            if not payouts:
                payouts = [(self.payout_pubkey_hash, t['coinbasevalue'])]
            cb1, cb2 = build_coinbase(
                t['height'], t['coinbasevalue'], extranonce_total, wc,
                payouts=payouts,
            )
        else:
            cb1, cb2 = build_coinbase(
                t['height'], t['coinbasevalue'], extranonce_total, wc,
                payout_pubkey_hash=self.payout_pubkey_hash,
            )

        job = {
            'job_id': job_id,
            'prev_hash_le': bytes.fromhex(t['previousblockhash'])[::-1],
            'prev_hash_display': t['previousblockhash'],
            'coinbase_part1': cb1,
            'coinbase_part2': cb2,
            'merkle_branch_le': t['_merkle_branch_le'],
            'merkle_branch_hex': t['_merkle_branch_hex'],
            'version': t['version'],
            'bits': t['bits'],
            'time': t['curtime'],
            'height': t['height'],
            'tx_hashes_le': t['_tx_hashes_le'],
            'target': t['_target'],
            'clean': clean,
            'reward': t['coinbasevalue'],
            'template_seq': t['_template_seq'],
            'payout_pubkey_hash': self.payout_pubkey_hash,
        }
        self.current_job = job
        self.jobs_by_id[job_id] = job
        if len(self.jobs_by_id) > self.job_history_cap:
            # Drop the oldest by (template_seq, counter) so we keep the freshest slots
            def _key(k):
                j = self.jobs_by_id[k]
                return (j.get('template_seq', 0), int(k.split('-')[-1], 16))
            oldest = sorted(self.jobs_by_id.keys(), key=_key)[0]
            self.jobs_by_id.pop(oldest, None)
        return job

    def notify_params(self):
        """Return stratum mining.notify params for THIS session's current job."""
        j = self.current_job
        if not j:
            return None
        display_bytes = bytes.fromhex(j['prev_hash_display'])
        words = [display_bytes[i:i+4] for i in range(0, 32, 4)]
        prev_hex = b''.join(reversed(words)).hex()
        version_hex = j['version'].to_bytes(4, 'big').hex()
        bits_hex = j['bits']
        time_hex = j['time'].to_bytes(4, 'big').hex()
        return [
            j['job_id'], prev_hex,
            j['coinbase_part1'].hex(), j['coinbase_part2'].hex(),
            j['merkle_branch_hex'],
            version_hex, bits_hex, time_hex,
            j['clean'],
        ]

    def validate_share(self, job_id, extranonce2_hex, ntime_hex, nonce_hex, worker_diff, version_bits_hex=None):
        """Validate a share against THIS session's jobs. Returns (is_valid, is_block, block_hex, block_hash)."""
        j = self.jobs_by_id.get(job_id) or self.current_job
        if not j or j['job_id'] != job_id:
            log.info(f"  reject: unknown job_id={job_id} for {self.peer}")
            return False, False, None, None

        en2 = bytes.fromhex(extranonce2_hex)
        cb = j['coinbase_part1'] + self.extranonce1 + en2 + j['coinbase_part2']
        cb_hash_le = dsha256(cb)

        root_le = cb_hash_le
        for sib in j['merkle_branch_le']:
            root_le = dsha256(root_le + sib)

        version = j['version']
        if version_bits_hex:
            mask = 0x1fffe000
            version = (version & ~mask) | (int(version_bits_hex, 16) & mask)
        prev_le = j['prev_hash_le']
        ntime = int(ntime_hex, 16)
        bits = int(j['bits'], 16)
        nonce = int(nonce_hex, 16)
        header = (
            version.to_bytes(4, 'little') + prev_le + root_le +
            ntime.to_bytes(4, 'little') + bits.to_bytes(4, 'little') + nonce.to_bytes(4, 'little')
        )
        hash_le = dsha256(header)
        hash_int = int.from_bytes(hash_le, 'little')

        share_target = diff_to_target(worker_diff)
        block_target = j['target']
        max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
        effective_diff = max_target / max(1, hash_int)
        log.info(f"  share diff={effective_diff:.2f} need>={worker_diff} hash={hash_le[::-1].hex()[:16]}...")

        if hash_int > share_target:
            return False, False, None, None

        is_block = hash_int <= block_target
        if not is_block:
            log.info(f"    (share valid but not a block: hash_int={hash_int:#x} block_target={block_target:#x})")
        if is_block:
            all_txs = [cb]
            for tx in (self.job_mgr.current_template.get('transactions') or []):
                all_txs.append(bytes.fromhex(tx['data']))
            block = header + encode_varint(len(all_txs)) + b''.join(all_txs)
            block_hash_display = hash_le[::-1].hex()
            return True, True, block.hex(), block_hash_display

        return True, False, None, None

    async def send(self, obj):
        line = json.dumps(obj) + '\n'
        try:
            self.writer.write(line.encode())
            await self.writer.drain()
            log.info(f"[{self.peer}] -> {line.strip()[:180]}")
        except Exception as e:
            log.warning(f"[{self.peer}] send failed: {e}")

    async def _set_difficulty(self, new_diff):
        """Update per-session difficulty and notify the miner."""
        new_diff = max(CONFIG['vardiff_min'], min(CONFIG['vardiff_max'], new_diff))
        if new_diff == self.difficulty:
            return
        old = self.difficulty
        self.difficulty = new_diff
        self.last_diff_change = time.time()
        self.share_times.clear()  # reset measurement window
        await self.send({'id': None, 'method': 'mining.set_difficulty', 'params': [float(new_diff)]})
        log.info(f"[{self.peer}] vardiff: {old} -> {new_diff:.4g}")
        # re-send current job so miner starts on the new target immediately
        if self.job_mgr.current_template:
            self._build_job(clean=False)
            await self.send({'id': None, 'method': 'mining.notify', 'params': self.notify_params()})

    async def _maybe_retune_difficulty(self):
        """Tune diff so each miner sees ~1 share per target_seconds.

        Direct hashrate estimation (not 2x ping-pong):
            H         = diff * 2^32 * n_intervals / span
            diff_new  = H * target / 2^32  =  diff * n_intervals * target / span

        Clamped per-retune to ride out variance on lucky/unlucky share runs.
        """
        now = time.time()
        if now - self.last_diff_change < CONFIG['vardiff_retune_seconds']:
            return
        if len(self.share_times) < 2:
            return  # need at least 2 shares to measure an interval
        span = self.share_times[-1] - self.share_times[0]
        if span <= 0:
            return
        n_intervals = len(self.share_times) - 1
        target = CONFIG['vardiff_target_seconds']
        ratio = (n_intervals * target) / span  # = ideal_diff / current_diff
        # Deadband: ignore small jitter to avoid ping-pong
        deadband = CONFIG['vardiff_deadband']
        if (1 - deadband) < ratio < (1 + deadband):
            return
        # Clamp per-step multiplier
        ratio = max(CONFIG['vardiff_max_step_down'],
                    min(CONFIG['vardiff_max_step_up'], ratio))
        await self._set_difficulty(self.difficulty * ratio)

    async def handle(self):
        log.info(f"[{self.peer}] connected")
        try:
            while True:
                line = await self.reader.readline()
                if not line:
                    break
                try:
                    msg = json.loads(line.decode().strip())
                except json.JSONDecodeError:
                    continue
                await self._dispatch(msg)
        except Exception as e:
            log.warning(f"[{self.peer}] error: {e}")
        finally:
            log.info(f"[{self.peer}] disconnected (accepted={self.shares_accepted} rejected={self.shares_rejected} blocks={self.blocks_found})")
            try:
                self.job_mgr.subscribers.remove(self)
            except ValueError:
                pass
            self.writer.close()

    async def _dispatch(self, msg):
        method = msg.get('method')
        params = msg.get('params') or []
        msg_id = msg.get('id')
        self.last_message_at = time.time()
        log.info(f"[{self.peer}] <- {method} {json.dumps(params)[:120]}")

        if method == 'mining.configure':
            # Support BIP310 version-rolling (ASICBoost)
            extensions = params[0] if len(params) > 0 else []
            ext_params = params[1] if len(params) > 1 else {}
            result = {}
            if 'version-rolling' in extensions:
                result['version-rolling'] = True
                result['version-rolling.mask'] = '1fffe000'
            await self.send({'id': msg_id, 'result': result, 'error': None})
            return

        if method == 'mining.subscribe':
            self.subscribed = True
            if len(params) > 0 and isinstance(params[0], str):
                self.user_agent = params[0][:80]
            # Pick a starting difficulty based on the miner's self-reported UA.
            # Gets the very first share landing near target_seconds, so vardiff
            # (which needs share timing signal) can fine-tune from a sane base.
            ua_lower = self.user_agent.lower()
            matched = None
            for token, start_diff in CONFIG['ua_starting_diff']:
                if token in ua_lower:
                    self.difficulty = max(CONFIG['vardiff_min'],
                                          min(CONFIG['vardiff_max'], start_diff))
                    matched = token
                    break
            if matched:
                log.info(f"[{self.peer}] UA='{self.user_agent}' matched '{matched}' → starting diff={self.difficulty}")
            else:
                log.info(f"[{self.peer}] UA='{self.user_agent}' no match → diff={self.difficulty}")
            self.job_mgr.subscribers.append(self)
            subscription_id = 'nex'
            await self.send({
                'id': msg_id,
                'result': [
                    [['mining.set_difficulty', subscription_id], ['mining.notify', subscription_id]],
                    self.extranonce1.hex(),
                    CONFIG['extranonce2_size'],
                ],
                'error': None
            })
            # Send difficulty + a first job (payout falls back to pool operator until authorize arrives)
            await self.send({'id': None, 'method': 'mining.set_difficulty', 'params': [float(self.difficulty)]})
            if self.job_mgr.current_template:
                self._build_job(clean=True)
                await self.send({'id': None, 'method': 'mining.notify', 'params': self.notify_params()})

        elif method == 'mining.authorize':
            username = params[0] if params else 'anon'
            self.worker_name = username
            # Mode (solo vs PPLNS) is already set by which port the session dialed; the
            # username is only parsed to extract the payout address (and an optional
            # free-form worker tag after the first dot, which we log but don't interpret).
            #   "nex1z…"              → address only
            #   "nex1z….rig1"         → address + worker tag "rig1"
            candidate = username.split('.', 1)[0]
            pkh = bech32m_decode(candidate)
            if pkh:
                self.payout_pubkey_hash = pkh
                self.payout_address = candidate
                mode = 'PPLNS (shared)' if self.is_pplns else 'solo (100% to self)'
                log.info(f"[{self.peer}] authorized as {username} → {mode}, addr={candidate[:16]}…")
                # Rebuild the job with the new payout and push it so mining starts immediately
                if self.job_mgr.current_template:
                    self._build_job(clean=True)
                    await self.send({'id': None, 'method': 'mining.notify', 'params': self.notify_params()})
            else:
                log.warning(f"[{self.peer}] authorized as {username} — NOT a valid nex1 v2 address, using pool fallback payout")
            await self.send({'id': msg_id, 'result': True, 'error': None})

        elif method == 'mining.submit':
            # params: [worker, job_id, extranonce2, ntime, nonce, version_bits(optional)]
            if len(params) < 5:
                await self.send({'id': msg_id, 'result': False, 'error': [20, 'Bad params', None]})
                return
            worker = params[0]; job_id = params[1]; en2 = params[2]; ntime = params[3]; nonce = params[4]
            version_bits = params[5] if len(params) > 5 else None
            is_valid, is_block, block_hex, block_hash = self.validate_share(
                job_id, en2, ntime, nonce, self.difficulty, version_bits
            )
            if is_valid:
                self.shares_accepted += 1
                self.share_times.append(time.time())
                # keep only the vardiff window
                max_window = CONFIG['vardiff_window']
                if len(self.share_times) > max_window:
                    self.share_times = self.share_times[-max_window:]
                # Record PPLNS contribution (weight = share difficulty = work proven).
                # Solo sessions intentionally don't touch the ledger — they compete
                # independently for the whole block reward when one of their shares wins.
                if self.is_pplns and self.payout_pubkey_hash:
                    self.job_mgr.record_pplns_share(self.payout_pubkey_hash.hex(), self.difficulty)
                await self.send({'id': msg_id, 'result': True, 'error': None})
                await self._maybe_retune_difficulty()
                if is_block:
                    height = self.current_job['height'] if self.current_job else (self.job_mgr.current_template or {}).get('height', 0)
                    log.warning(f"🎉 BLOCK FOUND! hash={block_hash} height={height} by {self.worker_name} → {self.payout_address or 'pool'}")
                    result = rpc_call('submitblock', [block_hex])
                    accepted = (result is None or result == '' or (isinstance(result, str) and result == ''))
                    if accepted:
                        self.blocks_found += 1
                        log.warning(f"✅ Block accepted by nexd!")
                        # Report to MacMetalMiner.com website
                        try:
                            _wn = (self.worker_name or '').split('.', 1)
                            _addr = _wn[0]
                            _suffix = _wn[1].upper() if len(_wn) > 1 else ''
                            report_block_to_website(_addr, block_hash, height, 100.0)
                        except Exception as e:
                            log.warning(f"Failed to report block: {e}")
                        # Notify the miner with a custom stratum message — their UI can show the real count
                        await self.send({
                            'id': None,
                            'method': 'mining.block_found',
                            'params': [block_hash, height, 100.0]  # hash, height, reward NEX
                        })
                    else:
                        log.error(f"❌ Block rejected by nexd: {result}")
                    # Force job refresh
                    await asyncio.sleep(0.5)
                    self.job_mgr.refresh()
                    await broadcast_new_job(self.job_mgr, clean=True)
            else:
                self.shares_rejected += 1
                await self.send({'id': msg_id, 'result': False, 'error': [23, 'Low difficulty share', None]})

        elif method == 'mining.extranonce.subscribe':
            await self.send({'id': msg_id, 'result': True, 'error': None})

        elif method == 'mining.suggest_difficulty':
            # Honor miner's suggested diff as a starting point (vardiff will take over once shares flow)
            try:
                suggested = params[0] if params else None
                if suggested is not None:
                    val = float(suggested)
                    if 0 < val <= CONFIG['vardiff_max']:
                        await self._set_difficulty(val)
            except (ValueError, TypeError):
                pass
            await self.send({'id': msg_id, 'result': True, 'error': None})

        else:
            await self.send({'id': msg_id, 'result': None, 'error': [20, f'Unknown method {method}', None]})


async def broadcast_new_job(job_mgr, clean):
    """Build a fresh per-miner job for each subscriber and push it."""
    dead = []
    for s in list(job_mgr.subscribers):
        try:
            if not s.subscribed or not job_mgr.current_template:
                continue
            s._build_job(clean=clean)
            await s.send({'id': None, 'method': 'mining.notify', 'params': s.notify_params()})
        except Exception:
            dead.append(s)
    for d in dead:
        try: job_mgr.subscribers.remove(d)
        except ValueError: pass


async def job_refresh_loop(job_mgr):
    last_prev = None
    last_broadcast = 0
    REBROADCAST_SECONDS = 30  # push fresh ntime to miners even if tip unchanged
    while True:
        ok, tip_changed = job_mgr.refresh()
        if ok:
            t = job_mgr.current_template
            now = time.time()
            if tip_changed:
                log.info(f"new block tip: height={t['height']} prev={t['previousblockhash'][:16]}... reward={t['coinbasevalue']/1e8} NEX subs={len(job_mgr.subscribers)}")
                last_prev = t['previousblockhash']
            # Rebroadcast if tip changed OR we have not sent fresh work in a while
            if tip_changed or (now - last_broadcast) >= REBROADCAST_SECONDS:
                await broadcast_new_job(job_mgr, clean=tip_changed)
                last_broadcast = now
        await asyncio.sleep(10)  # check every 10s


async def stats_http_server(job_mgr):
    """Tiny HTTP endpoint for the explorer — returns pool stats JSON on GET /stats."""
    async def handle(reader, writer):
        try:
            req = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), timeout=3)
        except (asyncio.TimeoutError, asyncio.IncompleteReadError):
            writer.close(); return
        first_line = req.split(b'\r\n', 1)[0].decode(errors='replace')
        now = time.time()
        miners = []
        total_hashrate = 0.0
        for s in job_mgr.subscribers:
            if not s.subscribed:
                continue
            span = 0.0
            hashrate = 0.0
            if len(s.share_times) >= 2:
                span = s.share_times[-1] - s.share_times[0]
                if span > 0:
                    # hashes per share = diff * 2^32; shares per sec = (N-1)/span
                    shares_per_sec = (len(s.share_times) - 1) / span
                    hashrate = shares_per_sec * s.difficulty * (1 << 32)
            total_hashrate += hashrate
            addr = (s.worker_name or '').split('.', 1)[0]
            worker = (s.worker_name or '').split('.', 1)[1] if '.' in (s.worker_name or '') else ''
            miners.append({
                'user_agent': s.user_agent,
                'address': addr,
                'worker': worker,
                'peer': f'{s.peer[0]}:{s.peer[1]}' if s.peer else '',
                'difficulty': s.difficulty,
                'shares_accepted': s.shares_accepted,
                'shares_rejected': s.shares_rejected,
                'blocks_found': s.blocks_found,
                'hashrate': hashrate,
                'connected_seconds': int(now - s.connected_at),
                'last_share_seconds_ago': int(now - s.share_times[-1]) if s.share_times else None,
            })
        total_blocks = sum(s.blocks_found for s in job_mgr.subscribers)
        body = json.dumps({
            'miners': miners,
            'miner_count': len(miners),
            'total_hashrate': total_hashrate,
            'blocks_found_session': total_blocks,
            'tip_height': (job_mgr.current_template or {}).get('height', 0) - 1,
            'timestamp': int(now),
        }, default=str).encode()
        headers = (
            b'HTTP/1.1 200 OK\r\n'
            b'Content-Type: application/json\r\n'
            b'Access-Control-Allow-Origin: *\r\n'
            b'Content-Length: ' + str(len(body)).encode() + b'\r\n'
            b'Connection: close\r\n\r\n'
        )
        writer.write(headers + body)
        try:
            await writer.drain()
        except Exception:
            pass
        writer.close()

    server = await asyncio.start_server(handle, '0.0.0.0', CONFIG['stats_port'])
    log.info(f"stats HTTP listening on :{CONFIG['stats_port']}")
    async with server:
        await server.serve_forever()


async def silent_miner_loop(job_mgr):
    """Drop diff for miners that haven't submitted any share in a while — typically CPU miners at too-high diff."""
    IDLE_KICK_SECONDS = 180  # drop session if no activity for 3 minutes (zombie TCP)
    while True:
        await asyncio.sleep(15)
        now = time.time()
        for sess in list(job_mgr.subscribers):
            if not sess.subscribed:
                continue
            last_share = sess.share_times[-1] if sess.share_times else sess.connected_at
            silent_for = now - last_share
            last_msg = getattr(sess, 'last_message_at', sess.connected_at)
            idle_for = now - last_msg
            # Zombie TCP detection: disconnect sessions that haven't sent any message in 3 minutes
            if idle_for >= IDLE_KICK_SECONDS:
                log.info(f"[{sess.peer}] zombie detected, closing (idle {int(idle_for)}s)")
                try:
                    sess.writer.close()
                except Exception:
                    pass
                continue
            # Normal vardiff: drop diff if shares too slow
            if silent_for >= CONFIG['vardiff_no_share_timeout'] and sess.difficulty > CONFIG['vardiff_min']:
                try:
                    await sess._set_difficulty(sess.difficulty / 4)
                except Exception:
                    pass



async def website_heartbeat_loop(job_mgr):
    """Report connected miners' hashrate to MacMetalMiner.com every 60 seconds."""
    import urllib.request as _req
    while True:
        await asyncio.sleep(60)
        try:
            # Aggregate stats from all connected sessions
            for sess in list(job_mgr.subscribers):
                if not sess.subscribed or not sess.worker_name:
                    continue
                addr = sess.worker_name.split('.')[0] if '.' in (sess.worker_name or '') else sess.worker_name
                if not addr:
                    continue
                # Estimate hashrate from share rate and difficulty
                now = time.time()
                recent = [t for t in sess.share_times if now - t < 60]
                if recent:
                    shares_per_sec = len(recent) / max(1, now - recent[0])
                    hashrate = int(shares_per_sec * sess.difficulty * (2**32))
                else:
                    hashrate = 0
                # Detect via type from worker name suffix
                worker_suffix = (sess.worker_name or '').split('.', 1)[1].upper() if '.' in (sess.worker_name or '') else ''
                if worker_suffix == 'MMM':
                    via = 'GUI'
                elif worker_suffix in ('BITAXE', 'ASIC', 'BM', 'S9', 'S19', 'S21'):
                    via = 'ASIC'
                elif sess.difficulty >= 1000:
                    via = 'ASIC'  # high diff likely ASIC
                else:
                    via = 'CLI'
                payload = json.dumps({
                    'action': 'heartbeat',
                    'address': addr,
                    'hashrate': hashrate,
                    'total_hashes': 0,
                    'uptime': int(now - sess.connected_at),
                    'pool_name': '98.80.98.17',
                    'session_shares': sess.shares_accepted,
                    'best_diff': int(getattr(sess, 'best_diff', 0)),
                    'machine': '',
                    'gpu': '',
                    'version': '1.0',
                    'via': via
                }).encode()
                req = _req.Request(
                    'http://52.22.9.39/api/heartbeat.php',
                    data=payload,
                    headers={'Content-Type': 'application/json', 'Host': 'macmetalminer.com'}
                )
                _req.urlopen(req, timeout=5)
        except Exception as e:
            log.debug(f"heartbeat report error: {e}")


async def main():
    job_mgr = JobManager()
    ok, _ = job_mgr.refresh()
    if not ok:
        log.error("Initial getblocktemplate failed — is nexd running?")
        sys.exit(1)
    t = job_mgr.current_template
    log.info(f"first template fetched: height={t['height']} reward={t['coinbasevalue']/1e8} NEX (per-miner coinbases built on authorize)")

    # Two listeners, one per reward model — port-based routing is simpler for clients
    # (no username suffix needed) and keeps the two populations cleanly separated in logs.
    async def handle_pplns(reader, writer):
        sess = StratumSession(reader, writer, job_mgr, default_pplns=True)
        await sess.handle()

    async def handle_solo(reader, writer):
        sess = StratumSession(reader, writer, job_mgr, default_pplns=False)
        await sess.handle()

    pplns_server = await asyncio.start_server(handle_pplns, CONFIG['stratum_host'], CONFIG['pplns_port'])
    solo_server  = await asyncio.start_server(handle_solo,  CONFIG['stratum_host'], CONFIG['solo_port'])
    log.info(f"stratum PPLNS listening on {CONFIG['stratum_host']}:{CONFIG['pplns_port']}")
    log.info(f"stratum  SOLO listening on {CONFIG['stratum_host']}:{CONFIG['solo_port']}")

    asyncio.create_task(job_refresh_loop(job_mgr))
    asyncio.create_task(silent_miner_loop(job_mgr))
    asyncio.create_task(stats_http_server(job_mgr))
    asyncio.create_task(website_heartbeat_loop(job_mgr))

    async with pplns_server, solo_server:
        await asyncio.gather(pplns_server.serve_forever(), solo_server.serve_forever())


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("shutting down")
