#!/usr/bin/env python3
"""
NEX Seed Recovery Spend — builds a raw transaction using the CLI wallet's
seed-based spending path (witness v2, Path B).

Reads the BIP-39 seed from macOS Keychain, constructs a transaction
spending from the CLI wallet address to a destination address.

Usage:
    python3 seed-spend.py <dest_address> <amount>
    python3 seed-spend.py nex1pw6uuah6ys5u4nhc9g33jac7adqjm28cy269qu5ewdhqvd8tjw8cqev8he5 3.0
"""

import json
import urllib.request
import subprocess
import struct
import hashlib
import sys
import base64

# Config
RPC_URL = 'http://127.0.0.1:9332'
RPC_USER = 'nex'
RPC_PASS = '6c41e991419bef293d9910243a188fc2'

CLI_ADDRESS = 'nex1zcmctzd937pf5v683a3f4k6mzvqwxjx0n5x8l9e07f7e2vh7as8aqqwr4sq'
CLI_PUBKEY_HASH = bytes.fromhex('c6f0b134b1f0534668f1ec535b6b62601c6919f3a18ff2e5fe4fb2a65fdd81fa')

COIN = 100_000_000  # satoshis per NEX
MIN_FEE = 10_000    # 0.0001 NEX fee

# ── RPC ──
def rpc(method, params=None):
    body = json.dumps({'jsonrpc': '1.0', 'id': 'spend', 'method': method, 'params': params or []}).encode()
    auth = base64.b64encode(f"{RPC_USER}:{RPC_PASS}".encode()).decode()
    req = urllib.request.Request(RPC_URL, data=body, headers={
        'Content-Type': 'application/json',
        'Authorization': f'Basic {auth}'
    })
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read())
        if data.get('error'):
            raise Exception(data['error'].get('message', str(data['error'])))
        return data['result']


# ── Keychain ──
def get_seed_from_keychain():
    """Read the BIP-39 seed from macOS Keychain (com.nex.wallet.cli / master_seed)."""
    try:
        result = subprocess.run(
            ['security', 'find-generic-password',
             '-s', 'com.nex.wallet.cli',
             '-a', 'master_seed',
             '-w'],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            raise Exception('Seed not found in Keychain')

        # The -w flag outputs the password data. For binary data it's hex encoded
        raw = result.stdout.strip()

        # Try hex decode first
        try:
            seed = bytes.fromhex(raw)
            if len(seed) == 64:
                return seed
        except ValueError:
            pass

        # Try base64
        try:
            seed = base64.b64decode(raw)
            if len(seed) == 64:
                return seed
        except:
            pass

        # Raw bytes (security outputs binary as-is sometimes)
        seed = raw.encode('latin-1')
        if len(seed) == 64:
            return seed

        raise Exception(f'Unexpected seed format: {len(seed)} bytes')

    except FileNotFoundError:
        raise Exception('macOS `security` command not found')


def get_seed_from_keychain_binary():
    """Read binary seed from Keychain using security command with hex output."""
    result = subprocess.run(
        ['security', 'find-generic-password',
         '-s', 'com.nex.wallet.cli',
         '-a', 'master_seed',
         '-g'],
        capture_output=True, text=True
    )

    if result.returncode != 0:
        raise Exception('Seed not found in Keychain')

    # Parse the -g output which shows: password: 0xABCD...  "escaped"
    for line in result.stderr.split('\n'):
        if 'password:' in line:
            line = line.strip()
            if '0x' in line:
                # Hex format: password: 0xABCD...
                # Extract only valid hex chars after 0x, stop at first space or quote
                raw = line.split('0x')[1]
                hex_str = ''
                for c in raw:
                    if c in '0123456789ABCDEFabcdef':
                        hex_str += c
                    else:
                        break
                return bytes.fromhex(hex_str)
            elif '"' in line:
                text = line.split('"')[1]
                return text.encode('utf-8')

    raise Exception('Could not parse seed from Keychain output')


# ── Bech32m ──
CHARSET = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'

def bech32_polymod(values):
    GEN = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3]
    chk = 1
    for v in values:
        b = chk >> 25
        chk = ((chk & 0x1ffffff) << 5) ^ v
        for i in range(5):
            chk ^= GEN[i] if ((b >> i) & 1) else 0
    return chk

def bech32m_encode(hrp, data):
    """Encode bech32m from hrp + 5-bit data array."""
    values = [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp] + data
    polymod = bech32_polymod(values + [0]*6) ^ 0x2bc830a3
    checksum = [(polymod >> 5*(5-i)) & 31 for i in range(6)]
    return hrp + '1' + ''.join(CHARSET[d] for d in data + checksum)

