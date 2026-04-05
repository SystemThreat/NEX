#!/usr/bin/env python3
"""
NEX Solo Mining Pool - Stratum v1 for Bitaxe
Pays 100% of block rewards to configured address (no pool fee).
"""
import asyncio
import json
import hashlib
import struct
import time
import urllib.request
import urllib.error
import base64
import logging
import sys

# ═══════════════════════════════════════════════════════════════════════════
# CONFIG
# ═══════════════════════════════════════════════════════════════════════════
CONFIG = {
    'stratum_host': '0.0.0.0',
    'stratum_port': 3333,
    'rpc_host': '127.0.0.1',
    'rpc_port': 9332,
    'rpc_user': 'nex',
    'rpc_password': '6c41e991419bef293d9910243a188fc2',
    # Payout: full 32-byte pubkey hash for nex1zcmctzd937pf5v683a3f4k6mzvqwxjx0n5x8l9e07f7e2vh7as8aqqwr4sq
    'payout_pubkey_hash': bytes.fromhex('c6f0b134b1f0534668f1ec535b6b62601c6919f3a18ff2e5fe4fb2a65fdd81fa'),
    'job_refresh_sec': 30,
    'extranonce1_size': 4,
    'extranonce2_size': 4,
    # Vardiff — per-miner adaptive difficulty
    'initial_difficulty': 16,       # start low so every miner sees a share within seconds
    'vardiff_target_seconds': 10,   # aim for 1 share every ~10 seconds
    'vardiff_min': 0.001,           # floor for CPU miners
    'vardiff_max': 1_000_000,       # ceiling for ASICs
    'vardiff_window': 8,            # shares in trailing average
    'vardiff_retune_seconds': 20,   # min interval between diff changes
    'vardiff_no_share_timeout': 45, # if no shares this long, drop diff hard
    'stats_port': 8081,             # internal HTTP endpoint for explorer to fetch stats
}

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
log = logging.getLogger('nexpool')

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

