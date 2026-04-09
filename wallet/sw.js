// NEX Wallet Service Worker v3 — Safari-safe, no redirect responses ever cached
const CACHE = 'nex-wallet-v5';

self.addEventListener('install', e => {
  // Skip caching on install entirely — avoids Safari redirect-in-cache bug
  self.skipWaiting();
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys().then(keys =>
      Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))
    )
  );
  self.clients.claim();
});

self.addEventListener('fetch', e => {
  const url = new URL(e.request.url);

  // Only handle same-origin GET requests
  if (e.request.method !== 'GET') return;
  if (url.origin !== self.location.origin) return;

  // Never intercept /rpc calls
  if (url.pathname.startsWith('/rpc')) return;

  // Navigation: always go to network, never serve from cache
  if (e.request.mode === 'navigate') {
    e.respondWith(
      fetch(e.request).catch(() => caches.match('/index.html'))
    );
    return;
  }

  // Static assets: network-first, cache clean responses for offline
  e.respondWith(
    fetch(e.request).then(resp => {
      // Only cache clean 200 responses, never redirects
      if (resp.ok && resp.status === 200 && !resp.redirected && resp.type === 'basic') {
        const clone = resp.clone();
        caches.open(CACHE).then(c => c.put(e.request, clone));
      }
      return resp;
    }).catch(() => caches.match(e.request))
  );
});