def bech32m_decode(addr):
    """Decode a bech32m address → (hrp, witness_version, program_bytes)."""
    pos = addr.rfind('1')
    hrp = addr[:pos]
    data_part = addr[pos+1:]
    data = [CHARSET.index(c) for c in data_part]

    # Verify checksum (bech32m: 0x2bc830a3)
    hrp_exp = [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp]
    if bech32_polymod(hrp_exp + data) != 0x2bc830a3:
        raise Exception('Invalid bech32m checksum')

    wit_ver = data[0]
    # Convert 5-bit to 8-bit (exclude checksum)
    acc, bits, program = 0, 0, []
    for v in data[1:-6]:
        acc = (acc << 5) | v
        bits += 5
        while bits >= 8:
            bits -= 8
            program.append((acc >> bits) & 0xff)

    return hrp, wit_ver, bytes(program)


# ── Transaction builder ──
def varint(n):
    if n < 0xfd:
        return struct.pack('<B', n)
    elif n <= 0xffff:
        return b'\xfd' + struct.pack('<H', n)
    elif n <= 0xffffffff:
        return b'\xfe' + struct.pack('<I', n)
    else:
        return b'\xff' + struct.pack('<Q', n)

def push_data(data):
    """Bitcoin script push data encoding."""
    l = len(data)
    if l < 76:
        return struct.pack('<B', l) + data
    elif l <= 0xff:
        return b'\x4c' + struct.pack('<B', l) + data
    elif l <= 0xffff:
        return b'\x4d' + struct.pack('<H', l) + data
    else:
        return b'\x4e' + struct.pack('<I', l) + data


def build_scriptpubkey(address):
    """Build scriptPubKey from a bech32m address."""
    hrp, wit_ver, program = bech32m_decode(address)
    if len(program) != 32:
        raise Exception(f'Expected 32-byte program. Got {len(program)} bytes')
    # OP_n (0x50 + n) + push 32 bytes
    return bytes([0x50 + wit_ver, 0x20]) + program

def build_scriptpubkey_v2(pubkey_hash):
    """Build a v2 scriptPubKey directly from a 32-byte hash."""
    return bytes([0x52, 0x20]) + pubkey_hash

def derive_v2_address(seed, index=0):
    """Derive a v2 PQ address from seed + index tag."""
    tag = f'NEX-CLI-KEY-{index}'.encode()
    h = hashlib.sha256(seed + tag).digest()
    # Encode as bech32m with witness version 2
    wit_ver = 2
    # Convert 8-bit to 5-bit
    acc, bits, data5 = 0, 0, [wit_ver]
    for byte in h:
        acc = (acc << 8) | byte
        bits += 8
        while bits >= 5:
            bits -= 5
            data5.append((acc >> bits) & 31)
    if bits:
        data5.append((acc << (5 - bits)) & 31)
    return bech32m_encode('nex', data5), h


def build_recovery_tx(seed, utxos, dest_address, amount_nex, change_address=None, dest_hash_override=None):
    """
    Build a raw transaction spending CLI wallet UTXOs via seed recovery (Path B).
    """
    amount_sat = int(amount_nex * COIN)

    # Coin selection — pick UTXOs until we have enough (skip first few to avoid conflict with mempool)
    selected = []
    total_in = 0
    for u in utxos[5:]:  # skip first 5 to avoid mempool conflicts
        selected.append(u)
        total_in += int(u['amount'] * COIN)
        if total_in >= amount_sat + MIN_FEE:
            break

    if total_in < amount_sat + MIN_FEE:
        raise Exception(f'Insufficient funds: have {total_in/COIN:.8f}, need {(amount_sat+MIN_FEE)/COIN:.8f}')

    change_sat = total_in - amount_sat - MIN_FEE

    # Build destination scriptPubKey — use hash override if provided (v2 derived)
    if dest_hash_override:
        dest_spk = build_scriptpubkey_v2(dest_hash_override)
    else:
        dest_spk = build_scriptpubkey(dest_address)

    # Build outputs
    outputs = [(amount_sat, dest_spk)]

    # Change output — always back to CLI address (v2)
    if change_sat > 0:
        change_spk = build_scriptpubkey_v2(CLI_PUBKEY_HASH)
        outputs.append((change_sat, change_spk))

    # ── Serialize transaction ──
    tx = b''

    # Version (2 = current)
    tx += struct.pack('<I', 2)

    # Segwit marker + flag
    tx += b'\x00\x01'

    # Inputs
    tx += varint(len(selected))
    for u in selected:
        txid_bytes = bytes.fromhex(u['txid'])[::-1]  # reverse for internal byte order
        tx += txid_bytes
        tx += struct.pack('<I', u['vout'])
        tx += b'\x00'  # empty scriptSig (segwit)
        tx += struct.pack('<I', 0xffffffff)  # sequence

    # Outputs
    tx += varint(len(outputs))
    for (val, spk) in outputs:
        tx += struct.pack('<q', val)
        tx += varint(len(spk)) + spk

    # Witness — each input gets the seed as witness (Path B: 1 item, 64 bytes)
    for _ in selected:
        tx += b'\x01'  # 1 witness item
        tx += varint(len(seed)) + seed  # the 64-byte BIP-39 seed

    # Locktime
    tx += struct.pack('<I', 0)

    return tx.hex()


