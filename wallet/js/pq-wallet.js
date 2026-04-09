// ═══════════════════════════════════════════════════════════
// NEX PQ Wallet — Client-side BIP-39 + v2 PQ Address + TX Builder
// Uses only Web Crypto API — no external dependencies
// ═══════════════════════════════════════════════════════════

(function(){
'use strict';

const WORDLIST = window.BIP39_WORDS.split(' ');
const COIN = 100000000;
const MIN_FEE = 10000;
const CHARSET = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l';

// ═══════════ Hex Helpers ═══════════

function hexToBytes(hex) {
  const b = new Uint8Array(hex.length / 2);
  for (let i = 0; i < hex.length; i += 2) b[i/2] = parseInt(hex.substr(i, 2), 16);
  return b;
}

function bytesToHex(bytes) {
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
}

function uint32LE(n) {
  const b = new Uint8Array(4);
  b[0] = n & 0xff; b[1] = (n >> 8) & 0xff; b[2] = (n >> 16) & 0xff; b[3] = (n >> 24) & 0xff;
  return b;
}

function int64LE(n) {
  // For values up to ~9 quadrillion satoshis (safe for JS)
  const b = new Uint8Array(8);
  const lo = n & 0xffffffff;
  const hi = Math.floor(n / 0x100000000) & 0xffffffff;
  b[0] = lo & 0xff; b[1] = (lo >> 8) & 0xff; b[2] = (lo >> 16) & 0xff; b[3] = (lo >> 24) & 0xff;
  b[4] = hi & 0xff; b[5] = (hi >> 8) & 0xff; b[6] = (hi >> 16) & 0xff; b[7] = (hi >> 24) & 0xff;
  return b;
}

function varint(n) {
  if (n < 0xfd) return new Uint8Array([n]);
  if (n <= 0xffff) { const b = new Uint8Array(3); b[0] = 0xfd; b[1] = n & 0xff; b[2] = (n >> 8) & 0xff; return b; }
  const b = new Uint8Array(5); b[0] = 0xfe;
  b[1] = n & 0xff; b[2] = (n >> 8) & 0xff; b[3] = (n >> 16) & 0xff; b[4] = (n >> 24) & 0xff;
  return b;
}

function concatBytes(...arrays) {
  const total = arrays.reduce((s, a) => s + a.length, 0);
  const result = new Uint8Array(total);
  let offset = 0;
  for (const a of arrays) { result.set(a, offset); offset += a.length; }
  return result;
}

// ═══════════ BIP-39 ═══════════

async function sha256(data) {
  const buf = await crypto.subtle.digest('SHA-256', data instanceof Uint8Array ? data : new TextEncoder().encode(data));
  return new Uint8Array(buf);
}

async function generateMnemonic() {
  // 256 bits entropy → 24 words
  const entropy = new Uint8Array(32);
  crypto.getRandomValues(entropy);
  return await entropyToMnemonic(entropy);
}

async function entropyToMnemonic(entropy) {
  const hash = await sha256(entropy);
  // Checksum: first (entropyBits/32) bits of hash = 8 bits for 256-bit entropy
  const checksumBits = entropy.length / 4; // 8

  // Build bit string: entropy + checksum
  let bits = '';
  for (const b of entropy) bits += b.toString(2).padStart(8, '0');
  for (let i = 0; i < checksumBits; i++) bits += ((hash[Math.floor(i/8)] >> (7 - (i % 8))) & 1).toString();

  // Split into 11-bit groups → word indices
  const words = [];
  for (let i = 0; i < bits.length; i += 11) {
    const idx = parseInt(bits.substr(i, 11), 2);
    words.push(WORDLIST[idx]);
  }
  return words.join(' ');
}

function validateMnemonic(phrase) {
  const words = phrase.trim().toLowerCase().split(/\s+/);
  if (words.length !== 12 && words.length !== 15 && words.length !== 18 && words.length !== 21 && words.length !== 24) return false;

  // Check all words exist
  for (const w of words) {
    if (WORDLIST.indexOf(w) === -1) return false;
  }

  // Rebuild bits
  let bits = '';
  for (const w of words) {
    const idx = WORDLIST.indexOf(w);
    bits += idx.toString(2).padStart(11, '0');
  }

  // Split entropy / checksum
  const entropyBits = (words.length * 11 * 32) / 33;
  const checksumBits = bits.length - entropyBits;

  const entropyBytes = [];
  for (let i = 0; i < entropyBits; i += 8) {
    entropyBytes.push(parseInt(bits.substr(i, 8), 2));
  }

  // We can't do async checksum validation in a sync function
  // Basic validation passes if all words are valid and length is correct
  return true;
}

async function validateMnemonicAsync(phrase) {
  const words = phrase.trim().toLowerCase().split(/\s+/);
  if (words.length !== 12 && words.length !== 15 && words.length !== 18 && words.length !== 21 && words.length !== 24) return false;

  for (const w of words) {
    if (WORDLIST.indexOf(w) === -1) return false;
  }

  let bits = '';
  for (const w of words) {
    const idx = WORDLIST.indexOf(w);
    bits += idx.toString(2).padStart(11, '0');
  }

  const entropyBits = (words.length * 11 * 32) / 33;
  const checksumBits = bits.length - entropyBits;

  const entropyHex = [];
  for (let i = 0; i < entropyBits; i += 8) {
    entropyHex.push(parseInt(bits.substr(i, 8), 2));
  }
  const entropy = new Uint8Array(entropyHex);
  const hash = await sha256(entropy);

  // Verify checksum
  let checksumActual = '';
  for (let i = 0; i < checksumBits; i++) {
    checksumActual += ((hash[Math.floor(i/8)] >> (7 - (i % 8))) & 1).toString();
  }
  const checksumExpected = bits.substr(entropyBits);
  return checksumActual === checksumExpected;
}

async function mnemonicToSeed(phrase) {
  // BIP-39: PBKDF2-HMAC-SHA512, 2048 iterations, salt = "mnemonic"
  const enc = new TextEncoder();
  const keyMaterial = await crypto.subtle.importKey(
    'raw', enc.encode(phrase.normalize('NFKD')), 'PBKDF2', false, ['deriveBits']
  );
  const seedBuf = await crypto.subtle.deriveBits(
    { name: 'PBKDF2', salt: enc.encode('mnemonic'), iterations: 2048, hash: 'SHA-512' },
    keyMaterial, 512
  );
  return new Uint8Array(seedBuf);
}

// ═══════════ Bech32m ═══════════

function bech32Polymod(values) {
  const GEN = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3];
  let chk = 1;
  for (const v of values) {
    const b = chk >>> 25;
    chk = ((chk & 0x1ffffff) << 5) ^ v;
    for (let i = 0; i < 5; i++) {
      if ((b >>> i) & 1) chk ^= GEN[i];
    }
  }
  return chk;
}

function bech32mEncode(hrp, data5bit) {
  const hrpExp = [];
  for (const c of hrp) { hrpExp.push(c.charCodeAt(0) >> 5); }
  hrpExp.push(0);
  for (const c of hrp) { hrpExp.push(c.charCodeAt(0) & 31); }

  const values = hrpExp.concat(data5bit).concat([0,0,0,0,0,0]);
  const polymod = bech32Polymod(values) ^ 0x2bc830a3;
  const checksum = [];
  for (let i = 0; i < 6; i++) {
    checksum.push((polymod >>> (5 * (5 - i))) & 31);
  }
  let result = hrp + '1';
  for (const d of data5bit.concat(checksum)) {
    result += CHARSET[d];
  }
  return result;
}

function bech32mDecode(addr) {
  const pos = addr.lastIndexOf('1');
  const hrp = addr.substring(0, pos);
  const dataPart = addr.substring(pos + 1);

  const data = [];
  for (const c of dataPart) {
    const idx = CHARSET.indexOf(c);
    if (idx === -1) throw new Error('Invalid bech32m character: ' + c);
    data.push(idx);
  }

  const hrpExp = [];
  for (const c of hrp) { hrpExp.push(c.charCodeAt(0) >> 5); }
  hrpExp.push(0);
  for (const c of hrp) { hrpExp.push(c.charCodeAt(0) & 31); }

  if (bech32Polymod(hrpExp.concat(data)) !== 0x2bc830a3) {
    throw new Error('Invalid bech32m checksum');
  }

  const witVer = data[0];
  // Convert 5-bit to 8-bit (exclude checksum = last 6)
  let acc = 0, bits = 0;
  const program = [];
  for (let i = 1; i < data.length - 6; i++) {
    acc = (acc << 5) | data[i];
    bits += 5;
    while (bits >= 8) {
      bits -= 8;
      program.push((acc >>> bits) & 0xff);
    }
  }

  return { hrp, witVer, program: new Uint8Array(program) };
}

// ═══════════ v2 PQ Address ═══════════

async function deriveV2Address(seedBytes, index) {
  if (typeof index === 'undefined') index = 0;
  const tag = new TextEncoder().encode('NEX-CLI-KEY-' + index);
  const combined = concatBytes(seedBytes, tag);
  const hash = await sha256(combined);

  // Encode as bech32m: witness version 2 + 32-byte program
  const data5 = [2]; // witness version 2
  let acc = 0, bits = 0;
  for (const byte of hash) {
    acc = (acc << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      data5.push((acc >>> bits) & 31);
    }
  }
  if (bits > 0) data5.push((acc << (5 - bits)) & 31);

  const address = bech32mEncode('nex', data5);
  return { address, programHex: bytesToHex(hash), program: hash };
}

function buildScriptPubKey(address) {
  const { witVer, program } = bech32mDecode(address);
  // OP_n = 0x50 + n, then push 32 bytes
  return concatBytes(new Uint8Array([0x50 + witVer, 0x20]), program);
}

function buildScriptPubKeyFromHash(hash) {
  // OP_2 (0x52) + push 32 (0x20) + hash
  return concatBytes(new Uint8Array([0x52, 0x20]), hash instanceof Uint8Array ? hash : hexToBytes(hash));
}

// ═══════════ Transaction Builder ═══════════

function buildRecoveryTx(seedBytes, utxos, destAddress, amountNex, changeAddress) {
  const amountSat = Math.round(amountNex * COIN);

  // Coin selection (skip immature coinbase — need 100 confirmations)
  const selected = [];
  let totalIn = 0;
  for (const u of utxos) {
    if (u.coinbase && u.confirmations < 100) continue;
    selected.push(u);
    totalIn += Math.round(u.amount * COIN);
    if (totalIn >= amountSat + MIN_FEE) break;
  }

  if (totalIn < amountSat + MIN_FEE) {
    throw new Error('Insufficient funds: have ' + (totalIn/COIN).toFixed(8) + ', need ' + ((amountSat+MIN_FEE)/COIN).toFixed(8));
  }

  const changeSat = totalIn - amountSat - MIN_FEE;

  // Build destination scriptPubKey
  const destSPK = buildScriptPubKey(destAddress);

  // Build outputs
  const outputs = [{ sat: amountSat, spk: destSPK }];

  if (changeSat > 0 && changeAddress) {
    const changeSPK = buildScriptPubKey(changeAddress);
    outputs.push({ sat: changeSat, spk: changeSPK });
  }

  // ── Serialize ──
  const parts = [];

  // Version 2
  parts.push(uint32LE(2));

  // Segwit marker + flag
  parts.push(new Uint8Array([0x00, 0x01]));

  // Inputs
  parts.push(varint(selected.length));
  for (const u of selected) {
    // txid in internal byte order (reversed)
    const txidBytes = hexToBytes(u.txid);
    txidBytes.reverse();
    parts.push(txidBytes);
    parts.push(uint32LE(u.vout));
    parts.push(new Uint8Array([0x00])); // empty scriptSig
    parts.push(uint32LE(0xffffffff));   // sequence
  }

  // Outputs
  parts.push(varint(outputs.length));
  for (const o of outputs) {
    parts.push(int64LE(o.sat));
    parts.push(varint(o.spk.length));
    parts.push(o.spk);
  }

  // Witness — each input gets the 64-byte seed (Path B: 1 witness item)
  for (let i = 0; i < selected.length; i++) {
    parts.push(new Uint8Array([0x01])); // 1 witness item
    parts.push(varint(seedBytes.length));
    parts.push(seedBytes);
  }

  // Locktime
  parts.push(uint32LE(0));

  return bytesToHex(concatBytes(...parts));
}

// ═══════════ Export ═══════════

window.PQWallet = {
  WORDLIST,
  generateMnemonic,
  validateMnemonic,
  validateMnemonicAsync,
  mnemonicToSeed,
  deriveV2Address,
  buildScriptPubKey,
  buildScriptPubKeyFromHash,
  buildRecoveryTx,
  bech32mEncode,
  bech32mDecode,
  hexToBytes,
  bytesToHex,
  sha256,
  COIN,
  MIN_FEE
};

})();
