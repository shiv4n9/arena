/*
 * Tombstone for the former coi-serviceworker.
 *
 * Earlier ARENA builds registered a coi-serviceworker to force COOP/COEP
 * cross-origin isolation (needed when the WASM used SharedArrayBuffer). The
 * engine is now single-threaded and isolation is no longer required, so this
 * file replaces the old worker with one that unregisters itself. Returning
 * visitors fetch this on their next navigation; it then deletes itself and
 * reloads any pages it controlled, leaving a clean, non-isolated origin.
 *
 * index.html intentionally does NOT register this — new visitors never get a
 * service worker at all. This file exists only to retire stale registrations.
 */
self.addEventListener('install', () => self.skipWaiting());

self.addEventListener('activate', (event) => {
  event.waitUntil(
    (async () => {
      try {
        await self.registration.unregister();
      } catch (_) { /* ignore */ }
      const clients = await self.clients.matchAll({ type: 'window' });
      for (const client of clients) {
        try { client.navigate(client.url); } catch (_) { /* ignore */ }
      }
    })()
  );
});

// Pass every request straight through — no interception, no injected headers.
self.addEventListener('fetch', () => {});
