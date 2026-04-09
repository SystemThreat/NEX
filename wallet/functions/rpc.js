// Cloudflare Pages Function — proxies /rpc to the NEX node
// Handles CORS + injects auth so the browser never needs credentials

const NODE_URL = 'https://street-consultancy-required-queens.trycloudflare.com';
const NODE_AUTH = 'Basic bmV4OjZjNDFlOTkxNDE5YmVmMjkzZDk5MTAyNDNhMTg4ZmMy'; // nex:<rpcpassword>

const CORS_HEADERS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization',
};

export async function onRequestOptions() {
  return new Response(null, { status: 204, headers: CORS_HEADERS });
}

export async function onRequestPost(context) {
  try {
    const url = new URL(context.request.url);
    const wallet = url.searchParams.get('wallet');

    let target = NODE_URL;
    if (wallet) target += '/wallet/' + encodeURIComponent(wallet);

    const body = await context.request.text();

    const resp = await fetch(target, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': NODE_AUTH,
      },
      body,
    });

    const result = await resp.text();

    return new Response(result, {
      status: resp.status,
      headers: {
        'Content-Type': 'application/json',
        ...CORS_HEADERS,
      },
    });
  } catch (e) {
    return new Response(
      JSON.stringify({ error: { message: 'Proxy error: ' + e.message, code: -1 } }),
      {
        status: 502,
        headers: { 'Content-Type': 'application/json', ...CORS_HEADERS },
      }
    );
  }
}