def main():
    if len(sys.argv) < 3:
        print(f'\n  Usage: python3 seed-spend.py <dest_address> <amount>')
        print(f'  Example: python3 seed-spend.py nex1pw6uu... 3.0\n')
        return

    dest = sys.argv[1]
    amount = float(sys.argv[2])

    if not dest.startswith('nex1'):
        print(f'\n  Error: address must start with nex1\n')
        return

    print(f'\n  NEX Seed Recovery Spend')
    print(f'  ─────────────────────────────────────')

    # Step 1: Get seed from Keychain (ONE read, cached for everything)
    print(f'  Reading seed from Keychain...', end='', flush=True)
    try:
        seed = get_seed_from_keychain_binary()
    except Exception as e:
        print(f'\n  Error: {e}')
        return

    # Verify seed matches the CLI address
    h = hashlib.sha256(seed + b'NEX-CLI-KEY-0').digest()
    if h != CLI_PUBKEY_HASH:
        print(f'\n  Error: Seed does not match CLI wallet address!')
        print(f'  Expected hash: {CLI_PUBKEY_HASH.hex()}')
        print(f'  Got hash:      {h.hex()}')
        return
    print(f' ✓ (matches CLI address)')

    # Check if destination is v2. If not, auto-derive from seed (already loaded).
    dest_hash_override = None
    try:
        _, wit_ver, _ = bech32m_decode(dest)
    except Exception:
        wit_ver = -1
    if wit_ver != 2:
        print(f'  Destination is v{wit_ver} — PQ requires v2. Deriving v2 address...')
        dest, dest_hash_override = derive_v2_address(seed, index=1)
        print(f'  New v2 destination: {dest}')

    # Step 2: Get UTXOs
    print(f'  Scanning UTXOs...', end='', flush=True)
    scan = rpc('scantxoutset', ['start', [f'raw(5220{CLI_PUBKEY_HASH.hex()})#a3zw3tqk']])
    utxos = scan.get('unspents', [])
    total = scan.get('total_amount', 0)
    print(f' ✓ ({len(utxos)} UTXOs, {total:,.8f} NEX)')

    if amount > total:
        print(f'\n  Error: Insufficient balance\n')
        return

    # Step 3: Build transaction
    print(f'  Building transaction...')
    print(f'    To:     {dest}')
    print(f'    Amount: {amount:.8f} NEX')
    print(f'    Fee:    {MIN_FEE/COIN:.8f} NEX')

    raw_tx = build_recovery_tx(seed, utxos, dest, amount, dest_hash_override=dest_hash_override)
    print(f'    Size:   {len(raw_tx)//2} bytes')

    # Step 4: Confirm
    print(f'  ─────────────────────────────────────')
    confirm = input(f'  Broadcast? (yes/no): ').strip().lower()
    if confirm not in ('yes', 'y'):
        print(f'\n  Cancelled.\n')
        # Still print the raw tx for debugging
        print(f'  Raw TX (hex): {raw_tx[:80]}...\n')
        return

    # Step 5: Broadcast
    print(f'  Broadcasting...', end='', flush=True)
    try:
        txid = rpc('sendrawtransaction', [raw_tx])
        print(f' ✓')
        print(f'\n  ✓ SENT!')
        print(f'  TX: {txid}')
        print(f'  Amount: {amount:.8f} NEX → {dest}\n')
    except Exception as e:
        print(f'\n  Failed: {e}')
        print(f'\n  Raw TX saved for debugging:')
        with open('/tmp/nex-recovery-tx.hex', 'w') as f:
            f.write(raw_tx)
        print(f'  /tmp/nex-recovery-tx.hex\n')


if __name__ == '__main__':
    main()
