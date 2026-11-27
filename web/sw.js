// SolEngine - Offline PWA Service Worker with COOP/COEP Cross-Origin Isolation
const CACHE_NAME = 'solengine-pwa-v4';

const PRECACHE_ASSETS = [
    './',
    './index.html',
    './solengine.js',
    './manifest.json',
    './favicon.ico',
    './apple-touch-icon.png',
    './icon-192.png',
    './icon-512.png',
    './icon-192-maskable.png',
    './icon-512-maskable.png',
    './solengine_logo.svg'
];

// Heavy payloads: prefer pre-compressed .gz (used via DecompressionStream),
// falling back to uncompressed if .gz is not served (e.g. raw dev builds).
const PAYLOAD_PAIRS = [
    ['./solengine.wasm.gz', './solengine.wasm'],
    ['./solengine.data.gz', './solengine.data']
];

function ensureCoopCoepHeaders(response) {
    if (
        !response ||
        response.type === 'opaque' ||
        response.type === 'opaqueredirect' ||
        response.status === 0 ||
        response.status === 204 ||
        response.status === 205 ||
        response.status === 304 ||
        response.status < 200 ||
        response.status > 599
    ) {
        return response;
    }
    const headers = new Headers(response.headers);
    let modified = false;

    if (!headers.get('Cross-Origin-Opener-Policy')) {
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        modified = true;
    }
    if (!headers.get('Cross-Origin-Embedder-Policy')) {
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
        modified = true;
    }
    if (!headers.get('Cross-Origin-Resource-Policy')) {
        headers.set('Cross-Origin-Resource-Policy', 'same-origin');
        modified = true;
    }

    if (modified) {
        return new Response(response.body, {
            status: response.status,
            statusText: response.statusText,
            headers: headers
        });
    }
    return response;
}

async function cacheAsset(cache, url) {
    const resp = await fetch(new Request(url, { cache: 'reload' }));
    if (!resp.ok) {
        throw new Error(`HTTP ${resp.status} for ${url}`);
    }
    await cache.put(url, resp);
}

self.addEventListener('install', (event) => {
    event.waitUntil((async () => {
        const cache = await caches.open(CACHE_NAME);

        // Cache each asset individually so a single optional miss never wipes the precache.
        await Promise.allSettled(
            PRECACHE_ASSETS.map((url) =>
                cacheAsset(cache, url).catch((err) => {
                    console.warn('[SolEngine SW] Precache skipped:', url, err);
                })
            )
        );

        await Promise.allSettled(
            PAYLOAD_PAIRS.map(async ([primary, fallback]) => {
                try {
                    await cacheAsset(cache, primary);
                } catch (err) {
                    console.warn('[SolEngine SW] Primary payload unavailable, caching fallback:', fallback, err);
                    await cacheAsset(cache, fallback).catch((fbErr) => {
                        console.warn('[SolEngine SW] Fallback payload precache failed:', fallback, fbErr);
                    });
                }
            })
        );

        await self.skipWaiting();
    })());
});

self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys().then((keys) =>
            Promise.all(
                keys.map((key) => (key !== CACHE_NAME ? caches.delete(key) : Promise.resolve()))
            )
        ).then(() => self.clients.claim())
    );
});

async function matchFromCache(request) {
    const cache = await caches.open(CACHE_NAME);
    const exact = await cache.match(request);
    if (exact) return ensureCoopCoepHeaders(exact);

    const ignoreSearch = await cache.match(request, { ignoreSearch: true });
    if (ignoreSearch) return ensureCoopCoepHeaders(ignoreSearch);

    if (request.mode === 'navigate' || request.headers.get('accept')?.includes('text/html')) {
        const indexFallback = (await cache.match('./index.html')) || (await cache.match('./'));
        if (indexFallback) return ensureCoopCoepHeaders(indexFallback);
    }

    return new Response('Offline', {
        status: 503,
        statusText: 'Service Unavailable',
        headers: { 'Content-Type': 'text/plain; charset=utf-8' }
    });
}

self.addEventListener('fetch', (event) => {
    if (event.request.method !== 'GET') return;
    if (event.request.cache === 'only-if-cached' && event.request.mode !== 'same-origin') return;

    const url = new URL(event.request.url);
    // Let cross-origin requests (Google Fonts, GitHub Gist/raw imports) bypass the SW
    // so we never mutate their headers or fail them when offline.
    if (url.origin !== self.location.origin) return;

    // For top-level navigations, bypass the HTTP cache so we never get a 304 Not Modified
    // without COOP/COEP headers; fall back to Cache Storage when offline.
    const fetchRequest = event.request.mode === 'navigate'
        ? new Request(event.request.url, { cache: 'no-store' })
        : event.request;

    // Network-first so rebuilt WASM/JS/data/HTML are picked up immediately when online,
    // falling back to Cache Storage when offline or on server errors.
    event.respondWith((async () => {
        try {
            const networkResponse = await fetch(fetchRequest);
            if (networkResponse && networkResponse.ok) {
                const responseToCache = networkResponse.clone();
                event.waitUntil(
                    caches.open(CACHE_NAME).then((cache) => cache.put(event.request, responseToCache))
                );
                return ensureCoopCoepHeaders(networkResponse);
            }
            if (networkResponse && networkResponse.status >= 500) {
                return matchFromCache(event.request);
            }
            return ensureCoopCoepHeaders(networkResponse);
        } catch (err) {
            return matchFromCache(event.request);
        }
    })());
});