def build_coinbase(height, reward, extranonce_placeholder_size, witness_commitment_hex=None):
    """
    Returns (coinbase_part1, coinbase_part2) where the full coinbase tx =
    part1 + extranonce1 + extranonce2 + part2
    The extranonce placeholder gives miner space to fiddle.

    Output 0: reward to our address via OP_2 <32-byte-hash>
    Output 1 (optional): witness commitment (OP_RETURN)
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
    tag = b'/nex-solo/'
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

    # Outputs
    # Output 0: reward -> OP_2 <32 bytes>
    out0_script = bytes([0x52, 0x20]) + CONFIG['payout_pubkey_hash']
    out0 = reward.to_bytes(8, 'little') + encode_varint(len(out0_script)) + out0_script

    outs = out0
    out_count = 1
    if witness_commitment_hex:
        wc_script = bytes.fromhex(witness_commitment_hex)
        out1 = (0).to_bytes(8, 'little') + encode_varint(len(wc_script)) + wc_script
        outs += out1
        out_count = 2

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
    def __init__(self):
        self.current_template = None
        self.current_job = None
        self.job_counter = 0
        self.subscribers = []  # list of (send_func, extranonce1, extranonce2_size)
        self.jobs_by_id = {}  # job_id -> job dict, for validating stale submits
        self.job_history_cap = 16

    def refresh(self):
        tmpl = rpc_call('getblocktemplate', [{'rules': ['segwit']}])
        if not tmpl:
            return False
        # Only build a NEW job (and burn a new job_id) if the template actually changed.
        # Otherwise we'd silently invalidate every miner's last-known job every 30s.
        same_tip = (
            self.current_template is not None and
            self.current_template.get('previousblockhash') == tmpl.get('previousblockhash') and
            self.current_template.get('coinbasevalue') == tmpl.get('coinbasevalue') and
            (self.current_template.get('transactions') or []) == (tmpl.get('transactions') or [])
        )
        self.current_template = tmpl
        if not same_tip or self.current_job is None:
            self._build_job(clean=True)
        return True

    def _build_job(self, clean):
        t = self.current_template
        self.job_counter += 1
        job_id = f'{self.job_counter:x}'

        wc = t.get('default_witness_commitment')
        extranonce_total = CONFIG['extranonce1_size'] + CONFIG['extranonce2_size']
        cb1, cb2 = build_coinbase(t['height'], t['coinbasevalue'], extranonce_total, wc)

        # Parse tx hashes from template (stratum wants them little-endian, so we reverse display hex)
        tx_hashes_le = []
        for tx in t.get('transactions', []):
            # stratum coinbasetxn gives txid; use 'txid' field (display hex), reverse to LE
            txid_hex = tx.get('txid') or tx.get('hash')
            tx_hashes_le.append(bytes.fromhex(txid_hex)[::-1])

        branch = merkle_branch(tx_hashes_le)
        # Stratum v1 convention (per cgminer/ckpool): branches are sent in LE (internal) hex.
        # Miner concatenates these directly with the LE coinbase hash in sha256d.
        branch_hex = [b.hex() for b in branch]

        self.current_job = {
            'job_id': job_id,
            'prev_hash_le': bytes.fromhex(t['previousblockhash'])[::-1],
            'prev_hash_display': t['previousblockhash'],
            'coinbase_part1': cb1,
            'coinbase_part2': cb2,
            'merkle_branch_le': branch,
            'merkle_branch_hex': branch_hex,  # LE hex — what stratum miners expect
            'version': t['version'],
            'bits': t['bits'],
            'time': t['curtime'],
            'height': t['height'],
            'tx_hashes_le': tx_hashes_le,
            'target': bits_to_target(t['bits']),
            'clean': clean,
            'reward': t['coinbasevalue'],
        }
        # Remember recent jobs so miners can submit slightly-stale shares
        self.jobs_by_id[job_id] = self.current_job
        if len(self.jobs_by_id) > self.job_history_cap:
            # drop oldest (smallest counter) entries
            oldest = sorted(self.jobs_by_id.keys(), key=lambda k: int(k, 16))[0]
            self.jobs_by_id.pop(oldest, None)

    def notify_params(self):
        """Return stratum mining.notify params list."""
        j = self.current_job
        # Stratum v1 prev_hash format (matches what ESP-Miner/cgminer expect):
        # reverse the ORDER of the 4-byte words (NOT byte-swap within each word).
        # After ESP-Miner's two transformations (reverse_endianness_per_word +
        # reverse_32bit_words, then reverse_32bit_words again in test_nonce),
        # the resulting header[4..36] equals prev_le — matching nexd consensus.
        display_bytes = bytes.fromhex(j['prev_hash_display'])
        words = [display_bytes[i:i+4] for i in range(0, 32, 4)]
        prev_hex = b''.join(reversed(words)).hex()
        version_hex = j['version'].to_bytes(4, 'big').hex()
        bits_hex = j['bits']
        time_hex = j['time'].to_bytes(4, 'big').hex()
        return [
            j['job_id'],
            prev_hex,
            j['coinbase_part1'].hex(),
            j['coinbase_part2'].hex(),
            j['merkle_branch_hex'],
            version_hex,
            bits_hex,
            time_hex,
            j['clean'],
        ]

    def validate_share(self, job_id, extranonce1, extranonce2_hex, ntime_hex, nonce_hex, worker_diff, version_bits_hex=None):
        """Returns (is_valid_share, is_block, block_hex_or_none, block_hash_hex_or_none)."""
        # Look up job by id — accept recently-expired jobs too (prevents races on tip change)
        j = self.jobs_by_id.get(job_id) or self.current_job
        if not j or j['job_id'] != job_id:
            log.info(f"  reject: unknown job_id={job_id} current={self.current_job['job_id'] if self.current_job else 'none'}")
            return False, False, None, None

        # Reconstruct coinbase tx
        en2 = bytes.fromhex(extranonce2_hex)
        cb = j['coinbase_part1'] + extranonce1 + en2 + j['coinbase_part2']
        cb_hash_le = dsha256(cb)

        # Merkle root
        root_le = cb_hash_le
        for sib in j['merkle_branch_le']:
            root_le = dsha256(root_le + sib)

        # Build consensus-correct 80-byte header (same bytes nexd expects in the block)
        # Apply BIP310 version-rolling if the miner provided version_bits
        version = j['version']
        if version_bits_hex:
            mask = 0x1fffe000
            version = (version & ~mask) | (int(version_bits_hex, 16) & mask)
        prev_le = j['prev_hash_le']
        ntime = int(ntime_hex, 16)
        bits = int(j['bits'], 16)
        nonce = int(nonce_hex, 16)
        header = (
            version.to_bytes(4, 'little') +
            prev_le +
            root_le +
            ntime.to_bytes(4, 'little') +
            bits.to_bytes(4, 'little') +
            nonce.to_bytes(4, 'little')
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
            # Build full block: header + tx_count + coinbase + txs
            all_txs = [cb]
            for tx in (self.current_template.get('transactions') or []):
                all_txs.append(bytes.fromhex(tx['data']))
            block = header + encode_varint(len(all_txs)) + b''.join(all_txs)
            block_hash_display = hash_le[::-1].hex()
            return True, True, block.hex(), block_hash_display

        return True, False, None, None

# ═══════════════════════════════════════════════════════════════════════════
# Stratum server
# ═══════════════════════════════════════════════════════════════════════════
class StratumSession:
    _extranonce1_counter = 0

    def __init__(self, reader, writer, job_mgr):
        self.reader = reader
        self.writer = writer
        self.job_mgr = job_mgr
        self.peer = writer.get_extra_info('peername')
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
        self.last_message_at = time.time()

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
        if self.job_mgr.current_job:
            await self.send({'id': None, 'method': 'mining.notify', 'params': self.job_mgr.notify_params()})

    async def _maybe_retune_difficulty(self):
        """Adjust this miner's difficulty based on recent share arrival rate."""
        now = time.time()
        if now - self.last_diff_change < CONFIG['vardiff_retune_seconds']:
            return
        if len(self.share_times) < 3:
            return  # not enough data
        span = self.share_times[-1] - self.share_times[0]
        avg_interval = span / max(1, len(self.share_times) - 1)
        target = CONFIG['vardiff_target_seconds']
        # If shares arrive 2x too fast, double diff. 2x too slow, halve.
        if avg_interval < target / 2:
            await self._set_difficulty(self.difficulty * 2)
        elif avg_interval > target * 2:
            await self._set_difficulty(self.difficulty / 2)

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
            # Send difficulty + current job (force float for broader miner compatibility)
            await self.send({'id': None, 'method': 'mining.set_difficulty', 'params': [float(self.difficulty)]})
            if self.job_mgr.current_job:
                await self.send({'id': None, 'method': 'mining.notify', 'params': self.job_mgr.notify_params()})

        elif method == 'mining.authorize':
            self.worker_name = params[0] if params else 'anon'
            log.info(f"[{self.peer}] authorized as {self.worker_name}")
            await self.send({'id': msg_id, 'result': True, 'error': None})

        elif method == 'mining.submit':
            # params: [worker, job_id, extranonce2, ntime, nonce, version_bits(optional)]
            if len(params) < 5:
                await self.send({'id': msg_id, 'result': False, 'error': [20, 'Bad params', None]})
                return
            worker = params[0]; job_id = params[1]; en2 = params[2]; ntime = params[3]; nonce = params[4]
            version_bits = params[5] if len(params) > 5 else None
            is_valid, is_block, block_hex, block_hash = self.job_mgr.validate_share(
                job_id, self.extranonce1, en2, ntime, nonce, self.difficulty, version_bits
            )
            if is_valid:
                self.shares_accepted += 1
                self.share_times.append(time.time())
                # keep only the vardiff window
                max_window = CONFIG['vardiff_window']
                if len(self.share_times) > max_window:
                    self.share_times = self.share_times[-max_window:]
                await self.send({'id': msg_id, 'result': True, 'error': None})
                await self._maybe_retune_difficulty()
                if is_block:
                    height = self.job_mgr.current_job['height']
                    log.warning(f"🎉 BLOCK FOUND! hash={block_hash} height={height} by {self.worker_name}")
                    result = rpc_call('submitblock', [block_hex])
                    accepted = (result is None or result == '' or (isinstance(result, str) and result == ''))
                    if accepted:
                        self.blocks_found += 1
                        log.warning(f"✅ Block accepted by nexd!")
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
                    await broadcast_new_job(self.job_mgr)
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


