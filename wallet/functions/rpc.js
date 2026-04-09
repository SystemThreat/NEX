// Cloudflare Pages Function — proxies /rpc to the NEX node
// Security:
//   - Method whitelist: only safe read/broadcast methods allowed
//   - Server-side auth injection: browsers never send credentials
//   - Filters immature coinbase from scantxoutset responses (100-block maturity)

const NODE_URL = 'https://street-consultancy-required-queens.trycloudflare.com';
const NODE_AUTH = 'Basic bmV4OjNmZTBiMjljOTEwNDE3ZWY4ZWZmNTk1MjIyNjkwZTQxMjFiNmU2M2E4MmVjMmNiMWVmYWQ1ZDA4ODc4ZTFjZWI=';

const COINBASE_MATURITY = 100;

const ALLOWED_METHODS = new Set([
  // Chain info
  'getblockchaininfo', 'getmininginfo', 'getnetworkinfo',
  'getblockcount', 'getbestblockhash', 'getdifficulty',
  // Block lookups
  'getblockhash', 'getblock', 'getblockheader', 'getblockstats',
  // Transaction lookups
  'getrawtransaction', 'decoderawtransaction', 'gettxout',
  'gettxoutproof', 'verifytxoutproof',
  // Mempool
  'getrawmempool', 'getmempoolinfo', 'getmempoolentry',
  'testmempoolaccept',
  // UTXO scanning (essential for PQ wallet)
  'scantxoutset',
  // Broadcasting signed transactions (wallet signs client-side)
  'sendrawtransaction',
  // Address/script validation
  'validateaddress', 'decodescript',
  // Fee estimation
  'estimatesmartfee',
  // Utility
  'uptime', 'getrpcinfo',
]);

const CORS_HEADERS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization',
};

function jsonResponse(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json', ...CORS_HEADERS },
  });
}

export async function onRequestOptions() {
  return new Response(null, { status: 204, headers: CORS_HEADERS });
}

export async function onRequestPost(context) {
  // Parse request body
  let req;
  try {
    const bodyText = await context.request.text();
    req = JSON.parse(bodyText);
  } catch {
    return jsonResponse({ error: { code: -32700, message: 'Parse error' } }, 400);
  }

  const method = req.method || '';
  const reqId = req.id || 0;

  // Method whitelist
  if (!ALLOWED_METHODS.has(method)) {
    return jsonResponse({
      jsonrpc: '1.0',
      id: reqId,
      result: null,
      error: { code: -32601, message: `Method '${method}' not allowed` },
    }, 403);
  }

  // Forward to upstream node (strip any client-provided auth, inject server-side)
  let resp;
  try {
    resp = await fetch(NODE_URL, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': NODE_AUTH,
      },
      body: JSON.stringify(req),
    });
  } catch (e) {
    return jsonResponse({
      jsonrpc: '1.0',
      id: reqId,
      result: null,
      error: { code: -32603, message: 'Upstream error: ' + e.message },
    }, 502);
  }

  const resultText = await resp.text();

  // Filter immature coinbase from scantxoutset
  if (method === 'scantxoutset') {
    try {
      const data = JSON.parse(resultText);
      if (data.result && Array.isArray(data.result.unspents)) {
        const mature = data.result.unspents.filter(
          u => !(u.coinbase && (u.confirmations ?? 999) < COINBASE_MATURITY)
        );
        data.result.unspents = mature;
        data.result.total_amount = Math.round(
          mature.reduce((sum, u) => sum + u.amount, 0) * 1e8
        ) / 1e8;
        return jsonResponse(data, resp.status);
      }
    } catch {
      // Fall through to return original
    }
  }

  return new Response(resultText, {
    status: resp.status,
    headers: { 'Content-Type': 'application/json', ...CORS_HEADERS },
  });
}