async def broadcast_new_job(job_mgr):
    params = job_mgr.notify_params()
    dead = []
    for s in list(job_mgr.subscribers):
        try:
            await s.send({'id': None, 'method': 'mining.notify', 'params': params})
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
        if job_mgr.refresh():
            j = job_mgr.current_job
            now = time.time()
            tip_changed = (last_prev != j['prev_hash_display'])
            if tip_changed:
                log.info(f"new block tip: height={j['height']} prev={j['prev_hash_display'][:16]}... reward={j['reward']/1e8} NEX subs={len(job_mgr.subscribers)}")
                last_prev = j['prev_hash_display']
            # Rebroadcast if tip changed OR we have not sent fresh work in a while
            # (keeps miners from looping on stale ntime when no blocks are being found)
            if tip_changed or (now - last_broadcast) >= REBROADCAST_SECONDS:
                # Force a new job_id + fresh ntime even on same tip (clean=False so miners don't discard old shares)
                if not tip_changed:
                    job_mgr._build_job(clean=False)
                await broadcast_new_job(job_mgr)
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
            'tip_height': (job_mgr.current_job or {}).get('height', 0) - 1,
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


async def main():
    job_mgr = JobManager()
    if not job_mgr.refresh():
        log.error("Initial getblocktemplate failed — is nexd running?")
        sys.exit(1)
    log.info(f"first job built: height={job_mgr.current_job['height']} reward={job_mgr.current_job['reward']/1e8} NEX")

    async def handle_client(reader, writer):
        sess = StratumSession(reader, writer, job_mgr)
        await sess.handle()

    server = await asyncio.start_server(handle_client, CONFIG['stratum_host'], CONFIG['stratum_port'])
    addr = server.sockets[0].getsockname()
    log.info(f"stratum listening on {addr[0]}:{addr[1]}")

    asyncio.create_task(job_refresh_loop(job_mgr))
    asyncio.create_task(silent_miner_loop(job_mgr))
    asyncio.create_task(stats_http_server(job_mgr))

    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("shutting down")
